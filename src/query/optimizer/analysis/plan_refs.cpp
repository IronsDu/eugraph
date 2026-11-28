#include "query/optimizer/analysis/plan_refs.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/planner/bound_expression/bound_expression_walk.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_edge_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_node_op.hpp"
#include "query/planner/logical_plan/operator/bound_delete_op.hpp"
#include "query/planner/logical_plan/operator/bound_distinct_op.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_foreach_op.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_limit_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_build_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_count_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_apply_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_fork_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"
#include "query/planner/logical_plan/operator/bound_remove_op.hpp"
#include "query/planner/logical_plan/operator/bound_set_op.hpp"
#include "query/planner/logical_plan/operator/bound_skip_op.hpp"
#include "query/planner/logical_plan/operator/bound_sort_op.hpp"
#include "query/planner/logical_plan/operator/bound_union_op.hpp"
#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

#include <algorithm>
#include <type_traits>

namespace eugraph {
namespace optimizer {

namespace {

void countExprRefs(const binder::BoundExpression& expr, std::unordered_map<std::string, int>& refs) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, binder::BoundVariableRef> || std::is_same_v<T, binder::BoundColumnRef>) {
                ++refs[value.name];
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (value)
                    countExprRefs(value->object, refs);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (value)
                    countExprRefs(value->object, refs);
            }
        },
        expr);
    binder::forEachSubExpr(expr, [&](const binder::BoundExpression& child) { countExprRefs(child, refs); });
}

} // namespace

const binder::BoundColumnRef* exprRef(const binder::BoundExpression& expr) {
    return std::get_if<binder::BoundColumnRef>(&expr);
}

const binder::BoundVariableRef* exprVarRef(const binder::BoundExpression& expr) {
    return std::get_if<binder::BoundVariableRef>(&expr);
}

std::string bareRefName(const binder::BoundExpression& expr) {
    if (const auto* ref = exprRef(expr))
        return ref->name;
    if (const auto* ref = exprVarRef(expr))
        return ref->name;
    return {};
}

const binder::BoundColumnRef* vertexRefIdArgument(const binder::BoundExpression& expr) {
    auto* call = std::get_if<std::unique_ptr<binder::BoundFunctionCall>>(&expr);
    if (!call || !*call || !(*call)->func_def || (*call)->func_def->name != "id" || (*call)->args.size() != 1)
        return nullptr;
    auto* ref = std::get_if<binder::BoundColumnRef>(&(*call)->args[0]);
    if (!ref || ref->type.kind != binder::BoundTypeKind::VERTEX_REF)
        return nullptr;
    return ref;
}

bool subtreeProvidesCorrelatedVariable(const binder::BoundLogicalOperator& root, const std::string& name) {
    bool found = false;
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                found = std::find(value.variables.begin(), value.variables.end(), name) != value.variables.end();
            }
        },
        root);
    if (found)
        return true;
    forEachLogicalChild(root, [&](const binder::BoundLogicalOperator& child) {
        if (!found)
            found = subtreeProvidesCorrelatedVariable(child, name);
    });
    return found;
}

bool expressionReferences(const binder::BoundExpression& expr, const std::string& name) {
    if (const auto* ref = exprRef(expr); ref && ref->name == name)
        return true;
    if (const auto* ref = exprVarRef(expr); ref && ref->name == name)
        return true;
    bool found = false;
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (value)
                    found = expressionReferences(value->object, name);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (value)
                    found = expressionReferences(value->object, name);
            }
        },
        expr);
    if (found)
        return true;
    binder::forEachSubExpr(expr, [&](const binder::BoundExpression& child) {
        if (!found)
            found = expressionReferences(child, name);
    });
    return found;
}

