#include "query/optimizer/requirement_collector.hpp"

#include "query/catalog/catalog.hpp"
#include "query/planner/bound_expression/bound_binary_op.hpp"
#include "query/planner/bound_expression/bound_case.hpp"
#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "query/planner/bound_expression/bound_function_call.hpp"
#include "query/planner/bound_expression/bound_label_cast.hpp"
#include "query/planner/bound_expression/bound_list.hpp"
#include "query/planner/bound_expression/bound_map.hpp"
#include "query/planner/bound_expression/bound_property_ref.hpp"
#include "query/planner/bound_expression/bound_quantifier_expr.hpp"
#include "query/planner/bound_expression/bound_slice.hpp"
#include "query/planner/bound_expression/bound_subscript.hpp"
#include "query/planner/bound_expression/bound_unary_op.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_correlated_source_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_edge_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_node_op.hpp"
#include "query/planner/logical_plan/operator/bound_delete_op.hpp"
#include "query/planner/logical_plan/operator/bound_distinct_op.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_limit_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_build_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"
#include "query/planner/logical_plan/operator/bound_remove_op.hpp"
#include "query/planner/logical_plan/operator/bound_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_semi_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_set_op.hpp"
#include "query/planner/logical_plan/operator/bound_singleton_op.hpp"
#include "query/planner/logical_plan/operator/bound_skip_op.hpp"
#include "query/planner/logical_plan/operator/bound_sort_op.hpp"
#include "query/planner/logical_plan/operator/bound_union_op.hpp"
#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

