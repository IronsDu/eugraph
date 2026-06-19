#include "query/optimizer/operator_hash.hpp"

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

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace eugraph {
namespace optimizer {
namespace {

uint64_t combine(uint64_t seed, uint64_t v) {
    // FNV-1a
    return (seed ^ v) * 1099511628211ULL;
}

uint64_t hashBytes(uint64_t seed, const std::string& s) {
    for (unsigned char c : s)
        seed = combine(seed, c);
    return seed;
}

uint64_t hashBytes(uint64_t seed, const char* data, size_t n) {
    for (size_t i = 0; i < n; ++i)
        seed = combine(seed, static_cast<unsigned char>(data[i]));
    return seed;
}

template <typename T> uint64_t hashScalar(uint64_t seed, const T& v) {
    return combine(seed, static_cast<uint64_t>(std::hash<T>{}(v)));
}

uint64_t hashBoundType(uint64_t seed, const binder::BoundType& t) {
    seed = combine(seed, static_cast<uint64_t>(t.kind));
    if (t.element_type)
        seed = hashBoundType(seed, *t.element_type);
    if (t.map_value_type)
        seed = hashBoundType(seed, *t.map_value_type);
    return seed;
}

// PropertyValue (graph_types.hpp) is a variant of primitives + vectors.
uint64_t hashPropertyValue(uint64_t seed, const PropertyValue& v) {
    seed = combine(seed, static_cast<uint64_t>(v.index()));
    std::visit(
        [&seed](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return;
            } else if constexpr (std::is_same_v<T, bool>) {
                seed = combine(seed, x ? 1u : 0u);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                seed = combine(seed, static_cast<uint64_t>(x));
            } else if constexpr (std::is_same_v<T, double>) {
                seed = hashScalar(seed, x);
            } else if constexpr (std::is_same_v<T, std::string>) {
                seed = hashBytes(seed, x);
            } else if constexpr (std::is_same_v<T, DateTimeValue> || std::is_same_v<T, TimeValue> ||
                                 std::is_same_v<T, DurationValue>) {
                seed = hashBytes(seed, reinterpret_cast<const char*>(&x), sizeof(x));
            } else {
                // vector<T>
                for (const auto& e : x) {
                    if constexpr (std::is_same_v<typename T::value_type, int64_t>)
                        seed = combine(seed, static_cast<uint64_t>(e));
                    else if constexpr (std::is_same_v<typename T::value_type, double>)
                        seed = hashScalar(seed, e);
                    else if constexpr (std::is_same_v<typename T::value_type, std::string>)
                        seed = hashBytes(seed, e);
                    else
                        seed = hashBytes(seed, reinterpret_cast<const char*>(&e), sizeof(e));
                }
            }
        },
        v);
    return seed;
}

uint64_t hashValue(uint64_t seed, const Value& v) {
    seed = combine(seed, static_cast<uint64_t>(v.index()));
    std::visit(
        [&seed](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return;
            } else if constexpr (std::is_same_v<T, bool>) {
                seed = combine(seed, x ? 1u : 0u);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                seed = combine(seed, static_cast<uint64_t>(x));
            } else if constexpr (std::is_same_v<T, double>) {
                seed = hashScalar(seed, x);
            } else if constexpr (std::is_same_v<T, std::string>) {
                seed = hashBytes(seed, x);
            } else if constexpr (std::is_same_v<T, VertexValue>) {
                seed = combine(seed, x.id);
            } else if constexpr (std::is_same_v<T, EdgeValue>) {
                seed = combine(seed, x.id);
                seed = combine(seed, x.src_id);
                seed = combine(seed, x.dst_id);
                seed = combine(seed, static_cast<uint64_t>(x.label_id));
            } else {
                // PathValue / ListValue / MapValue / temporal
                // Use the existing operator==. We don't deep-hash these — they
                // rarely appear in BoundExpression at plan time, and full
                // deep hashing is out of scope for the optimizer's dedup.
                seed = combine(seed, 0);
            }
        },
        v);
    return seed;
}

uint64_t hashOptExpr(uint64_t seed, const std::optional<binder::BoundExpression>& e) {
    seed = combine(seed, e.has_value() ? 1u : 0u);
    if (e)
        seed = combine(seed, hashBoundExpression(*e));
    return seed;
}

} // namespace

