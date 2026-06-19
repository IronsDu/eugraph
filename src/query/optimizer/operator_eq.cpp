#include "query/optimizer/operator_eq.hpp"

#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_type.hpp"
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
#include "query/planner/logical_plan/operator/join_type.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace eugraph {
namespace optimizer {
namespace {

bool eqBoundType(const binder::BoundType& a, const binder::BoundType& b) {
    if (a.kind != b.kind)
        return false;
    if (a.element_type && b.element_type) {
        if (!eqBoundType(*a.element_type, *b.element_type))
            return false;
    } else if (a.element_type || b.element_type) {
        return false;
    }
    if (a.map_value_type && b.map_value_type) {
        if (!eqBoundType(*a.map_value_type, *b.map_value_type))
            return false;
    } else if (a.map_value_type || b.map_value_type) {
        return false;
    }
    return true;
}

bool eqOptExpr(const std::optional<binder::BoundExpression>& a, const std::optional<binder::BoundExpression>& b) {
    if (a.has_value() != b.has_value())
        return false;
    if (!a)
        return true;
    return equalBoundExpression(*a, *b);
}

} // namespace

bool equalBoundExpression(const binder::BoundExpression& a, const binder::BoundExpression& b) {
    if (a.index() != b.index())
        return false;
    return std::visit(
        [&b](const auto& av) -> bool {
            using T = std::decay_t<decltype(av)>;
            const auto& bv = std::get<T>(b);
            if constexpr (std::is_same_v<T, binder::BoundLiteral>) {
                return av.value == bv.value && eqBoundType(av.type, bv.type);
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                return av.column_index == bv.column_index && av.name == bv.name && eqBoundType(av.type, bv.type);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                return av.name == bv.name && eqBoundType(av.type, bv.type);
            } else if constexpr (std::is_same_v<T, binder::BoundParameter>) {
                return av.name == bv.name && eqBoundType(av.expected_type, bv.expected_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->op != bv->op)
                    return false;
                if (!equalBoundExpression(av->left, bv->left))
                    return false;
                if (!equalBoundExpression(av->right, bv->right))
                    return false;
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->op != bv->op)
                    return false;
                if (!equalBoundExpression(av->operand, bv->operand))
                    return false;
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->func_def != bv->func_def)
                    return false;
                if (av->args.size() != bv->args.size())
                    return false;
                for (size_t i = 0; i < av->args.size(); ++i)
                    if (!equalBoundExpression(av->args[i], bv->args[i]))
                        return false;
                if (!eqBoundType(av->return_type, bv->return_type))
                    return false;
                return av->distinct == bv->distinct;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->elements.size() != bv->elements.size())
                    return false;
                for (size_t i = 0; i < av->elements.size(); ++i)
                    if (!equalBoundExpression(av->elements[i], bv->elements[i]))
                        return false;
                if (!eqBoundType(av->element_type, bv->element_type))
                    return false;
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->entries.size() != bv->entries.size())
                    return false;
                for (size_t i = 0; i < av->entries.size(); ++i) {
                    if (av->entries[i].first != bv->entries[i].first)
                        return false;
                    if (!equalBoundExpression(av->entries[i].second, bv->entries[i].second))
                        return false;
                }
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (!eqOptExpr(av->subject, bv->subject))
                    return false;
                if (av->when_thens.size() != bv->when_thens.size())
                    return false;
                for (size_t i = 0; i < av->when_thens.size(); ++i) {
                    if (!equalBoundExpression(av->when_thens[i].first, bv->when_thens[i].first))
                        return false;
                    if (!equalBoundExpression(av->when_thens[i].second, bv->when_thens[i].second))
                        return false;
                }
                if (!eqOptExpr(av->else_expr, bv->else_expr))
                    return false;
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (!equalBoundExpression(av->list, bv->list))
                    return false;
                if (!equalBoundExpression(av->index, bv->index))
                    return false;
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (!equalBoundExpression(av->list, bv->list))
                    return false;
                if (!eqOptExpr(av->from, bv->from))
                    return false;
                if (!eqOptExpr(av->to, bv->to))
                    return false;
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (!equalBoundExpression(av->object, bv->object))
                    return false;
                if (av->property != bv->property)
                    return false;
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->property_name != bv->property_name)
                    return false;
                if (!equalBoundExpression(av->object, bv->object))
                    return false;
                if (av->candidates.size() != bv->candidates.size())
                    return false;
                for (size_t i = 0; i < av->candidates.size(); ++i) {
                    if (av->candidates[i].label_id != bv->candidates[i].label_id)
                        return false;
                    if (av->candidates[i].prop_id != bv->candidates[i].prop_id)
                        return false;
                    if (!eqBoundType(av->candidates[i].type, bv->candidates[i].type))
                        return false;
                }
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (!equalBoundExpression(av->object, bv->object))
                    return false;
                if (av->label_id != bv->label_id)
                    return false;
                return eqBoundType(av->result_type, bv->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->variable != bv->variable)
                    return false;
                if (av->loop_column_index != bv->loop_column_index)
                    return false;
                if (!equalBoundExpression(av->list_expr, bv->list_expr))
                    return false;
                if (!eqOptExpr(av->where_pred, bv->where_pred))
                    return false;
                if (!eqBoundType(av->result_type, bv->result_type))
                    return false;
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>)
                    return equalBoundExpression(av->projection, bv->projection);
                return true;
            }
            return false;
        },
        a);
}