namespace eugraph {
namespace optimizer {

namespace {

// Resolve the variable name from a property-access object expression.
// Returns empty string if the object is not a simple variable reference.
std::string variableNameOf(const binder::BoundExpression& object) {
    if (auto v = std::get_if<binder::BoundVariableRef>(&object))
        return v->name;
    return {};
}

void collectFromExpr(const binder::BoundExpression& expr, VarRequirements& dst, const catalog::Catalog* catalog) {
    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            // Leaf expressions carry no property access.
            if constexpr (std::is_same_v<T, binder::BoundLiteral> || std::is_same_v<T, binder::BoundColumnRef> ||
                          std::is_same_v<T, binder::BoundParameter>) {
                return;
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                // A bare variable reference does not need materialisation
                // by itself — its requirements come from the expressions
                // that *use* the variable.
                return;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!val)
                    return;
                // BoundPropertyRef: strong-mode, binder resolved (label,prop).
                // The object may be BoundVariableRef (before ColumnResolver)
                // or BoundColumnRef (after). Handle both.
                std::string var = variableNameOf(val->object);
                // Fallback: BoundColumnRef (after ColumnResolver). Skip
                // internal names (__anon_edge_0 etc.) — those are binder
                // generated and don't correspond to user variables.
                if (var.empty())
                    if (auto c = std::get_if<binder::BoundColumnRef>(&val->object))
                        if (!c->name.empty() && c->name.find("__anon") != 0)
                            var = c->name;
                if (var.empty())
                    return;
                auto& req = dst[var];
                for (const auto& cand : val->candidates) {
                    if (cand.label_id == INVALID_LABEL_ID)
                        continue;
                    req.need_props[cand.label_id].insert(cand.prop_id);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (!val)
                    return;
                const std::string var = variableNameOf(val->object);
                if (var.empty())
                    return;
                // Weak-mode access: we know the property NAME but not the
                // label. If the catalog is available, resolve it across all
                // labels so we get concrete (label_id, prop_id) pairs —
                // this triggers the lighter PropertyExtract path.
                dst[var].need_labels = true;
                if (catalog) {
                    for (const auto& [lid, ldef] : catalog->allLabels()) {
                        for (const auto& pd : ldef.properties) {
                            if (pd.name == val->property) {
                                dst[var].need_props[lid].insert(pd.id);
                                break; // one label can only have one prop with this name
                            }
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (!val)
                    return;
                // Label cast n::Person — operator downstream will need
                // labels available to validate the cast at runtime.
                const std::string var = variableNameOf(val->object);
                if (!var.empty())
                    dst[var].need_labels = true;
                collectFromExpr(val->object, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (!val)
                    return;
                collectFromExpr(val->left, dst, catalog);
                collectFromExpr(val->right, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (!val)
                    return;
                collectFromExpr(val->operand, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (!val)
                    return;
                // labels(n) and similar require labels materialised.
                // Cheap, conservative heuristic: any function whose first
                // argument is a variable reference triggers need_labels.
                // Specific overrides (count/collect/...) can refine later.
                const std::string fname = val->func_def ? val->func_def->name : std::string{};
                if (fname == "labels" || fname == "label") {
                    if (!val->args.empty()) {
                        const std::string var = variableNameOf(val->args.front());
                        if (!var.empty())
                            dst[var].need_labels = true;
                    }
                }
                for (const auto& arg : val->args)
                    collectFromExpr(arg, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (!val)
                    return;
                for (const auto& e : val->elements)
                    collectFromExpr(e, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                if (!val)
                    return;
                for (const auto& [_, v] : val->entries)
                    collectFromExpr(v, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (!val)
                    return;
                if (val->subject)
                    collectFromExpr(*val->subject, dst, catalog);
                for (const auto& [w, t] : val->when_thens) {
                    collectFromExpr(w, dst, catalog);
                    collectFromExpr(t, dst, catalog);
                }
                if (val->else_expr)
                    collectFromExpr(*val->else_expr, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                if (!val)
                    return;
                collectFromExpr(val->list, dst, catalog);
                collectFromExpr(val->index, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                if (!val)
                    return;
                collectFromExpr(val->list, dst, catalog);
                if (val->from)
                    collectFromExpr(*val->from, dst, catalog);
                if (val->to)
                    collectFromExpr(*val->to, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                if (!val)
                    return;
                collectFromExpr(val->list_expr, dst, catalog);
                if (val->where_pred)
                    collectFromExpr(*val->where_pred, dst, catalog);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                // List comprehension introduces a new variable scope; its
                // inner variable references resolve to the new scope, not
                // to plan variables. Conservatively recurse only into the
                // outer list_expr — references to outer variables there
                // (e.g. x in [i in list | i + x]) still count.
                if (!val)
                    return;
                collectFromExpr(val->list_expr, dst, catalog);
                if (val->where_pred)
                    collectFromExpr(*val->where_pred, dst, catalog);
            }
        },
        expr);
}

} // namespace

void collectExprRequirements(const binder::BoundExpression& expr, VarRequirements& dst,
                             const catalog::Catalog* catalog) {
    collectFromExpr(expr, dst, catalog);
}

VarRequirements collectOpRequirements(const binder::BoundLogicalOperator& op, const catalog::Catalog* catalog) {
    VarRequirements dst;
    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                          std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                          std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                // Leaf sources carry no expression to walk; their label/prop
                // requirements are modelled at the operator level via
                // label_prop_ids instead — handled by the impl rule that
                // declares the scan's output_mat (see rules/impl/impl_scan).
                return;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (!val)
                    return;
                collectExprRequirements(val->predicate, dst, catalog);
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (!val)
                    return;
                for (const auto& item : val->items) {
                    collectExprRequirements(item.expr, dst, catalog);
                    // RETURN n → need entire vertex/edge/path object for serialization.
                    // Detect simple column references to graph-typed variables.
                    if (std::holds_alternative<binder::BoundColumnRef>(item.expr)) {
                        auto kind = item.result_type.kind;
                        if (kind == binder::BoundTypeKind::VERTEX || kind == binder::BoundTypeKind::VERTEX_REF) {
                            dst[item.alias].need_entire = true;
                        } else if (kind == binder::BoundTypeKind::EDGE || kind == binder::BoundTypeKind::EDGE_KEY) {
                            dst[item.alias].need_entire = true;
                        } else if (kind == binder::BoundTypeKind::PATH ||
                                   kind == binder::BoundTypeKind::PATH_TOPOLOGY) {
                            dst[item.alias].need_entire = true;
                        }
                    }
                }
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (!val)
                    return;
                for (const auto& k : val->group_keys)
                    collectExprRequirements(k, dst, catalog);
                for (const auto& agg : val->aggregates)
                    collectExprRequirements(agg.argument, dst, catalog);
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (!val)
                    return;
                for (const auto& item : val->items)
                    collectExprRequirements(item.expr, dst, catalog);
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (!val)
                    return;
                collectExprRequirements(val->list_expr, dst, catalog);
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->left, catalog));
                mergeVarRequirements(dst, collectOpRequirements(val->right, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->left, catalog));
                mergeVarRequirements(dst, collectOpRequirements(val->right, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->left, catalog));
                mergeVarRequirements(dst, collectOpRequirements(val->right, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->left, catalog));
                mergeVarRequirements(dst, collectOpRequirements(val->right, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (!val)
                    return;
                for (const auto& [_, props] : val->label_properties)
                    for (const auto& [_, e] : props)
                        collectExprRequirements(e, dst, catalog);
                for (const auto& [_, e] : val->pending_props)
                    collectExprRequirements(e, dst, catalog);
                if (val->child)
                    mergeVarRequirements(dst, collectOpRequirements(*val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (!val)
                    return;
                for (const auto& [_, e] : val->properties)
                    collectExprRequirements(e, dst, catalog);
                for (const auto& [_, e] : val->pending_props)
                    collectExprRequirements(e, dst, catalog);
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>>) {
                if (!val)
                    return;
                for (const auto& item : val->items)
                    if (item.value_expr)
                        collectExprRequirements(*item.value_expr, dst, catalog);
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>>) {
                if (!val)
                    return;
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (!val)
                    return;
                for (const auto& [_, e] : val->start_prop_filters)
                    collectExprRequirements(e, dst, catalog);
                for (const auto& [_, e] : val->start_pending_props)
                    collectExprRequirements(e, dst, catalog);
                for (const auto& [_, e] : val->edge_prop_filters)
                    collectExprRequirements(e, dst, catalog);
                for (const auto& [_, e] : val->edge_pending_props)
                    collectExprRequirements(e, dst, catalog);
                for (const auto& [_, e] : val->end_prop_filters)
                    collectExprRequirements(e, dst, catalog);
                for (const auto& [_, e] : val->end_pending_props)
                    collectExprRequirements(e, dst, catalog);
                mergeVarRequirements(dst, collectOpRequirements(val->child, catalog));
            }
        },
        op);
    return dst;
}

} // namespace optimizer
} // namespace eugraph
