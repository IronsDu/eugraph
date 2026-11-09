#include "query/optimizer/logical_model/logical_op_model.hpp"

#include "query/optimizer/framework/memo.hpp"
#include "query/optimizer/framework/opt_rule.hpp"
#include "query/optimizer/logical_model/operator_eq.hpp"
#include "query/optimizer/logical_model/operator_hash.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_call_op.hpp"
#include "query/planner/logical_plan/operator/bound_correlated_source_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_edge_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_node_op.hpp"
#include "query/planner/logical_plan/operator/bound_delete_op.hpp"
#include "query/planner/logical_plan/operator/bound_distinct_op.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_foreach_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_limit_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_build_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_count_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_apply_op.hpp"
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

#include <stdexcept>
#include <type_traits>

namespace eugraph {
namespace optimizer {
namespace {

template <typename> inline constexpr bool always_false = false;

binder::BoundSetOp::SetItem cloneSetItem(const binder::BoundSetOp::SetItem& item) {
    binder::BoundSetOp::SetItem ci;
    ci.kind = item.kind;
    ci.target_variable = item.target_variable;
    ci.prop_name = item.prop_name;
    ci.label = item.label;
    ci.prop_id = item.prop_id;
    ci.value_expr = item.value_expr.has_value()
                        ? std::optional<binder::BoundExpression>(cloneBoundExpression(*item.value_expr))
                        : std::nullopt;
    ci.label_id = item.label_id;
    ci.strong_mode = item.strong_mode;
    ci.is_add_assign = item.is_add_assign;
    return ci;
}

template <typename T> binder::BoundLogicalOperator takeChildAt(binder::BoundLogicalOperator& op, int index) {
    auto& val = std::get<T>(op);
    if constexpr (std::is_same_v<T, binder::BoundSingletonOp> || std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                  std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp> ||
                  std::is_same_v<T, std::unique_ptr<binder::BoundCallOp>>) {
        return binder::BoundScanOp{};
    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
        if (!val || !val->child.has_value())
            return binder::BoundScanOp{};
        auto child = std::move(*val->child);
        val->child = std::nullopt;
        return child;
    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>>) {
        if (!val)
            return binder::BoundScanOp{};
        if (index == 0) {
            auto child = std::move(val->child);
            val->child = binder::BoundScanOp{};
            return child;
        }
        auto body = std::move(val->body);
        val->body = binder::BoundScanOp{};
        return body;
    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
        if (!val)
            return binder::BoundScanOp{};
        if (index == 0) {
            auto child = std::move(val->left);
            val->left = binder::BoundScanOp{};
            return child;
        }
        auto child = std::move(val->right);
        val->right = binder::BoundScanOp{};
        return child;
    } else {
        if (!val)
            return binder::BoundScanOp{};
        auto child = std::move(val->child);
        val->child = binder::BoundScanOp{};
        return child;
    }
}

template <typename T> void setChildAt(binder::BoundLogicalOperator& op, int index, binder::BoundLogicalOperator child) {
    auto& val = std::get<T>(op);
    if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>>) {
        if (!val)
            return;
        if (index == 0)
            val->child = std::move(child);
        else
            val->body = std::move(child);
    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
        if (!val)
            return;
        if (index == 0)
            val->left = std::move(child);
        else
            val->right = std::move(child);
    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
        if (!val)
            return;
        val->child = std::move(child);
    } else if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                         std::is_same_v<T, binder::BoundCorrelatedSourceOp> || std::is_same_v<T, binder::BoundScanOp> ||
                         std::is_same_v<T, binder::BoundLabelScanOp> ||
                         std::is_same_v<T, std::unique_ptr<binder::BoundCallOp>>) {
        (void)index;
        (void)child;
    } else {
        if (!val || !val)
            return;
        val->child = std::move(child);
    }
}

class VariantLogicalOpModel final : public LogicalOpModel {
public:
    OptNodeType type(const binder::BoundLogicalOperator& op) const override {
        return nodeTypeFromVariantIndex(op.index());
    }

