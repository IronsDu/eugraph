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
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_fork_op.hpp"
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

void markShortestPathBatch(binder::BoundLogicalOperator& op) {
    std::visit(
        [](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v && v->shortest_path && !v->all_shortest_paths && v->max_hops >= 0 && v->max_hops <= 8)
                    v->batch_shortest_path = true;
            }
        },
        op);
    forEachLogicalChild(op, [](binder::BoundLogicalOperator& child) { markShortestPathBatch(child); });
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
                          std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionForkOp>>) {
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

/// Fold one endpoint filter into the Expand below it. Recurses into children so
/// nested correlated chains fold as a whole.
bool foldExpandEndpointFilters(BoundLogicalOperator& op, bool allow_probe,
                               const std::unordered_map<std::string, int>& expr_refs,
                               const std::unordered_map<std::string, int>& field_refs) {
    if (auto* filter_holder = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&op)) {
        if (!*filter_holder)
            return false;
        auto& filter = **filter_holder;
        auto* bin_holder = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&filter.predicate);
        if (bin_holder && *bin_holder && (*bin_holder)->op == cypher::BinaryOperator::EQ) {
            const auto* left = vertexRefIdArgument((*bin_holder)->left);
            const auto* right = vertexRefIdArgument((*bin_holder)->right);
            // CBO's FilterPushdown may insert filters between the endpoint
            // equality and the Expand that produced the destination (e.g. the
            // edge-uniqueness predicate). Look through a chain of filters that do
            // not themselves reference the destination: moving the equality into
            // the Expand keeps those filters above it unchanged.
            auto expand_for = [&](const std::string& name) -> binder::BoundExpandOp* {
                auto find = [&](auto&& self, binder::BoundLogicalOperator& node) -> binder::BoundExpandOp* {
                    if (auto* expand = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&node)) {
                        return (expand && *expand && (*expand)->dst_variable == name) ? expand->get() : nullptr;
                    }
                    if (auto* mid = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&node)) {
                        if (!mid || !*mid || expressionReferences((*mid)->predicate, name))
                            return nullptr;
                        return self(self, (*mid)->child);
                    }
                    return nullptr;
                };
                return find(find, filter.child);
            };
            for (const binder::BoundColumnRef* dst : {left, right}) {
                if (dst == nullptr)
                    continue;
                binder::BoundColumnRef* saved = dst == left ? const_cast<binder::BoundColumnRef*>(right)
                                                            : const_cast<binder::BoundColumnRef*>(left);
                if (saved == nullptr || saved->name == dst->name)
                    continue;
                binder::BoundExpandOp* expand = expand_for(dst->name);
                if (expand == nullptr)
                    continue;
                // The destination must be the only expression reference (this
                // equality) and its Expand the only field reference; renaming it
                // must not dangle a later consumer.
                auto eit = expr_refs.find(dst->name);
                auto fit = field_refs.find(dst->name);
                const int expr_count = eit == expr_refs.end() ? 0 : eit->second;
                const int field_count = fit == field_refs.end() ? 0 : fit->second;
                if (expr_count != 1 || field_count != 1)
                    continue;
                if (!subtreeProvidesCorrelatedVariable(expand->child, saved->name))
                    continue;

                expand->dst_variable = saved->name;
                expand->dst_column_index = saved->column_index;
                expand->dst_slot_id = saved->slot_id;
                expand->planner_dst_slot_id = saved->slot_id;
                // A directed correlation can be flipped: the bound destination's
                // adjacency is materialised once per batch and the source rows
                // are probed against it. Undirected keeps the exact-seek path;
                // write plans keep the ordinary shape (correlated memoisation is
                // read-only).
                expand->probe_bound_dst = allow_probe && expand->direction != cypher::RelationshipDirection::UNDIRECTED;
                binder::BoundLogicalOperator rewritten = std::move(filter.child);
                op = std::move(rewritten);
                foldExpandEndpointFilters(op, allow_probe, expr_refs, field_refs);
                return true;
            }
        }
    }
    bool changed = false;
    forEachLogicalChild(op, [&](binder::BoundLogicalOperator& child) {
        changed = foldExpandEndpointFilters(child, allow_probe, expr_refs, field_refs) || changed;
    });
    return changed;
}

} // namespace

void FoldExpandEndpointFilterNormalization::apply(binder::BoundLogicalOperator& root, NormalizationContext&) const {
    PlanRefIndex refs = collectPlanRefs(root);
    if (!refs.complete)
        return;
    std::unordered_map<std::string, int> field_refs;
    countPlanFieldRefs(root, field_refs);
    foldExpandEndpointFilters(root, !planHasWriteOperator(root), refs.expr_refs, field_refs);
}

void JoinConditionNormalization::apply(binder::BoundLogicalOperator& root, NormalizationContext&) const {
    extractJoinConditions(root);
}

void ShortestPathBatchNormalization::apply(binder::BoundLogicalOperator& root, NormalizationContext&) const {
    markShortestPathBatch(root);
}

void JoinSafetyNormalization::apply(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const {
    ctx.commute_safe = !subtreeNeedsConservativeJoinOrder(root);
}

} // namespace optimizer
} // namespace eugraph