uint64_t hashBoundExpression(const binder::BoundExpression& expr) {
    uint64_t seed = 14695981039346656037ULL;
    seed = combine(seed, expr.index());
    std::visit(
        [&seed](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundLiteral>) {
                seed = hashValue(seed, val.value);
                seed = hashBoundType(seed, val.type);
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                seed = combine(seed, val.column_index);
                seed = hashBytes(seed, val.name);
                seed = hashBoundType(seed, val.type);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                seed = hashBytes(seed, val.name);
                seed = hashBoundType(seed, val.type);
            } else if constexpr (std::is_same_v<T, binder::BoundParameter>) {
                seed = hashBytes(seed, val.name);
                seed = hashBoundType(seed, val.expected_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (!val)
                    return;
                seed = combine(seed, static_cast<uint64_t>(val->op));
                seed = combine(seed, hashBoundExpression(val->left));
                seed = combine(seed, hashBoundExpression(val->right));
                seed = hashBoundType(seed, val->result_type);
                // batch_fn is determined by op — skip
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (!val)
                    return;
                seed = combine(seed, static_cast<uint64_t>(val->op));
                seed = combine(seed, hashBoundExpression(val->operand));
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (!val)
                    return;
                // func_def: registry singleton — pointer identity is stable
                seed = hashScalar(seed, reinterpret_cast<uintptr_t>(val->func_def));
                for (const auto& a : val->args)
                    seed = combine(seed, hashBoundExpression(a));
                seed = hashBoundType(seed, val->return_type);
                seed = combine(seed, val->distinct ? 1u : 0u);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (!val)
                    return;
                for (const auto& e : val->elements)
                    seed = combine(seed, hashBoundExpression(e));
                seed = hashBoundType(seed, val->element_type);
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                if (!val)
                    return;
                for (const auto& [k, v] : val->entries) {
                    seed = hashBytes(seed, k);
                    seed = combine(seed, hashBoundExpression(v));
                }
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (!val)
                    return;
                seed = hashOptExpr(seed, val->subject);
                for (const auto& [w, t] : val->when_thens) {
                    seed = combine(seed, hashBoundExpression(w));
                    seed = combine(seed, hashBoundExpression(t));
                }
                seed = hashOptExpr(seed, val->else_expr);
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                if (!val)
                    return;
                seed = combine(seed, hashBoundExpression(val->list));
                seed = combine(seed, hashBoundExpression(val->index));
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                if (!val)
                    return;
                seed = combine(seed, hashBoundExpression(val->list));
                seed = hashOptExpr(seed, val->from);
                seed = hashOptExpr(seed, val->to);
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (!val)
                    return;
                seed = combine(seed, hashBoundExpression(val->object));
                seed = hashBytes(seed, val->property);
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!val)
                    return;
                seed = hashBytes(seed, val->property_name);
                seed = combine(seed, hashBoundExpression(val->object));
                for (const auto& c : val->candidates) {
                    seed = combine(seed, static_cast<uint64_t>(c.label_id));
                    seed = combine(seed, static_cast<uint64_t>(c.prop_id));
                    seed = hashBoundType(seed, c.type);
                }
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (!val)
                    return;
                seed = combine(seed, hashBoundExpression(val->object));
                seed = combine(seed, static_cast<uint64_t>(val->label_id));
                seed = hashBoundType(seed, val->result_type);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                if (!val)
                    return;
                seed = hashBytes(seed, val->variable);
                seed = combine(seed, val->loop_column_index);
                seed = combine(seed, hashBoundExpression(val->list_expr));
                seed = hashOptExpr(seed, val->where_pred);
                seed = hashBoundType(seed, val->result_type);
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>)
                    seed = combine(seed, hashBoundExpression(val->projection));
            }
        },
        expr);
    return seed;
}

