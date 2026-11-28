#include "query/optimizer/normalization/plan_normalizations.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/analysis/plan_refs.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_expression_walk.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_correlated_source_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_edge_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_node_op.hpp"
#include "query/planner/logical_plan/operator/bound_delete_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_foreach_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_apply_op.hpp"
#include "query/planner/logical_plan/operator/bound_remove_op.hpp"
#include "query/planner/logical_plan/operator/bound_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_set_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

namespace eugraph {
namespace optimizer {
namespace {
using binder::BoundLogicalOperator;

void collectExprVars(const binder::BoundExpression& expr, std::unordered_set<std::string>& out) {
    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                out.insert(val.name);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                out.insert(val.name);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (val)
                    collectExprVars(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (val)
                    collectExprVars(val->object, out);
            } else {
                binder::forEachSubExpr(expr, [&](const binder::BoundExpression& c) { collectExprVars(c, out); });
            }
        },
        expr);
}

void collectOpVars(const BoundLogicalOperator& op, std::unordered_set<std::string>& out) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                out.insert(v.variable);
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                out.insert(v.variable);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    if (!v->src_variable.empty())
                        out.insert(v->src_variable);
                    if (!v->dst_variable.empty())
                        out.insert(v->dst_variable);
                    if (!v->edge_variable.empty())
                        out.insert(v->edge_variable);
                }
            }
        },
        op);
    forEachLogicalChild(op, [&](const BoundLogicalOperator& c) { collectOpVars(c, out); });
}

void splitAnd(const binder::BoundExpression& expr, std::vector<const binder::BoundExpression*>& out) {
    if (auto* bin = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&expr)) {
        if (*bin && (*bin)->op == cypher::BinaryOperator::AND) {
            splitAnd((*bin)->left, out);
            splitAnd((*bin)->right, out);
            return;
        }
    }
    out.push_back(&expr);
}

bool isJoinEquality(const binder::BoundExpression& expr, const std::unordered_set<std::string>& left_vars,
                    const std::unordered_set<std::string>& right_vars) {
    const auto* bin = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&expr);
    if (!bin || !*bin || (*bin)->op != cypher::BinaryOperator::EQ)
        return false;
    std::unordered_set<std::string> lv;
    std::unordered_set<std::string> rv;
    collectExprVars((*bin)->left, lv);
    collectExprVars((*bin)->right, rv);
    auto subset = [](const auto& a, const auto& b) {
        for (const auto& v : a)
            if (b.find(v) == b.end())
                return false;
        return true;
    };
    return (!lv.empty() && !rv.empty()) &&
           ((subset(lv, left_vars) && subset(rv, right_vars)) || (subset(lv, right_vars) && subset(rv, left_vars)));
}

void extractJoinConditions(binder::BoundLogicalOperator& op) {
    forEachLogicalChild(op, [&](binder::BoundLogicalOperator& child) { extractJoinConditions(child); });
    auto* fp = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&op);
    if (!fp || !*fp)
        return;
    auto& filter = **fp;
    auto* jp = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&filter.child);
    if (!jp || !*jp)
        return;
    auto& join = **jp;
    if (!join.join_conditions.empty())
        return;
    if (join.join_type != binder::JoinType::Cross && join.join_type != binder::JoinType::Inner)
        return;
    std::unordered_set<std::string> lv;
    std::unordered_set<std::string> rv;
    collectOpVars(join.left, lv);
    collectOpVars(join.right, rv);
    std::vector<const binder::BoundExpression*> conjuncts;
    splitAnd(filter.predicate, conjuncts);
    for (const auto* c : conjuncts) {
        if (isJoinEquality(*c, lv, rv))
            join.join_conditions.push_back(cloneBoundExpression(*c));
    }
}

bool subtreeNeedsConservativeJoinOrder(const binder::BoundLogicalOperator& op) {
    bool unsafe = false;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
                unsafe = true;
            }
        },
        op);
    if (unsafe)
        return true;
    forEachLogicalChild(op, [&](const binder::BoundLogicalOperator& child) {
        if (!unsafe)
            unsafe = subtreeNeedsConservativeJoinOrder(child);
    });
    return unsafe;
}

/// Annotate dst-bound Expands whose destination column is supplied by a
/// CorrelatedSource below: the operator may materialise that destination's
/// adjacency once per batch instead of seeking from every source row. This is
/// an execution-strategy annotation only — the shape already comes from the
/// binder (P2) and no cost comparison happens here (degree statistics are P3).
void annotateCorrelatedEndpointProbe(BoundLogicalOperator& op, bool allow_probe) {
    if (allow_probe) {
        std::visit(
            [&](auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                    if (!v || v->dst_variable.empty())
                        return;
                    // Only a directed pattern can flip the traversal: the
                    // probe reads the destination's adjacency in the opposite
                    // direction. Undirected keeps the exact-seek path.
                    if (v->direction == cypher::RelationshipDirection::UNDIRECTED)
                        return;
                    // The destination must be the correlated outer value
                    // injected by the sub-plan source, not an ordinary column.
                    if (!subtreeProvidesCorrelatedVariable(v->child, v->dst_variable))
                        return;
                    v->probe_bound_dst = true;
                }
            },
            op);
    }
    forEachLogicalChild(
        op, [&](binder::BoundLogicalOperator& child) { annotateCorrelatedEndpointProbe(child, allow_probe); });
}

} // namespace

void AnnotateCorrelatedEndpointProbeNormalization::apply(binder::BoundLogicalOperator& root,
                                                         NormalizationContext&) const {
    // The probe keeps a destination's adjacency for the batch and assumes the
    // statement's snapshot is stable, so write plans keep exact seeks. The
    // shape (dst-bound) is produced by the binder either way.
    annotateCorrelatedEndpointProbe(root, !planHasWriteOperator(root));
}

void JoinConditionNormalization::apply(binder::BoundLogicalOperator& root, NormalizationContext&) const {
    extractJoinConditions(root);
}

void JoinSafetyNormalization::apply(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const {
    ctx.commute_safe = !subtreeNeedsConservativeJoinOrder(root);
}

} // namespace optimizer
} // namespace eugraph