    binder::BoundLogicalOperator clone(const binder::BoundLogicalOperator& op) const override {
        return std::visit(
            [](const auto& val) -> binder::BoundLogicalOperator {
                using T = std::decay_t<decltype(val)>;
                // Value types (leaf operators): trivially copyable
                if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                              std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                              std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                    return val;
                }
                // All unique_ptr types below — child/left/right fields intentionally skipped

                // Operators with no BoundExpression — copy scalar fields only
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                    if (!val)
                        return binder::BoundScanOp{};
                    auto c = std::make_unique<binder::BoundExpandOp>();
                    c->src_variable = val->src_variable;
                    c->src_column_index = val->src_column_index;
                    c->edge_variable = val->edge_variable;
                    c->edge_column_index = val->edge_column_index;
                    c->dst_variable = val->dst_variable;
                    c->dst_column_index = val->dst_column_index;
                    c->edge_label_ids = val->edge_label_ids;
                    c->all_edge_labels = val->all_edge_labels;
                    c->direction = val->direction;
                    c->edge_prop_ids = val->edge_prop_ids;
                    c->dst_label_prop_ids = val->dst_label_prop_ids;
                    c->dst_label_ids = val->dst_label_ids;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                    auto c = std::make_unique<binder::BoundSkipOp>();
                    c->constant = val->constant;
                    if (val->expr)
                        c->expr = cloneBoundExpression(*val->expr);
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                    auto c = std::make_unique<binder::BoundLimitOp>();
                    c->constant = val->constant;
                    if (val->expr)
                        c->expr = cloneBoundExpression(*val->expr);
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                    return std::make_unique<binder::BoundDistinctOp>();
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>>) {
                    auto c = std::make_unique<binder::BoundRemoveOp>();
                    c->items = val->items;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>>) {
                    auto c = std::make_unique<binder::BoundDeleteOp>();
                    c->detach = val->detach;
                    for (const auto& t : val->targets) {
                        binder::BoundDeleteOp::DeleteTarget ct;
                        ct.kind = t.kind;
                        ct.variable_name = t.variable_name;
                        if (t.expr)
                            ct.expr = cloneBoundExpression(*t.expr);
                        c->targets.push_back(std::move(ct));
                    }
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathCountOp>>) {
                    auto c = std::make_unique<binder::BoundPathCountOp>();
                    c->source_variable = val->source_variable;
                    c->dst_variable = val->dst_variable;
                    c->previous_edge_variables = val->previous_edge_variables;
                    c->edge_label_ids = val->edge_label_ids;
                    c->output_name = val->output_name;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                    auto c = std::make_unique<binder::BoundPathBuildOp>();
                    c->path_variable = val->path_variable;
                    c->path_column_index = val->path_column_index;
                    c->element_variables = val->element_variables;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                    auto c = std::make_unique<binder::BoundVarLenExpandOp>();
                    c->src_variable = val->src_variable;
                    c->src_column_index = val->src_column_index;
                    c->dst_variable = val->dst_variable;
                    c->dst_column_index = val->dst_column_index;
                    c->edge_label_ids = val->edge_label_ids;
                    c->all_edge_labels = val->all_edge_labels;
                    c->direction = val->direction;
                    c->min_hops = val->min_hops;
                    c->max_hops = val->max_hops;
                    c->dst_label_prop_ids = val->dst_label_prop_ids;
                    c->dst_label_ids = val->dst_label_ids;
                    c->dst_label_missing = val->dst_label_missing;
                    c->path_variable = val->path_variable;
                    c->path_column_index = val->path_column_index;
                    c->path_slot_id = val->path_slot_id;
                    c->path_handled_by_varlen = val->path_handled_by_varlen;
                    c->shortest_path = val->shortest_path;
                    c->all_shortest_paths = val->all_shortest_paths;
                    c->batch_shortest_path = val->batch_shortest_path;
                    c->edge_variable = val->edge_variable;
                    c->edge_column_index = val->edge_column_index;
                    c->edge_slot_id = val->edge_slot_id;
                    c->planner_edge_slot_id = val->planner_edge_slot_id;
                    c->bound_edge_list = val->bound_edge_list;
                    c->bound_edge_list_col_index = val->bound_edge_list_col_index;
                    c->prev_edge_var = val->prev_edge_var;
                    c->prev_edge_col_index = val->prev_edge_col_index;
                    c->edge_prop_filters = val->edge_prop_filters;
                    return c;
                }
                // Binary operators — copy scalar fields, skip left/right
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                    auto c = std::make_unique<binder::BoundBinaryJoinOp>();
                    c->join_type = val->join_type;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                    auto c = std::make_unique<binder::BoundLeftJoinOp>();
                    c->correlation = val->correlation;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                    auto c = std::make_unique<binder::BoundSemiJoinOp>();
                    c->correlation = val->correlation;
                    c->anti = val->anti;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                    auto c = std::make_unique<binder::BoundUnionOp>();
                    c->all = val->all;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
                    auto c = std::make_unique<binder::BoundPatternComprehensionApplyOp>();
                    c->correlation = val->correlation;
                    c->batch_enabled = val->batch_enabled;
                    c->batch_slot_id = val->batch_slot_id;
                    c->batch_var = val->batch_var;
                    for (const auto& out : val->outputs) {
                        binder::BoundPatternComprehensionApplyOp::Output co;
                        co.slot_id = out.slot_id;
                        co.name = out.name;
                        co.element_type = out.element_type;
                        c->outputs.push_back(std::move(co));
                    }
                    return c;
                }
                // Operators with BoundExpression — clone expressions, skip child
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                    auto c = std::make_unique<binder::BoundFilterOp>();
                    c->predicate = cloneBoundExpression(val->predicate);
                    c->uniqueness_scope = val->uniqueness_scope;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                    auto c = std::make_unique<binder::BoundSortOp>();
                    for (const auto& item : val->items) {
                        typename binder::BoundSortOp::SortItem ci;
                        ci.expr = cloneBoundExpression(item.expr);
                        ci.direction = item.direction;
                        c->items.push_back(std::move(ci));
                    }
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                    auto c = std::make_unique<binder::BoundProjectOp>();
                    for (const auto& item : val->items) {
                        typename binder::BoundProjectOp::ProjectItem ci;
                        ci.expr = cloneBoundExpression(item.expr);
                        ci.alias = item.alias;
                        ci.result_type = item.result_type;
                        ci.output_slot = item.output_slot;
                        c->items.push_back(std::move(ci));
                    }
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                    auto c = std::make_unique<binder::BoundAggregateOp>();
                    for (const auto& gk : val->group_keys)
                        c->group_keys.push_back(cloneBoundExpression(gk));
                    for (const auto& agg : val->aggregates) {
                        typename binder::BoundAggregateOp::AggregateItem ca;
                        ca.function_name = agg.function_name;
                        for (const auto& arg : agg.arguments)
                            ca.arguments.push_back(cloneBoundExpression(arg));
                        ca.alias = agg.alias;
                        ca.result_type = agg.result_type;
                        ca.func_def = agg.func_def;
                        ca.distinct = agg.distinct;
                        ca.is_internal = agg.is_internal;
                        ca.keeps_nulls = agg.keeps_nulls;
                        c->aggregates.push_back(std::move(ca));
                    }
                    c->output_names = val->output_names;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                    auto c = std::make_unique<binder::BoundCreateNodeOp>();
                    c->variable = val->variable;
                    c->label_ids = val->label_ids;
                    c->label_names = val->label_names;
                    for (const auto& [label_id, props] : val->label_properties) {
                        std::vector<std::pair<uint16_t, binder::BoundExpression>> cloned_props;
                        for (const auto& [prop_id, expr] : props)
                            cloned_props.emplace_back(prop_id, cloneBoundExpression(expr));
                        c->label_properties.emplace_back(label_id, std::move(cloned_props));
                    }
                    for (const auto& [name, expr] : val->pending_props)
                        c->pending_props.emplace_back(name, cloneBoundExpression(expr));
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                    auto c = std::make_unique<binder::BoundCreateEdgeOp>();
                    c->variable = val->variable;
                    c->label_id = val->label_id;
                    c->label_name = val->label_name;
                    c->src_variable = val->src_variable;
                    c->dst_variable = val->dst_variable;
                    for (const auto& [prop_id, expr] : val->properties)
                        c->properties.emplace_back(prop_id, cloneBoundExpression(expr));
                    for (const auto& [name, expr] : val->pending_props)
                        c->pending_props.emplace_back(name, cloneBoundExpression(expr));
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>>) {
                    auto c = std::make_unique<binder::BoundSetOp>();
                    for (const auto& item : val->items)
                        c->items.push_back(cloneSetItem(item));
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                    auto c = std::make_unique<binder::BoundMergeOp>();
                    c->path_variable = val->path_variable;
                    c->start_var = val->start_var;
                    c->start_pre_bound = val->start_pre_bound;
                    c->start_labels = val->start_labels;
                    for (const auto& [prop_id, expr] : val->start_prop_filters)
                        c->start_prop_filters.emplace_back(prop_id, cloneBoundExpression(expr));
                    for (const auto& [name, expr] : val->start_pending_props)
                        c->start_pending_props.emplace_back(name, cloneBoundExpression(expr));
                    c->has_relationship = val->has_relationship;
                    c->edge_var = val->edge_var;
                    c->edge_label_id = val->edge_label_id;
                    c->edge_label_name = val->edge_label_name;
                    c->direction = val->direction;
                    for (const auto& [prop_id, expr] : val->edge_prop_filters)
                        c->edge_prop_filters.emplace_back(prop_id, cloneBoundExpression(expr));
                    for (const auto& [name, expr] : val->edge_pending_props)
                        c->edge_pending_props.emplace_back(name, cloneBoundExpression(expr));
                    c->end_var = val->end_var;
                    c->end_pre_bound = val->end_pre_bound;
                    c->end_labels = val->end_labels;
                    for (const auto& [prop_id, expr] : val->end_prop_filters)
                        c->end_prop_filters.emplace_back(prop_id, cloneBoundExpression(expr));
                    for (const auto& [name, expr] : val->end_pending_props)
                        c->end_pending_props.emplace_back(name, cloneBoundExpression(expr));
                    for (const auto& item : val->on_create_items)
                        c->on_create_items.push_back(cloneSetItem(item));
                    for (const auto& item : val->on_match_items)
                        c->on_match_items.push_back(cloneSetItem(item));
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                    auto c = std::make_unique<binder::BoundUnwindOp>();
                    c->list_expr = cloneBoundExpression(val->list_expr);
                    c->variable = val->variable;
                    c->variable_column_index = val->variable_column_index;
                    c->variable_slot_id = val->variable_slot_id;
                    return c;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>>) {
                    auto c = std::make_unique<binder::BoundForeachOp>();
                    c->list_expr = cloneBoundExpression(val->list_expr);
                    c->variable = val->variable;
                    c->element_type = binder::BoundType::clone(val->element_type);
                    c->element_column = val->element_column;
                    c->input_columns = val->input_columns;
                    return c; // child + body are restored from the memo's child groups
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCallOp>>) {
                    auto c = std::make_unique<binder::BoundCallOp>();
                    c->procedure_name = val->procedure_name;
                    for (const auto& arg : val->arguments)
                        c->arguments.push_back(cloneBoundExpression(arg));
                    c->yield_items = val->yield_items;
                    c->output_names = val->output_names;
                    c->output_types = val->output_types;
                    return c;
                } else {
                    // Fail closed: a newly appended operator must get an explicit
                    // clone branch.  Never silently substitute BoundScanOp{}.
                    static_assert(always_false<T>, "unhandled BoundLogicalOperator in cloneBoundLogicalOperator");
                }
            },
            op);
    }

    bool equals(const binder::BoundLogicalOperator& a, const binder::BoundLogicalOperator& b) const override {
        return equalBoundLogicalOperator(a, b);
    }
    uint64_t hash(const binder::BoundLogicalOperator& op) const override {
        return hashBoundLogicalOperator(op);
    }

    int childCount(const binder::BoundLogicalOperator& op) const override {
        return std::visit(
            [](const auto& val) -> int {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                              std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                              std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp> ||
                              std::is_same_v<T, std::unique_ptr<binder::BoundCallOp>>) {
                    return 0;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                    return 2;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                    return (val && val->child.has_value()) ? 1 : 0;
                } else {
                    return val ? 1 : 0;
                }
            },
            op);
    }

    binder::BoundLogicalOperator takeChild(binder::BoundLogicalOperator& op, int index) const override {
        if (childCount(op) == 0)
            return binder::BoundScanOp{};
        return std::visit(
            [&op, index](auto& val) -> binder::BoundLogicalOperator {
                using T = std::decay_t<decltype(val)>;
                return takeChildAt<T>(op, index);
            },
            op);
    }

    void setChild(binder::BoundLogicalOperator& op, int index, binder::BoundLogicalOperator child) const override {
        std::visit(
            [&op, index, &child](auto& val) {
                using T = std::decay_t<decltype(val)>;
                setChildAt<T>(op, index, std::move(child));
            },
            op);
    }
};

} // namespace

const LogicalOpModel& logicalOpModel() {
    static const VariantLogicalOpModel model;
    return model;
}

} // namespace optimizer
} // namespace eugraph