uint64_t hashBoundLogicalOperator(const binder::BoundLogicalOperator& op) {
    uint64_t seed = 14695981039346656037ULL;
    seed = combine(seed, op.index());
    std::visit(
        [&seed](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundSingletonOp>) {
                return;
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                for (const auto& v : val.variables)
                    seed = hashBytes(seed, v);
                for (const auto& t : val.types)
                    seed = hashBoundType(seed, t);
                for (auto c : val.column_indices)
                    seed = combine(seed, c);
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                seed = hashBytes(seed, val.variable);
                seed = combine(seed, val.column_index);
                for (const auto& [lid, pids] : val.label_prop_ids) {
                    seed = combine(seed, static_cast<uint64_t>(lid));
                    for (auto p : pids)
                        seed = combine(seed, p);
                }
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                seed = hashBytes(seed, val.variable);
                seed = combine(seed, val.column_index);
                for (auto l : val.label_ids)
                    seed = combine(seed, static_cast<uint64_t>(l));
                for (const auto& n : val.label_names)
                    seed = hashBytes(seed, n);
                for (const auto& [lid, pids] : val.label_prop_ids) {
                    seed = combine(seed, static_cast<uint64_t>(lid));
                    for (auto p : pids)
                        seed = combine(seed, p);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (!val)
                    return;
                seed = combine(seed, hashBoundExpression(val->predicate));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (!val)
                    return;
                for (const auto& it : val->items) {
                    seed = combine(seed, hashBoundExpression(it.expr));
                    seed = hashBytes(seed, it.alias);
                    seed = hashBoundType(seed, it.result_type);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (!val)
                    return;
                for (const auto& gk : val->group_keys)
                    seed = combine(seed, hashBoundExpression(gk));
                for (const auto& a : val->aggregates) {
                    seed = hashBytes(seed, a.function_name);
                    seed = combine(seed, hashBoundExpression(a.argument));
                    seed = hashBytes(seed, a.alias);
                    seed = hashBoundType(seed, a.result_type);
                    seed = hashScalar(seed, reinterpret_cast<uintptr_t>(a.func_def));
                    seed = combine(seed, a.distinct ? 1u : 0u);
                    seed = combine(seed, a.is_internal ? 1u : 0u);
                }
                for (const auto& n : val->output_names)
                    seed = hashBytes(seed, n);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (!val)
                    return;
                for (const auto& it : val->items) {
                    seed = combine(seed, hashBoundExpression(it.expr));
                    seed = combine(seed, static_cast<uint64_t>(it.direction));
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                if (!val)
                    return;
                seed = combine(seed, static_cast<uint64_t>(val->count));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                if (!val)
                    return;
                seed = combine(seed, static_cast<uint64_t>(val->count));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                return;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (!val)
                    return;
                seed = hashBytes(seed, val->src_variable);
                seed = combine(seed, val->src_column_index);
                seed = hashBytes(seed, val->edge_variable);
                seed = combine(seed, val->edge_column_index);
                seed = hashBytes(seed, val->dst_variable);
                seed = combine(seed, val->dst_column_index);
                for (auto l : val->edge_label_ids)
                    seed = combine(seed, static_cast<uint64_t>(l));
                seed = combine(seed, static_cast<uint64_t>(val->direction));
                for (auto p : val->edge_prop_ids)
                    seed = combine(seed, p);
                for (const auto& [lid, pids] : val->dst_label_prop_ids) {
                    seed = combine(seed, static_cast<uint64_t>(lid));
                    for (auto p : pids)
                        seed = combine(seed, p);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (!val)
                    return;
                seed = hashBytes(seed, val->src_variable);
                seed = combine(seed, val->src_column_index);
                seed = hashBytes(seed, val->dst_variable);
                seed = combine(seed, val->dst_column_index);
                for (auto l : val->edge_label_ids)
                    seed = combine(seed, static_cast<uint64_t>(l));
                seed = combine(seed, static_cast<uint64_t>(val->direction));
                seed = combine(seed, static_cast<uint64_t>(val->min_hops));
                seed = combine(seed, static_cast<uint64_t>(val->max_hops));
                for (const auto& [lid, pids] : val->dst_label_prop_ids) {
                    seed = combine(seed, static_cast<uint64_t>(lid));
                    for (auto p : pids)
                        seed = combine(seed, p);
                }
                seed = hashBytes(seed, val->path_variable);
                seed = combine(seed, val->path_column_index);
                seed = combine(seed, val->path_handled_by_varlen ? 1u : 0u);
                seed = hashBytes(seed, val->edge_variable);
                seed = combine(seed, val->edge_column_index);
                for (const auto& [lid, pids] : val->edge_prop_filters) {
                    seed = combine(seed, static_cast<uint64_t>(lid));
                    for (const auto& [pid, pv] : pids) {
                        seed = combine(seed, pid);
                        seed = hashPropertyValue(seed, pv);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (!val)
                    return;
                seed = hashBytes(seed, val->path_variable);
                seed = combine(seed, val->path_column_index);
                for (const auto& ev : val->element_variables)
                    seed = hashBytes(seed, ev);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (!val)
                    return;
                seed = combine(seed, hashBoundExpression(val->list_expr));
                seed = hashBytes(seed, val->variable);
                seed = combine(seed, val->variable_column_index);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (!val)
                    return;
                seed = hashBytes(seed, val->variable);
                for (auto l : val->label_ids)
                    seed = combine(seed, static_cast<uint64_t>(l));
                for (const auto& [lid, props] : val->label_properties) {
                    seed = combine(seed, static_cast<uint64_t>(lid));
                    for (const auto& [pid, expr] : props) {
                        seed = combine(seed, pid);
                        seed = combine(seed, hashBoundExpression(expr));
                    }
                }
                for (const auto& [n, expr] : val->pending_props) {
                    seed = hashBytes(seed, n);
                    seed = combine(seed, hashBoundExpression(expr));
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (!val)
                    return;
                seed = hashBytes(seed, val->variable);
                seed = combine(seed, val->label_id.has_value() ? static_cast<uint64_t>(*val->label_id) : 0u);
                if (val->label_name)
                    seed = hashBytes(seed, *val->label_name);
                seed = hashBytes(seed, val->src_variable);
                seed = hashBytes(seed, val->dst_variable);
                for (const auto& [pid, expr] : val->properties) {
                    seed = combine(seed, pid);
                    seed = combine(seed, hashBoundExpression(expr));
                }
                for (const auto& [n, expr] : val->pending_props) {
                    seed = hashBytes(seed, n);
                    seed = combine(seed, hashBoundExpression(expr));
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>>) {
                if (!val)
                    return;
                for (const auto& it : val->items) {
                    seed = combine(seed, static_cast<uint64_t>(it.kind));
                    seed = hashBytes(seed, it.target_variable);
                    seed = hashBytes(seed, it.prop_name);
                    seed = combine(seed, it.prop_id.has_value() ? *it.prop_id : 0xFFFFu);
                    seed = hashOptExpr(seed, it.value_expr);
                    seed = combine(seed, it.label_id.has_value() ? static_cast<uint64_t>(*it.label_id) : 0u);
                    seed = combine(seed, it.strong_mode ? 1u : 0u);
                    seed = combine(seed, it.is_add_assign ? 1u : 0u);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>>) {
                if (!val)
                    return;
                for (const auto& it : val->items) {
                    seed = combine(seed, static_cast<uint64_t>(it.kind));
                    seed = hashBytes(seed, it.target_variable);
                    seed = hashBytes(seed, it.prop_name);
                    seed = combine(seed, it.prop_id);
                    seed = combine(seed, it.label_id.has_value() ? static_cast<uint64_t>(*it.label_id) : 0u);
                    seed = combine(seed, it.strong_mode ? 1u : 0u);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>>) {
                if (!val)
                    return;
                seed = combine(seed, val->detach ? 1u : 0u);
                for (const auto& t : val->targets) {
                    seed = combine(seed, static_cast<uint64_t>(t.kind));
                    seed = hashBytes(seed, t.variable_name);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                if (!val)
                    return;
                seed = combine(seed, static_cast<uint64_t>(val->join_type));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                if (!val)
                    return;
                for (const auto& [l, r] : val->correlation) {
                    seed = combine(seed, l);
                    seed = combine(seed, r);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                if (!val)
                    return;
                for (const auto& [l, r] : val->correlation) {
                    seed = combine(seed, l);
                    seed = combine(seed, r);
                }
                seed = combine(seed, val->anti ? 1u : 0u);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (!val)
                    return;
                seed = combine(seed, val->all ? 1u : 0u);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (!val)
                    return;
                if (val->path_variable)
                    seed = hashBytes(seed, *val->path_variable);
                seed = hashBytes(seed, val->start_var);
                seed = combine(seed, val->start_pre_bound ? 1u : 0u);
                for (auto l : val->start_labels)
                    seed = combine(seed, static_cast<uint64_t>(l));
                for (const auto& [pid, expr] : val->start_prop_filters) {
                    seed = combine(seed, pid);
                    seed = combine(seed, hashBoundExpression(expr));
                }
                for (const auto& [n, expr] : val->start_pending_props) {
                    seed = hashBytes(seed, n);
                    seed = combine(seed, hashBoundExpression(expr));
                }
                seed = combine(seed, val->has_relationship ? 1u : 0u);
                seed = hashBytes(seed, val->edge_var);
                seed = combine(seed, val->edge_label_id.has_value() ? static_cast<uint64_t>(*val->edge_label_id) : 0u);
                if (val->edge_label_name)
                    seed = hashBytes(seed, *val->edge_label_name);
                seed = combine(seed, static_cast<uint64_t>(val->direction));
                for (const auto& [pid, expr] : val->edge_prop_filters) {
                    seed = combine(seed, pid);
                    seed = combine(seed, hashBoundExpression(expr));
                }
                for (const auto& [n, expr] : val->edge_pending_props) {
                    seed = hashBytes(seed, n);
                    seed = combine(seed, hashBoundExpression(expr));
                }
                seed = hashBytes(seed, val->end_var);
                seed = combine(seed, val->end_pre_bound ? 1u : 0u);
                for (auto l : val->end_labels)
                    seed = combine(seed, static_cast<uint64_t>(l));
                for (const auto& [pid, expr] : val->end_prop_filters) {
                    seed = combine(seed, pid);
                    seed = combine(seed, hashBoundExpression(expr));
                }
                for (const auto& [n, expr] : val->end_pending_props) {
                    seed = hashBytes(seed, n);
                    seed = combine(seed, hashBoundExpression(expr));
                }
            }
        },
        op);
    return seed;
}

} // namespace optimizer
} // namespace eugraph