PlanRefIndex collectPlanRefs(const binder::BoundLogicalOperator& root) {
    PlanRefIndex index;
    auto visit_expr = [&](const binder::BoundExpression& e) {
        if (const auto* call = std::get_if<std::unique_ptr<binder::BoundFunctionCall>>(&e)) {
            if (*call && (*call)->func_def && (*call)->func_def->name == "size") {
                for (const auto& arg : (*call)->args) {
                    if (const auto* ref = exprVarRef(arg))
                        ++index.size_arg_refs[ref->name];
                    else if (const auto* ref = exprRef(arg))
                        ++index.size_arg_refs[ref->name];
                }
            }
        }
        countExprRefs(e, index.expr_refs);
    };
    auto recurse = [&](const binder::BoundLogicalOperator& child) {
        PlanRefIndex sub = collectPlanRefs(child);
        index.complete = index.complete && sub.complete;
        for (const auto& [name, count] : sub.expr_refs)
            index.expr_refs[name] += count;
        for (const auto& [name, count] : sub.size_arg_refs)
            index.size_arg_refs[name] += count;
        index.path_elements.insert(sub.path_elements.begin(), sub.path_elements.end());
    };
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (!v)
                    return;
                visit_expr(v->predicate);
                recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (!v)
                    return;
                for (const auto& item : v->items)
                    visit_expr(item.expr);
                recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (!v)
                    return;
                for (const auto& item : v->items)
                    visit_expr(item.expr);
                recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (!v)
                    return;
                for (const auto& key : v->group_keys)
                    visit_expr(key);
                for (const auto& agg : v->aggregates)
                    for (const auto& arg : agg.arguments)
                        visit_expr(arg);
                recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (!v)
                    return;
                visit_expr(v->list_expr);
                recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPathCountOp>>) {
                if (v)
                    recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (!v)
                    return;
                for (const auto& name : v->element_variables)
                    index.path_elements.insert(name);
                recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v)
                    recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (!v)
                    return;
                if (!v->path_variable.empty())
                    index.path_elements.insert(v->path_variable);
                recurse(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
                if (!v)
                    return;
                recurse(v->left);
                recurse(v->right);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionForkOp>>) {
                if (!v)
                    return;
                recurse(v->left);
                recurse(v->anchor);
                for (const auto& branch : v->branches)
                    recurse(branch);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundIndexProbeJoinOp>>) {
                if (v)
                    recurse(v->anchor);
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                                 std::is_same_v<T, binder::BoundScanOp> ||
                                 std::is_same_v<T, binder::BoundLabelScanOp> ||
                                 std::is_same_v<T, binder::BoundSingletonOp>) {
                // leaves: no expressions
            } else {
                // Write operators / CALL / anything with expression fields this
                // walker does not enumerate. Callers must not rename fields when
                // a consumer could be missed.
                index.complete = false;
            }
        },
        root);
    return index;
}

void countPlanFieldRefs(const binder::BoundLogicalOperator& root, std::unordered_map<std::string, int>& refs) {
    auto add = [&](const std::string& name) {
        if (!name.empty())
            ++refs[name];
    };
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    add(v->src_variable);
                    add(v->dst_variable);
                    add(v->edge_variable);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v) {
                    add(v->src_variable);
                    add(v->dst_variable);
                    add(v->edge_variable);
                    add(v->path_variable);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v)
                    add(v->variable);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (v) {
                    add(v->path_variable);
                    for (const auto& element : v->element_variables)
                        add(element);
                }
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                for (const auto& name : v.variables)
                    add(name);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
                if (v)
                    for (const auto& corr : v->correlation)
                        add(corr.left_var);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionForkOp>>) {
                if (v)
                    for (const auto& corr : v->correlation)
                        add(corr.left_var);
            }
        },
        root);
    forEachLogicalChild(root, [&](const binder::BoundLogicalOperator& child) { countPlanFieldRefs(child, refs); });
}

bool planHasWriteOperator(const binder::BoundLogicalOperator& root) {
    bool write = false;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                write = true;
            }
        },
        root);
    if (write)
        return true;
    forEachLogicalChild(root, [&](const binder::BoundLogicalOperator& child) {
        if (!write)
            write = planHasWriteOperator(child);
    });
    return write;
}

} // namespace optimizer
} // namespace eugraph