bool equalBoundLogicalOperator(const binder::BoundLogicalOperator& a, const binder::BoundLogicalOperator& b) {
    if (a.index() != b.index())
        return false;
    return std::visit(
        [&b](const auto& av) -> bool {
            using T = std::decay_t<decltype(av)>;
            const auto& bv = std::get<T>(b);
            if constexpr (std::is_same_v<T, binder::BoundSingletonOp>) {
                return true;
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                if (av.variables != bv.variables)
                    return false;
                if (av.types.size() != bv.types.size())
                    return false;
                for (size_t i = 0; i < av.types.size(); ++i)
                    if (!eqBoundType(av.types[i], bv.types[i]))
                        return false;
                return av.column_indices == bv.column_indices;
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                if (av.variable != bv.variable)
                    return false;
                if (av.column_index != bv.column_index)
                    return false;
                return av.label_prop_ids == bv.label_prop_ids;
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                if (av.variable != bv.variable)
                    return false;
                if (av.column_index != bv.column_index)
                    return false;
                if (av.label_ids != bv.label_ids)
                    return false;
                if (av.label_names != bv.label_names)
                    return false;
                return av.label_prop_ids == bv.label_prop_ids;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                return equalBoundExpression(av->predicate, bv->predicate);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->items.size() != bv->items.size())
                    return false;
                for (size_t i = 0; i < av->items.size(); ++i) {
                    if (!equalBoundExpression(av->items[i].expr, bv->items[i].expr))
                        return false;
                    if (av->items[i].alias != bv->items[i].alias)
                        return false;
                    if (!eqBoundType(av->items[i].result_type, bv->items[i].result_type))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->group_keys.size() != bv->group_keys.size())
                    return false;
                for (size_t i = 0; i < av->group_keys.size(); ++i)
                    if (!equalBoundExpression(av->group_keys[i], bv->group_keys[i]))
                        return false;
                if (av->aggregates.size() != bv->aggregates.size())
                    return false;
                for (size_t i = 0; i < av->aggregates.size(); ++i) {
                    const auto& x = av->aggregates[i];
                    const auto& y = bv->aggregates[i];
                    if (x.function_name != y.function_name)
                        return false;
                    if (!equalBoundExpression(x.argument, y.argument))
                        return false;
                    if (x.alias != y.alias)
                        return false;
                    if (!eqBoundType(x.result_type, y.result_type))
                        return false;
                    if (x.func_def != y.func_def)
                        return false;
                    if (x.distinct != y.distinct)
                        return false;
                    if (x.is_internal != y.is_internal)
                        return false;
                }
                return av->output_names == bv->output_names;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->items.size() != bv->items.size())
                    return false;
                for (size_t i = 0; i < av->items.size(); ++i) {
                    if (!equalBoundExpression(av->items[i].expr, bv->items[i].expr))
                        return false;
                    if (av->items[i].direction != bv->items[i].direction)
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                return av->count == bv->count;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                return av->count == bv->count;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                return static_cast<bool>(av) == static_cast<bool>(bv);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->src_variable != bv->src_variable)
                    return false;
                if (av->src_column_index != bv->src_column_index)
                    return false;
                if (av->edge_variable != bv->edge_variable)
                    return false;
                if (av->edge_column_index != bv->edge_column_index)
                    return false;
                if (av->dst_variable != bv->dst_variable)
                    return false;
                if (av->dst_column_index != bv->dst_column_index)
                    return false;
                if (av->edge_label_ids != bv->edge_label_ids)
                    return false;
                if (av->direction != bv->direction)
                    return false;
                if (av->edge_prop_ids != bv->edge_prop_ids)
                    return false;
                return av->dst_label_prop_ids == bv->dst_label_prop_ids;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->src_variable != bv->src_variable)
                    return false;
                if (av->src_column_index != bv->src_column_index)
                    return false;
                if (av->dst_variable != bv->dst_variable)
                    return false;
                if (av->dst_column_index != bv->dst_column_index)
                    return false;
                if (av->edge_label_ids != bv->edge_label_ids)
                    return false;
                if (av->direction != bv->direction)
                    return false;
                if (av->min_hops != bv->min_hops)
                    return false;
                if (av->max_hops != bv->max_hops)
                    return false;
                if (av->dst_label_prop_ids != bv->dst_label_prop_ids)
                    return false;
                if (av->path_variable != bv->path_variable)
                    return false;
                if (av->path_column_index != bv->path_column_index)
                    return false;
                if (av->path_handled_by_varlen != bv->path_handled_by_varlen)
                    return false;
                if (av->edge_variable != bv->edge_variable)
                    return false;
                if (av->edge_column_index != bv->edge_column_index)
                    return false;
                return av->edge_prop_filters == bv->edge_prop_filters;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->path_variable != bv->path_variable)
                    return false;
                if (av->path_column_index != bv->path_column_index)
                    return false;
                return av->element_variables == bv->element_variables;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (!equalBoundExpression(av->list_expr, bv->list_expr))
                    return false;
                if (av->variable != bv->variable)
                    return false;
                return av->variable_column_index == bv->variable_column_index;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->variable != bv->variable)
                    return false;
                if (av->label_ids != bv->label_ids)
                    return false;
                if (av->label_properties.size() != bv->label_properties.size())
                    return false;
                for (size_t i = 0; i < av->label_properties.size(); ++i) {
                    if (av->label_properties[i].first != bv->label_properties[i].first)
                        return false;
                    const auto& pl = av->label_properties[i].second;
                    const auto& pr = bv->label_properties[i].second;
                    if (pl.size() != pr.size())
                        return false;
                    for (size_t j = 0; j < pl.size(); ++j) {
                        if (pl[j].first != pr[j].first)
                            return false;
                        if (!equalBoundExpression(pl[j].second, pr[j].second))
                            return false;
                    }
                }
                if (av->pending_props.size() != bv->pending_props.size())
                    return false;
                for (size_t i = 0; i < av->pending_props.size(); ++i) {
                    if (av->pending_props[i].first != bv->pending_props[i].first)
                        return false;
                    if (!equalBoundExpression(av->pending_props[i].second, bv->pending_props[i].second))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->variable != bv->variable)
                    return false;
                if (av->label_id != bv->label_id)
                    return false;
                if (av->label_name != bv->label_name)
                    return false;
                if (av->src_variable != bv->src_variable)
                    return false;
                if (av->dst_variable != bv->dst_variable)
                    return false;
                if (av->properties.size() != bv->properties.size())
                    return false;
                for (size_t i = 0; i < av->properties.size(); ++i) {
                    if (av->properties[i].first != bv->properties[i].first)
                        return false;
                    if (!equalBoundExpression(av->properties[i].second, bv->properties[i].second))
                        return false;
                }
                if (av->pending_props.size() != bv->pending_props.size())
                    return false;
                for (size_t i = 0; i < av->pending_props.size(); ++i) {
                    if (av->pending_props[i].first != bv->pending_props[i].first)
                        return false;
                    if (!equalBoundExpression(av->pending_props[i].second, bv->pending_props[i].second))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->items.size() != bv->items.size())
                    return false;
                for (size_t i = 0; i < av->items.size(); ++i) {
                    const auto& x = av->items[i];
                    const auto& y = bv->items[i];
                    if (x.kind != y.kind)
                        return false;
                    if (x.target_variable != y.target_variable)
                        return false;
                    if (x.prop_name != y.prop_name)
                        return false;
                    if (x.prop_id != y.prop_id)
                        return false;
                    if (!eqOptExpr(x.value_expr, y.value_expr))
                        return false;
                    if (x.label_id != y.label_id)
                        return false;
                    if (x.strong_mode != y.strong_mode)
                        return false;
                    if (x.is_add_assign != y.is_add_assign)
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->items.size() != bv->items.size())
                    return false;
                for (size_t i = 0; i < av->items.size(); ++i) {
                    const auto& x = av->items[i];
                    const auto& y = bv->items[i];
                    if (x.kind != y.kind)
                        return false;
                    if (x.target_variable != y.target_variable)
                        return false;
                    if (x.prop_name != y.prop_name)
                        return false;
                    if (x.prop_id != y.prop_id)
                        return false;
                    if (x.label_id != y.label_id)
                        return false;
                    if (x.strong_mode != y.strong_mode)
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->detach != bv->detach)
                    return false;
                if (av->targets.size() != bv->targets.size())
                    return false;
                for (size_t i = 0; i < av->targets.size(); ++i) {
                    if (av->targets[i].kind != bv->targets[i].kind)
                        return false;
                    if (av->targets[i].variable_name != bv->targets[i].variable_name)
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                return av->join_type == bv->join_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                return av->correlation == bv->correlation;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->correlation != bv->correlation)
                    return false;
                return av->anti == bv->anti;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                return av->all == bv->all;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (!av || !bv)
                    return !av && !bv;
                if (av->path_variable != bv->path_variable)
                    return false;
                if (av->start_var != bv->start_var)
                    return false;
                if (av->start_pre_bound != bv->start_pre_bound)
                    return false;
                if (av->start_labels != bv->start_labels)
                    return false;
                if (av->start_prop_filters.size() != bv->start_prop_filters.size())
                    return false;
                for (size_t i = 0; i < av->start_prop_filters.size(); ++i) {
                    if (av->start_prop_filters[i].first != bv->start_prop_filters[i].first)
                        return false;
                    if (!equalBoundExpression(av->start_prop_filters[i].second, bv->start_prop_filters[i].second))
                        return false;
                }
                if (av->start_pending_props.size() != bv->start_pending_props.size())
                    return false;
                for (size_t i = 0; i < av->start_pending_props.size(); ++i) {
                    if (av->start_pending_props[i].first != bv->start_pending_props[i].first)
                        return false;
                    if (!equalBoundExpression(av->start_pending_props[i].second, bv->start_pending_props[i].second))
                        return false;
                }
                if (av->has_relationship != bv->has_relationship)
                    return false;
                if (av->edge_var != bv->edge_var)
                    return false;
                if (av->edge_label_id != bv->edge_label_id)
                    return false;
                if (av->edge_label_name != bv->edge_label_name)
                    return false;
                if (av->direction != bv->direction)
                    return false;
                if (av->edge_prop_filters.size() != bv->edge_prop_filters.size())
                    return false;
                for (size_t i = 0; i < av->edge_prop_filters.size(); ++i) {
                    if (av->edge_prop_filters[i].first != bv->edge_prop_filters[i].first)
                        return false;
                    if (!equalBoundExpression(av->edge_prop_filters[i].second, bv->edge_prop_filters[i].second))
                        return false;
                }
                if (av->edge_pending_props.size() != bv->edge_pending_props.size())
                    return false;
                for (size_t i = 0; i < av->edge_pending_props.size(); ++i) {
                    if (av->edge_pending_props[i].first != bv->edge_pending_props[i].first)
                        return false;
                    if (!equalBoundExpression(av->edge_pending_props[i].second, bv->edge_pending_props[i].second))
                        return false;
                }
                if (av->end_var != bv->end_var)
                    return false;
                if (av->end_pre_bound != bv->end_pre_bound)
                    return false;
                if (av->end_labels != bv->end_labels)
                    return false;
                if (av->end_prop_filters.size() != bv->end_prop_filters.size())
                    return false;
                for (size_t i = 0; i < av->end_prop_filters.size(); ++i) {
                    if (av->end_prop_filters[i].first != bv->end_prop_filters[i].first)
                        return false;
                    if (!equalBoundExpression(av->end_prop_filters[i].second, bv->end_prop_filters[i].second))
                        return false;
                }
                if (av->end_pending_props.size() != bv->end_pending_props.size())
                    return false;
                for (size_t i = 0; i < av->end_pending_props.size(); ++i) {
                    if (av->end_pending_props[i].first != bv->end_pending_props[i].first)
                        return false;
                    if (!equalBoundExpression(av->end_pending_props[i].second, bv->end_pending_props[i].second))
                        return false;
                }
                return true;
            }
            return false;
        },
        a);
}

} // namespace optimizer
} // namespace eugraph
