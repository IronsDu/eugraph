#include "query/optimizer/column_rewrite.hpp"

#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "query/planner/bound_expression/bound_function_call.hpp"
#include "query/planner/bound_expression/bound_map.hpp"
#include "query/planner/bound_expression/bound_property_ref.hpp"
#include "query/planner/bound_expression/bound_quantifier_expr.hpp"
#include "query/planner/bound_expression/bound_slice.hpp"
#include "query/planner/bound_expression/bound_subscript.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_edge_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_node_op.hpp"
#include "query/planner/logical_plan/operator/bound_delete_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"
#include "query/planner/logical_plan/operator/bound_remove_op.hpp"
#include "query/planner/logical_plan/operator/bound_set_op.hpp"
#include "query/planner/logical_plan/operator/bound_sort_op.hpp"


namespace eugraph {
namespace optimizer {

namespace {

/// Extract a variable name from an object expression (BoundVariableRef or
/// BoundColumnRef). Also unwraps BoundLabelCast and startNode/endNode function
/// calls. Returns empty string if not extractable.
std::string varNameFromObject(const binder::BoundExpression& obj) {
    if (auto* vref = std::get_if<binder::BoundVariableRef>(&obj))
        return vref->name;
    if (auto* cref = std::get_if<binder::BoundColumnRef>(&obj))
        return cref->name;
    if (auto* lc = std::get_if<std::unique_ptr<binder::BoundLabelCast>>(&obj)) {
        if (*lc)
            return varNameFromObject((*lc)->object);
    }
    if (auto* fc = std::get_if<std::unique_ptr<binder::BoundFunctionCall>>(&obj)) {
        if (*fc && ((*fc)->func_def && ((*fc)->func_def->name == "startNode" || (*fc)->func_def->name == "endNode")) &&
            !(*fc)->args.empty())
            return varNameFromObject((*fc)->args[0]);
    }
    return "";
}

/// Find the offset of (lid, pid) in the extraction info's ordered prop list.
/// Returns SIZE_MAX if not found.
size_t findPropOffset(const PropertyExtractionInfo& info, LabelId lid, uint16_t pid) {
    for (size_t i = 0; i < info.prop_order.size(); ++i) {
        if (info.prop_order[i].first == lid && info.prop_order[i].second == pid)
            return i + 1; // +1 to skip the source column
    }
    return SIZE_MAX;
}

/// Walk a single BoundExpression and collect requirements into `reqs` keyed
/// by variable name. Handles property refs, dynamic property refs, function
/// calls (labels/type), and recurses into compound expressions.
///
/// `in_schema_changing_op` is true when the expression lives inside an operator
/// that changes the output schema (Project / Aggregate). In that case property
/// refs are conservatively routed through need_whole_vertex because the column
/// rewrite pass cannot track indices across schema-changing boundaries.
///
/// `loop_var_skip` is the loop variable name of an enclosing list comprehension
/// / quantifier expression. References to that name resolve to a runtime scope,
/// not a plan column, so they must not contribute to PlanRequirements.
void collectExprReqs(const binder::BoundExpression& expr, PlanRequirements& reqs, bool in_schema_changing_op,
                     const std::string& loop_var_skip = "") {
    std::visit(
        [&](const auto& ptr) {
            using T = std::decay_t<decltype(ptr)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!ptr)
                    return;
                std::string var = varNameFromObject(ptr->object);
                if (var.empty() || var == loop_var_skip)
                    return;
                // Determine whether the object is an edge variable. BoundPropertyRef
                // uses the same LabelId type for both vertex labels and edge labels,
                // so we look at the object's bound type to disambiguate.
                bool is_edge = false;
                if (auto* vref = std::get_if<binder::BoundVariableRef>(&ptr->object))
                    is_edge = (vref->type.kind == binder::BoundTypeKind::EDGE);
                else if (auto* cref = std::get_if<binder::BoundColumnRef>(&ptr->object))
                    is_edge = (cref->type.kind == binder::BoundTypeKind::EDGE);
                auto& vr = reqs[var];
                if (is_edge) {
                    if (in_schema_changing_op) {
                        vr.need_whole_edge = true;
                    } else {
                        for (const auto& cand : ptr->candidates)
                            vr.edge_props.emplace_back(EdgeLabelId{cand.label_id}, cand.prop_id);
                    }
                } else {
                    if (in_schema_changing_op) {
                        // Project/WITH/Aggregate rewrite schemas; the flat extract
                        // column index visible at the topology source no longer
                        // matches what this consumer sees. Route through the whole
                        // object so the evaluator can resolve by name.
                        vr.need_whole_vertex = true;
                    } else {
                        for (const auto& cand : ptr->candidates)
                            vr.vertex_props.emplace_back(cand.label_id, cand.prop_id);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (!ptr)
                    return;
                std::string var = varNameFromObject(ptr->object);
                if (!var.empty() && var != loop_var_skip) {
                    // Dynamic property access requires the full object so that
                    // evalDynamicPropertyRef can scan labels + properties by name.
                    if (ptr->result_type.kind == binder::BoundTypeKind::EDGE)
                        reqs[var].need_whole_edge = true;
                    else
                        reqs[var].need_whole_vertex = true;
                }
                // Recurse into the object expression so that nested function calls
                // (e.g. startNode(r) inside startNode(r).id) can register their own
                // requirements (need_whole_edge for the edge argument).
                collectExprReqs(ptr->object, reqs, in_schema_changing_op, loop_var_skip);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (ptr) {
                    collectExprReqs(ptr->left, reqs, in_schema_changing_op, loop_var_skip);
                    collectExprReqs(ptr->right, reqs, in_schema_changing_op, loop_var_skip);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (ptr)
                    collectExprReqs(ptr->operand, reqs, in_schema_changing_op, loop_var_skip);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (ptr) {
                    std::string fname = ptr->func_def ? ptr->func_def->name : "";
                    // Function arg → variable requirement. Skip the enclosing
                    // list-comprehension / quantifier loop variable: that name
                    // resolves to a runtime scope, not a plan column, and
                    // registering it here would leak it into PropertyExtractionInfo
                    // and cause rewriteExpr to clobber the loop variable's
                    // column_index.
                    auto reqVarIfPlan = [&](const std::string& var) -> std::string {
                        return (!var.empty() && var != loop_var_skip) ? var : std::string{};
                    };
                    if (fname == "labels" && ptr->args.size() == 1) {
                        std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                        if (!var.empty()) {
                            // labels(n) reads vertex.labels at eval time; the evaluator
                            // needs a VertexValue column (LoadVertexLabels alone emits
                            // List<String>, which labelsImpl cannot consume).
                            reqs[var].need_vertex_labels = true;
                            reqs[var].need_whole_vertex = true;
                        }
                    } else if (fname == "type" && ptr->args.size() == 1) {
                        std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                        if (!var.empty()) {
                            reqs[var].need_edge_type = true;
                            reqs[var].need_whole_edge = true;
                        }
                    } else if ((fname == "properties" || fname == "keys") && ptr->args.size() == 1) {
                        std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                        if (!var.empty()) {
                            // Determine vertex vs edge from the argument's BoundType.
                            bool is_edge = false;
                            if (auto* vref = std::get_if<binder::BoundVariableRef>(&ptr->args[0]))
                                is_edge = (vref->type.kind == binder::BoundTypeKind::EDGE);
                            else if (auto* cref = std::get_if<binder::BoundColumnRef>(&ptr->args[0]))
                                is_edge = (cref->type.kind == binder::BoundTypeKind::EDGE);
                            if (is_edge)
                                reqs[var].need_whole_edge = true;
                            else
                                reqs[var].need_whole_vertex = true;
                        }
                    } else if ((fname == "startNode" || fname == "endNode") && ptr->args.size() == 1) {
                        // startNode/endNode read EdgeValue.src_id / dst_id; force
                        // full edge materialization (an int edge-id column would
                        // otherwise arrive and the impl throws InvalidArgumentValue).
                        std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                        if (!var.empty())
                            reqs[var].need_whole_edge = true;
                    } else if ((fname == "toInteger" || fname == "toFloat" || fname == "toBoolean" ||
                                fname == "toString") &&
                               ptr->args.size() == 1) {
                        // Conversion functions throw TypeError on entity inputs
                        // (VertexValue/EdgeValue). Without the full entity, the
                        // argument arrives as VertexRef/EdgeKey and the function
                        // silently returns NULL instead of raising the expected
                        // error. Force full materialization.
                        std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                        if (!var.empty()) {
                            bool is_edge = false;
                            if (auto* vref = std::get_if<binder::BoundVariableRef>(&ptr->args[0]))
                                is_edge = (vref->type.kind == binder::BoundTypeKind::EDGE);
                            else if (auto* cref = std::get_if<binder::BoundColumnRef>(&ptr->args[0]))
                                is_edge = (cref->type.kind == binder::BoundTypeKind::EDGE);
                            if (is_edge)
                                reqs[var].need_whole_edge = true;
                            else
                                reqs[var].need_whole_vertex = true;
                        }
                    }
                    for (const auto& a : ptr->args)
                        collectExprReqs(a, reqs, in_schema_changing_op, loop_var_skip);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (ptr)
                    for (const auto& e : ptr->elements)
                        collectExprReqs(e, reqs, in_schema_changing_op, loop_var_skip);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                if (ptr)
                    for (const auto& [k, v] : ptr->entries)
                        collectExprReqs(v, reqs, in_schema_changing_op, loop_var_skip);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (ptr) {
                    if (ptr->subject)
                        collectExprReqs(*ptr->subject, reqs, in_schema_changing_op, loop_var_skip);
                    for (const auto& [w, t] : ptr->when_thens) {
                        collectExprReqs(w, reqs, in_schema_changing_op, loop_var_skip);
                        collectExprReqs(t, reqs, in_schema_changing_op, loop_var_skip);
                    }
                    if (ptr->else_expr)
                        collectExprReqs(*ptr->else_expr, reqs, in_schema_changing_op, loop_var_skip);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (ptr) {
                    // n::Label requires the full vertex to scope properties.
                    std::string var = varNameFromObject(ptr->object);
                    if (!var.empty())
                        reqs[var].need_whole_vertex = true;
                    collectExprReqs(ptr->object, reqs, in_schema_changing_op, loop_var_skip);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                if (ptr) {
                    collectExprReqs(ptr->list, reqs, in_schema_changing_op, loop_var_skip);
                    collectExprReqs(ptr->index, reqs, in_schema_changing_op, loop_var_skip);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                if (ptr) {
                    collectExprReqs(ptr->list, reqs, in_schema_changing_op, loop_var_skip);
                    if (ptr->from)
                        collectExprReqs(*ptr->from, reqs, in_schema_changing_op, loop_var_skip);
                    if (ptr->to)
                        collectExprReqs(*ptr->to, reqs, in_schema_changing_op, loop_var_skip);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                if (ptr) {
                    // List comprehension and quantifier expressions introduce a
                    // local loop variable that resolves to a runtime scope, not
                    // to the plan's output columns. Pass that name as loop_var_skip
                    // so references to it inside list_expr/where_pred/projection
                    // don't get registered as plan-level requirements. Outer
                    // variable references still propagate normally.
                    collectExprReqs(ptr->list_expr, reqs, in_schema_changing_op, ptr->variable);
                    if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                        collectExprReqs(ptr->projection, reqs, in_schema_changing_op, ptr->variable);
                    }
                    if (ptr->where_pred)
                        collectExprReqs(*ptr->where_pred, reqs, in_schema_changing_op, ptr->variable);
                }
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                // Standalone variable reference (not consumed as the object of a
                // BoundPropertyRef): the entity value will be inspected directly —
                // e.g. placed in a list/map, compared, or passed to a function.
                // Force materialization so VertexRef/EdgeKey becomes VertexValue/EdgeValue.
                // Skip the loop variable of an enclosing list comprehension /
                // quantifier — that name resolves to a runtime scope, not a plan column.
                if (!ptr.name.empty() && ptr.name != loop_var_skip) {
                    if (ptr.type.kind == binder::BoundTypeKind::EDGE)
                        reqs[ptr.name].need_whole_edge = true;
                    else if (ptr.type.kind == binder::BoundTypeKind::VERTEX)
                        reqs[ptr.name].need_whole_vertex = true;
                }
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                // Standalone column reference (e.g. `WITH x ... RETURN x`): the
                // entity will be inspected directly. Materialize so downstream
                // sees VertexValue/EdgeValue.
                if (!ptr.name.empty() && ptr.name != loop_var_skip) {
                    if (ptr.type.kind == binder::BoundTypeKind::EDGE)
                        reqs[ptr.name].need_whole_edge = true;
                    else if (ptr.type.kind == binder::BoundTypeKind::VERTEX)
                        reqs[ptr.name].need_whole_vertex = true;
                }
            }
            // BoundLiteral / BoundParameter: leaf, no requirements.
        },
        expr);
}

/// Walk the BoundLogicalOperator tree and accumulate requirements. Stops at
/// topology-producing operators (Scan/Expand/VarLenExpand) since their output
/// variables' requirements are consumed here, not propagated further down.
void collectOpReqs(const binder::BoundLogicalOperator& op, PlanRequirements& reqs) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                // scans: fold binder-pushed-down label_prop_ids into vertex_props
                // so dispatchProjectionExtract loads them. These originate from
                // inline pattern predicates (MATCH (:L {p: ..})) and projection
                // pushdown (RETURN n.p).
                if (!v.variable.empty()) {
                    for (const auto& [lid, pids] : v.label_prop_ids)
                        for (uint16_t pid : pids)
                            reqs[v.variable].vertex_props.emplace_back(lid, pid);
                }
            } else if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                                 std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                // Leaf; no requirements.
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v) {
                    collectExprReqs(v->predicate, reqs, /*in_schema_changing_op=*/false);
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    // Project changes schema (selects/aliases columns). Route any
                    // property ref inside it through need_whole_vertex instead of
                    // appending flat columns, because the flat column index cannot
                    // be tracked across the projection boundary.
                    for (const auto& item : v->items) {
                        if (auto* cref = std::get_if<binder::BoundColumnRef>(&item.expr)) {
                            if (cref->type.kind == binder::BoundTypeKind::VERTEX && !cref->name.empty())
                                reqs[cref->name].need_whole_vertex = true;
                            else if (cref->type.kind == binder::BoundTypeKind::EDGE && !cref->name.empty())
                                reqs[cref->name].need_whole_edge = true;
                        }
                        collectExprReqs(item.expr, reqs, /*in_schema_changing_op=*/true);
                    }
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v) {
                    for (const auto& item : v->items)
                        collectExprReqs(item.expr, reqs, /*in_schema_changing_op=*/false);
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    for (const auto& k : v->group_keys)
                        collectExprReqs(k, reqs, /*in_schema_changing_op=*/true);
                    for (const auto& agg : v->aggregates)
                        collectExprReqs(agg.argument, reqs, /*in_schema_changing_op=*/true);
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    // Fold binder-pushed-down edge/dst prop ids into requirements.
                    if (!v->edge_variable.empty() && !v->edge_prop_ids.empty() && !v->edge_label_ids.empty()) {
                        for (uint16_t pid : v->edge_prop_ids)
                            reqs[v->edge_variable].edge_props.emplace_back(v->edge_label_ids[0], pid);
                    }
                    if (!v->dst_variable.empty()) {
                        for (const auto& [lid, pids] : v->dst_label_prop_ids)
                            for (uint16_t pid : pids)
                                reqs[v->dst_variable].vertex_props.emplace_back(lid, pid);
                    }
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v) {
                    if (!v->dst_variable.empty()) {
                        for (const auto& [lid, pids] : v->dst_label_prop_ids)
                            for (uint16_t pid : pids)
                                reqs[v->dst_variable].vertex_props.emplace_back(lid, pid);
                    }
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v) {
                    // UNWIND list_expr references input-schema variables (e.g.
                    // `UNWIND keys(n) AS x`). Without collecting from list_expr,
                    // keys(n)/properties(n) never trigger need_whole_vertex and
                    // the evaluator receives VertexRef instead of VertexValue.
                    collectExprReqs(v->list_expr, reqs, /*in_schema_changing_op=*/false);
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                if (v)
                    collectOpReqs(v->child, reqs);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>>) {
                if (v) {
                    for (const auto& item : v->items) {
                        // SET n.x = ...: convenience mode requires vertex.labels to
                        // resolve prop_id when item is not strong-typed.
                        if (!item.target_variable.empty())
                            reqs[item.target_variable].need_whole_vertex = true;
                        if (item.value_expr)
                            collectExprReqs(*item.value_expr, reqs, /*in_schema_changing_op=*/false);
                    }
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>>) {
                if (v) {
                    for (const auto& item : v->items) {
                        if (!item.target_variable.empty())
                            reqs[item.target_variable].need_whole_vertex = true;
                    }
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>>) {
                if (v) {
                    for (const auto& t : v->targets) {
                        if (!t.variable_name.empty()) {
                            if (t.kind == binder::BoundDeleteOp::TargetKind::EDGE)
                                reqs[t.variable_name].need_whole_edge = true;
                            else
                                reqs[t.variable_name].need_whole_vertex = true;
                        }
                    }
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (v) {
                    // pre_bound variables come from the child pipeline and are read
                    // as VertexValue by merge_physical_op (line ~1042). Mark them
                    // so the child's ProjectionExtract constructs the full vertex.
                    if (v->start_pre_bound && !v->start_var.empty())
                        reqs[v->start_var].need_whole_vertex = true;
                    if (v->has_relationship && v->end_pre_bound && !v->end_var.empty())
                        reqs[v->end_var].need_whole_vertex = true;
                    // Property expressions in pending_props (e.g. person.bornIn
                    // in MERGE (city:City {name: person.bornIn})) may reference
                    // properties of input variables.
                    for (const auto& [prop_name, expr] : v->start_pending_props)
                        collectExprReqs(expr, reqs, /*in_schema_changing_op=*/false);
                    for (const auto& [prop_name, expr] : v->edge_pending_props)
                        collectExprReqs(expr, reqs, /*in_schema_changing_op=*/false);
                    for (const auto& [prop_name, expr] : v->end_pending_props)
                        collectExprReqs(expr, reqs, /*in_schema_changing_op=*/false);
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                // CreateEdge's child subtree (e.g. Filter) may read source-variable
                // properties (a.name = 'x'). Descend so those requirements reach
                // the upstream scan / expand where ProjectionExtract can attach them.
                if (v) {
                    for (const auto& [prop_name, expr] : v->pending_props)
                        collectExprReqs(expr, reqs, /*in_schema_changing_op=*/false);
                    collectOpReqs(v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v) {
                    // Property expressions in pending_props (e.g. d.name + 'X'
                    // in CREATE (e:E {name: d.name + 'X'})) may reference
                    // properties of input variables. Collect those requirements
                    // so ProjectionExtract loads the source properties.
                    for (const auto& [prop_name, expr] : v->pending_props)
                        collectExprReqs(expr, reqs, /*in_schema_changing_op=*/false);
                    if (v->child)
                        collectOpReqs(*v->child, reqs);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (v) {
                    collectOpReqs(v->left, reqs);
                    collectOpReqs(v->right, reqs);
                }
            }
        },
        op);
}

/// De-duplicate vertex_props and edge_props in a VariableRequirement.
void dedupeVarReqs(VariableRequirement& r) {
    auto dedupe = [](auto& vec) {
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
    };
    dedupe(r.vertex_props);
    dedupe(r.edge_props);
}

} // namespace

PlanRequirements collectPlanRequirements(const binder::BoundLogicalOperator& root) {
    PlanRequirements reqs;
    collectOpReqs(root, reqs);
    for (auto& [var, r] : reqs)
        dedupeVarReqs(r);
    return reqs;
}

std::unordered_map<std::string, PropertyExtractionInfo>
buildExtractionInfo(const binder::BoundLogicalOperator& /*root*/, const PlanRequirements& reqs) {
    std::unordered_map<std::string, PropertyExtractionInfo> info;
    for (const auto& [var, r] : reqs) {
        // need_whole_vertex / need_whole_edge: extract's ConstructVertex/ConstructEdge
        // column OVERWRITES the source column in-place. Earlier comments assumed
        // "same index" so no rewrite was needed; but earlier variables' property
        // columns shift everyone downstream, so we still need to record base_col
        // to update BoundColumnRef(var).column_index for direct variable refs
        // (e.g. `WITH b`, `RETURN b`).
        if (r.need_whole_vertex || r.need_whole_edge) {
            PropertyExtractionInfo pi;
            pi.base_col = 0; // Filled in by updateExtractionBaseCols
            info[var] = std::move(pi);
            continue;
        }
        if (r.vertex_props.empty() && r.edge_props.empty())
            continue;
        PropertyExtractionInfo pi;
        pi.base_col = 0; // Filled in by updateExtractionBaseCols
        pi.prop_order = r.vertex_props;
        pi.edge_prop_order = r.edge_props;
        info[var] = std::move(pi);
    }
    return info;
}

// ===== Existing helpers (kept for compatibility; used by RBO path) =====

namespace {

/// Rewrite a BoundExpression, replacing BoundPropertyRef with BoundColumnRef.
bool rewriteExpr(binder::BoundExpression& expr, const std::unordered_map<std::string, PropertyExtractionInfo>& info) {
    return std::visit(
        [&](auto& val) -> bool {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!val)
                    return false;
                std::string var = varNameFromObject(val->object);
                if (var.empty())
                    return false;

                auto it = info.find(var);
                if (it == info.end())
                    return false;

                bool is_edge = false;
                if (auto* vref = std::get_if<binder::BoundVariableRef>(&val->object))
                    is_edge = (vref->type.kind == binder::BoundTypeKind::EDGE);
                else if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object))
                    is_edge = (cref->type.kind == binder::BoundTypeKind::EDGE);

                if (is_edge) {
                    for (const auto& cand : val->candidates) {
                        for (size_t ei = 0; ei < it->second.edge_prop_order.size(); ++ei) {
                            const auto& ep = it->second.edge_prop_order[ei];
                            if (EdgeLabelId{cand.label_id} == ep.first && cand.prop_id == ep.second) {
                                size_t offset = 1 + it->second.prop_order.size() + ei;
                                uint32_t new_col = it->second.base_col + static_cast<uint32_t>(offset);
                                expr = binder::BoundColumnRef{new_col, cand.type, var};
                                return true;
                            }
                        }
                    }
                } else {
                    for (const auto& cand : val->candidates) {
                        size_t offset = findPropOffset(it->second, cand.label_id, cand.prop_id);
                        if (offset == SIZE_MAX)
                            continue;
                        uint32_t new_col = it->second.base_col + static_cast<uint32_t>(offset);
                        expr = binder::BoundColumnRef{new_col, cand.type, var};
                        return true;
                    }
                }
                // Flat-column rewrite failed (e.g. need_whole_vertex with empty
                // prop_order). The BoundColumnRef inside the object still holds
                // the binder-assigned column_index, which does not account for
                // property columns appended by ProjectionExtract before this
                // variable. Update it so evalPropertyRef can locate the
                // ConstructVertex column at runtime.
                if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object)) {
                    if (it->second.base_col != cref->column_index) {
                        cref->column_index = it->second.base_col;
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (!val)
                    return false;
                bool a = rewriteExpr(val->left, info);
                bool b = rewriteExpr(val->right, info);
                return a || b;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (!val)
                    return false;
                return rewriteExpr(val->operand, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (!val)
                    return false;
                bool changed = false;
                for (auto& arg : val->args)
                    changed |= rewriteExpr(arg, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (!val)
                    return false;
                bool changed = false;
                for (auto& e : val->elements)
                    changed |= rewriteExpr(e, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                if (!val)
                    return false;
                bool changed = false;
                for (auto& [k, v] : val->entries)
                    changed |= rewriteExpr(v, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (!val)
                    return false;
                bool changed = false;
                if (val->subject)
                    changed |= rewriteExpr(*val->subject, info);
                for (auto& [w, t] : val->when_thens) {
                    changed |= rewriteExpr(w, info);
                    changed |= rewriteExpr(t, info);
                }
                if (val->else_expr)
                    changed |= rewriteExpr(*val->else_expr, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (!val)
                    return false;
                return rewriteExpr(val->object, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                if (!val)
                    return false;
                bool changed = rewriteExpr(val->list, info);
                changed |= rewriteExpr(val->index, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                if (!val)
                    return false;
                bool changed = rewriteExpr(val->list, info);
                if (val->from)
                    changed |= rewriteExpr(*val->from, info);
                if (val->to)
                    changed |= rewriteExpr(*val->to, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                if (!val)
                    return false;
                bool changed = rewriteExpr(val->list_expr, info);
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>)
                    changed |= rewriteExpr(val->projection, info);
                if (val->where_pred)
                    changed |= rewriteExpr(*val->where_pred, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                // Dynamic property ref — left intact. Evaluator reads from the
                // constructed VertexValue/EdgeValue column at runtime.
                // Still update the BoundColumnRef inside the object so the
                // evaluator can locate the constructed entity column.
                if (val) {
                    bool changed = false;
                    // If the object itself is a ColumnRef, update it directly.
                    if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object)) {
                        std::string var = varNameFromObject(val->object);
                        if (!var.empty()) {
                            auto it = info.find(var);
                            if (it != info.end() && it->second.base_col != cref->column_index) {
                                cref->column_index = it->second.base_col;
                                changed = true;
                            }
                        }
                    }
                    // Recurse into the object expression so that nested column refs
                    // (e.g. the edge argument inside startNode(r).id) are rewritten.
                    changed |= rewriteExpr(val->object, info);
                    return changed;
                }
                return false;
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                // Direct variable reference (e.g. `WITH b` or `RETURN b`):
                // base_col may have shifted because ProjectionExtract appended
                // property columns for earlier variables. Update column_index
                // to point at the variable's physical source column.
                auto it = info.find(val.name);
                if (it != info.end() && it->second.base_col != val.column_index) {
                    val.column_index = it->second.base_col;
                    return true;
                }
                return false;
            }
            return false;
        },
        expr);
}

void rewriteOp(binder::BoundLogicalOperator& op, const std::unordered_map<std::string, PropertyExtractionInfo>& info,
               const ProjectResetMap* project_resets, const LeftJoinColMap* left_join_cols = nullptr) {
    // Build the INPUT-scope view of `info` for a schema-changing operator:
    // variables reset by THIS operator get base_col switched back to the
    // producer-side position captured at THIS operator's reset (stored in
    // project_resets). Returns a copy only when a switch is actually needed;
    // otherwise returns the original map unchanged so callers can pass it
    // through without allocation.
    auto buildInputScope =
        [&project_resets](
            const std::unordered_map<std::string, PropertyExtractionInfo>& base,
            const void* op_ptr) -> std::pair<bool, std::unordered_map<std::string, PropertyExtractionInfo>> {
        if (!project_resets)
            return {false, base};
        auto rit = project_resets->find(op_ptr);
        if (rit == project_resets->end())
            return {false, base};
        auto copy = base;
        bool changed = false;
        for (const auto& [var, input_pos] : rit->second) {
            auto it = copy.find(var);
            if (it != copy.end() && it->second.base_col != input_pos) {
                it->second.base_col = input_pos;
                changed = true;
            }
        }
        return {changed, std::move(copy)};
    };

    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v) {
                    rewriteExpr(v->predicate, info);
                    rewriteOp(v->child, info, project_resets, left_join_cols);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    // Project's items + child subtree consume THIS Project's INPUT
                    // schema. Variables reset by THIS Project had their base_col
                    // overwritten to the OUTPUT position; switch them back to the
                    // producer position (saved in project_resets) for the items
                    // and the child descent. Variables NOT reset by THIS Project
                    // (e.g. an ancestor WITH already reset them, or they live
                    // entirely inside the child subtree) keep their current
                    // base_col. The project_resets map lets us distinguish a true
                    // reset from an inherited one (without it, nested WITH/RETURN
                    // chains collapse all positions to the innermost Project's
                    // input and the outer Project's items get mis-rewritten).
                    auto [changed, scope] = buildInputScope(info, static_cast<const void*>(&*v));
                    const auto& input_scope = changed ? scope : info;
                    for (auto& item : v->items)
                        rewriteExpr(item.expr, input_scope);
                    rewriteOp(v->child, input_scope, project_resets, left_join_cols);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v) {
                    for (auto& item : v->items)
                        rewriteExpr(item.expr, info);
                    rewriteOp(v->child, info, project_resets, left_join_cols);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    // Aggregate's group_keys and aggregate arguments consume the
                    // INPUT schema, same as Project's items. Without this switch,
                    // a group_key like BoundColumnRef(r) would be rewritten to the
                    // post-Aggregate output position, but the runtime reads INPUT
                    // columns — yielding the wrong variable. (Symmetric to the
                    // ProjectOp branch above.)
                    auto [changed, scope] = buildInputScope(info, static_cast<const void*>(&*v));
                    const auto& input_scope = changed ? scope : info;
                    for (auto& k : v->group_keys)
                        rewriteExpr(k, input_scope);
                    for (auto& agg : v->aggregates)
                        rewriteExpr(agg.argument, input_scope);
                    rewriteOp(v->child, input_scope, project_resets, left_join_cols);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v)
                    rewriteOp(v->child, info, project_resets, left_join_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v)
                    rewriteOp(v->child, info, project_resets, left_join_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v) {
                    // list_expr references INPUT schema variables; rewrite
                    // BoundColumnRef indices so they point at the ConstructVertex
                    // / flat property columns emitted by upstream ProjectionExtract.
                    rewriteExpr(v->list_expr, info);
                    rewriteOp(v->child, info, project_resets, left_join_cols);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                if (v)
                    rewriteOp(v->child, info, project_resets, left_join_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (v) {
                    rewriteOp(v->left, info, project_resets, left_join_cols);
                    if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                  std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                  std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                        // The right sub-plan of LeftJoin/BinaryJoin has its OWN
                        // column space. base_col in the shared info map includes
                        // the left column count as an offset. Expressions inside
                        // the right sub-plan (e.g. WHERE clause on OPTIONAL MATCH,
                        // or VarLenExpand's dispatched ProjectionExtract) see only
                        // the sub-plan's columns, so adjust base_col to be
                        // sub-plan-local.
                        auto right_info = info;
                        if (left_join_cols) {
                            auto it = left_join_cols->find(&*v);
                            if (it != left_join_cols->end()) {
                                uint32_t offset = it->second;
                                for (auto& [var, pei] : right_info) {
                                    if (pei.base_col >= offset)
                                        pei.base_col -= offset;
                                }
                            }
                        }
                        rewriteOp(v->right, right_info, project_resets, left_join_cols);
                    } else {
                        rewriteOp(v->right, info, project_resets, left_join_cols);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                // Descend so Filter / Project under CREATE (e.g. CREATE ... WHERE
                // a.name = ...) get their BoundPropertyRef rewritten to the flat
                // property columns emitted by ProjectionExtract.
                if (v)
                    rewriteOp(v->child, info, project_resets, left_join_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v && v->child)
                    rewriteOp(*v->child, info, project_resets, left_join_cols);
            }
            // Leaves (Scan, LabelScan, Singleton, CorrelatedSource) and write ops
            // (Set, Remove, Delete, Merge) are left unmodified.
        },
        op);
}

void collectFromChosen(const ChosenPlan& chosen, std::unordered_map<std::string, PropertyExtractionInfo>& info,
                       uint32_t base_col) {
    if (chosen.tag == PhysicalOpTag::VertexPropertyExtract) {
        auto& ext = info[chosen.enrich_variable];
        ext.base_col = base_col;
        auto it = chosen.enrich_output.find(chosen.enrich_variable);
        if (it != chosen.enrich_output.end()) {
            for (const auto& [lid, pids] : it->second.need_props)
                for (uint16_t pid : pids)
                    ext.prop_order.emplace_back(lid, pid);
        }
    }
    for (const auto& c : chosen.children)
        if (c)
            collectFromChosen(*c, info, base_col);
}

} // namespace

std::unordered_map<std::string, PropertyExtractionInfo> collectExtractionInfo(const ChosenPlan& chosen,
                                                                              uint32_t base_col) {
    std::unordered_map<std::string, PropertyExtractionInfo> info;
    collectFromChosen(chosen, info, base_col);
    return info;
}

/// Count how many columns ProjectionExtract will emit for a variable with the
/// given requirement: 1 (source passthrough or Construct) + appended property/
/// labels/type columns.
static size_t columnCountFor(const VariableRequirement& r) {
    size_t n = 1;
    if (!r.need_whole_vertex && !r.need_whole_edge) {
        n += r.vertex_props.size() + r.edge_props.size();
        if (r.need_vertex_labels)
            ++n;
        if (r.need_edge_type)
            ++n;
    }
    return n;
}

/// Tracks which variables each Project/Aggregate operator actually resets.
/// Keyed by operator pointer. Used by rewriteOp to distinguish a true schema
/// reset (which requires switching to producer-side positions for the child
/// subtree) from an inherited reset (an inner Project already reset the var;
/// the outer Project's items and child should keep using its own input
/// positions).
using ProjectResetMap = std::unordered_map<const void*, std::unordered_map<std::string, uint32_t>>;

/// Tracks the left child's output column count for each LeftJoin operator.
/// In updateBaseCols, the right sub-plan's base_col values include the left
/// column count as an offset (because col_count advances through left before
/// right). The physical right sub-plan execution sees only its own columns,
/// so rewriteOp must subtract this offset when descending into the right
/// sub-plan — otherwise BoundColumnRef column_index values point past the
/// sub-plan's actual column count.
using LeftJoinColMap = std::unordered_map<const void*, uint32_t>;

void updateBaseCols(const binder::BoundLogicalOperator& op,
                    std::unordered_map<std::string, PropertyExtractionInfo>& info, const PlanRequirements& reqs,
                    uint32_t& col_count, ProjectResetMap& project_resets, LeftJoinColMap& left_join_cols) {
    auto assignVar = [&](const std::string& var) {
        auto it = info.find(var);
        if (it != info.end()) {
            // base_col tracks the variable's output-side position at this point
            // in the traversal. Per-Project input positions are captured in
            // project_resets at the moment of each reset (not in info), so
            // sibling-subtree re-emission of an already-bound variable (e.g.
            // LeftJoin's right sub-plan) does not corrupt upstream Projects.
            it->second.base_col = col_count;
        }
        auto rit = reqs.find(var);
        col_count += static_cast<uint32_t>(rit != reqs.end() ? columnCountFor(rit->second) : 1);
    };
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                assignVar(v.variable);
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                // CorrelatedSource emits one column per correlated variable.
                // In LeftJoin's right sub-plan, these columns occupy the leading
                // physical positions; downstream variables (r, b in
                // `(a)-[r]->(b)`) come after them. Without this accounting,
                // base_col for those downstream vars would be too low.
                col_count += static_cast<uint32_t>(v.variables.size());
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    updateBaseCols(v->child, info, reqs, col_count, project_resets, left_join_cols);
                    if (!v->edge_variable.empty())
                        assignVar(v->edge_variable);
                    if (!v->dst_variable.empty())
                        assignVar(v->dst_variable);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                // VarLenExpand outputs dst, optional path, optional edge list —
                // all new columns appended after the child schema. Without this
                // accounting, downstream vars get base_col=0 (default) and the
                // BoundColumnRef rewrite would collapse them onto col 0.
                if (v) {
                    updateBaseCols(v->child, info, reqs, col_count, project_resets, left_join_cols);
                    if (!v->dst_variable.empty())
                        assignVar(v->dst_variable);
                    if (!v->path_variable.empty())
                        assignVar(v->path_variable);
                    if (!v->edge_variable.empty())
                        assignVar(v->edge_variable);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                if (v) {
                    updateBaseCols(v->left, info, reqs, col_count, project_resets, left_join_cols);
                    // Both BinaryJoin and LeftJoin combine left + right output columns,
                    // so right-sub-plan base_col values include the left column count
                    // as offset. Save it for the rewrite phase to subtract.
                    left_join_cols[&*v] = col_count;
                    updateBaseCols(v->right, info, reqs, col_count, project_resets, left_join_cols);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                // UNION combines two subtrees that produce the SAME output schema.
                // Each side must be laid out independently from the same starting
                // col_count — otherwise the right side's variables get shifted by
                // the left side's column count, and BoundColumnRef rewriting points
                // at wrong columns.
                if (v) {
                    uint32_t start = col_count;
                    updateBaseCols(v->left, info, reqs, col_count, project_resets, left_join_cols);
                    uint32_t left_count = col_count;
                    col_count = start;
                    updateBaseCols(v->right, info, reqs, col_count, project_resets, left_join_cols);
                    // Both sides should produce the same column count; keep left's.
                    col_count = left_count;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                // SemiJoin: only left child columns appear in the output schema;
                // the right child is a correlated subplan with its own column space.
                // Still traverse the right child so its variables get correct
                // base_col values for the rewrite phase (expressions in the
                // Filter/WHERE of EXISTS subqueries reference these variables).
                if (v) {
                    updateBaseCols(v->left, info, reqs, col_count, project_resets, left_join_cols);
                    left_join_cols[&*v] = col_count;
                    uint32_t saved = col_count;
                    updateBaseCols(v->right, info, reqs, col_count, project_resets, left_join_cols);
                    col_count = saved;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    updateBaseCols(v->child, info, reqs, col_count, project_resets, left_join_cols);
                    uint32_t out_idx = 0;
                    std::unordered_set<std::string> reset_in_this_op;
                    for (const auto& item : v->items) {
                        std::string var;
                        if (auto* cref = std::get_if<binder::BoundColumnRef>(&item.expr))
                            var = cref->name;
                        else if (auto* vref = std::get_if<binder::BoundVariableRef>(&item.expr))
                            var = vref->name;
                        // Skip duplicate resets within the same op: a variable
                        // referenced by multiple items (e.g. `WITH b AS y, b AS z`)
                        // has ONE producer position to remember; the second
                        // iteration would otherwise record the just-overwritten
                        // base_col (= first item's out_idx) as THIS Project's
                        // input position, corrupting the items' rewrite.
                        if (!var.empty() && reset_in_this_op.find(var) == reset_in_this_op.end()) {
                            auto it = info.find(var);
                            if (it != info.end() && it->second.base_col != out_idx) {
                                // Capture THIS Project's input position for the
                                // variable BEFORE overwriting base_col. Storing
                                // per-Project (keyed by op pointer) lets nested
                                // Projects each remember their own input schema
                                // even when a sibling subtree (LeftJoin right)
                                // advances base_col between resets.
                                project_resets[&*v][var] = it->second.base_col;
                                it->second.base_col = out_idx;
                                reset_in_this_op.insert(var);
                            }
                        }
                        ++out_idx;
                    }
                    col_count = out_idx;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    updateBaseCols(v->child, info, reqs, col_count, project_resets, left_join_cols);
                    uint32_t out_idx = 0;
                    std::unordered_set<std::string> reset_in_this_op;
                    for (const auto& k : v->group_keys) {
                        std::string var;
                        if (auto* cref = std::get_if<binder::BoundColumnRef>(&k))
                            var = cref->name;
                        else if (auto* vref = std::get_if<binder::BoundVariableRef>(&k))
                            var = vref->name;
                        if (!var.empty() && reset_in_this_op.find(var) == reset_in_this_op.end()) {
                            auto it = info.find(var);
                            if (it != info.end() && it->second.base_col != out_idx) {
                                project_resets[&*v][var] = it->second.base_col;
                                it->second.base_col = out_idx;
                                reset_in_this_op.insert(var);
                            }
                        }
                        ++out_idx;
                    }
                    col_count = static_cast<uint32_t>(v->group_keys.size() + v->aggregates.size());
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>>) {
                if (v)
                    updateBaseCols(v->child, info, reqs, col_count, project_resets, left_join_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (v) {
                    updateBaseCols(v->child, info, reqs, col_count, project_resets, left_join_cols);
                    if (!v->start_var.empty() && !v->start_pre_bound)
                        assignVar(v->start_var);
                    if (v->has_relationship) {
                        if (!v->end_var.empty() && !v->end_pre_bound)
                            assignVar(v->end_var);
                        if (!v->edge_var.empty())
                            assignVar(v->edge_var);
                        if (v->path_variable)
                            assignVar(*v->path_variable);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v) {
                    updateBaseCols(v->child, info, reqs, col_count, project_resets, left_join_cols);
                    if (!v->variable.empty())
                        assignVar(v->variable);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (v) {
                    updateBaseCols(v->child, info, reqs, col_count, project_resets, left_join_cols);
                    if (!v->variable.empty())
                        assignVar(v->variable);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v) {
                    if (v->child)
                        updateBaseCols(*v->child, info, reqs, col_count, project_resets, left_join_cols);
                    if (!v->variable.empty())
                        assignVar(v->variable);
                }
            }
        },
        op);
}

void updateExtractionBaseCols(const binder::BoundLogicalOperator& op,
                              std::unordered_map<std::string, PropertyExtractionInfo>& info,
                              const PlanRequirements& reqs, ProjectResetMap* project_resets,
                              LeftJoinColMap* left_join_cols_out) {
    uint32_t col_count = 0;
    ProjectResetMap local_resets;
    ProjectResetMap& resets = project_resets ? *project_resets : local_resets;
    LeftJoinColMap local_left_join_cols;
    LeftJoinColMap& left_cols = left_join_cols_out ? *left_join_cols_out : local_left_join_cols;
    updateBaseCols(op, info, reqs, col_count, resets, left_cols);
}

bool rewriteExpression(binder::BoundExpression& expr,
                       const std::unordered_map<std::string, PropertyExtractionInfo>& info) {
    return rewriteExpr(expr, info);
}

void rewriteColumnIndicesWithResets(binder::BoundLogicalOperator& op,
                                    const std::unordered_map<std::string, PropertyExtractionInfo>& info,
                                    const ProjectResetMap& project_resets,
                                    const LeftJoinColMap* left_join_cols) {
    rewriteOp(op, info, &project_resets, left_join_cols);
}

void rewriteColumnIndices(binder::BoundLogicalOperator& op,
                          const std::unordered_map<std::string, PropertyExtractionInfo>& info) {
    ProjectResetMap empty_resets;
    rewriteOp(op, info, &empty_resets, nullptr);
}

void collectFromPlan(const binder::BoundLogicalOperator& op,
                     std::unordered_map<std::string, PropertyExtractionInfo>& info, uint32_t base_col) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                if (!v.label_prop_ids.empty()) {
                    auto& ext = info[v.variable];
                    ext.base_col = base_col;
                    for (const auto& [lid, pids] : v.label_prop_ids)
                        for (uint16_t pid : pids)
                            ext.prop_order.emplace_back(lid, pid);
                }
                base_col += 1;
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                if (!v.label_prop_ids.empty()) {
                    auto& ext = info[v.variable];
                    ext.base_col = base_col;
                    for (const auto& [lid, pids] : v.label_prop_ids)
                        for (uint16_t pid : pids)
                            ext.prop_order.emplace_back(lid, pid);
                }
                base_col += 1;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    collectFromPlan(v->child, info, base_col);
                    uint32_t after_child = base_col;
                    if (!v->edge_variable.empty()) {
                        if (!v->edge_prop_ids.empty()) {
                            auto& ext = info[v->edge_variable];
                            ext.base_col = after_child;
                            for (uint16_t pid : v->edge_prop_ids)
                                ext.prop_order.emplace_back(EdgeLabelId{0}, pid);
                        }
                        after_child += 1;
                    }
                    if (!v->dst_variable.empty()) {
                        if (!v->dst_label_prop_ids.empty()) {
                            auto& ext = info[v->dst_variable];
                            ext.base_col = after_child;
                            for (const auto& [lid, pids] : v->dst_label_prop_ids)
                                for (uint16_t pid : pids)
                                    ext.prop_order.emplace_back(lid, pid);
                        }
                        after_child += 1;
                    }
                    base_col = after_child;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            }
        },
        op);
}

std::unordered_map<std::string, PropertyExtractionInfo>
collectExtractionFromPlan(const binder::BoundLogicalOperator& root, uint32_t base_col) {
    std::unordered_map<std::string, PropertyExtractionInfo> info;
    collectFromPlan(root, info, base_col);
    return info;
}

void findWholeObj(const binder::BoundLogicalOperator& op, std::set<std::string>& result) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    for (const auto& item : v->items) {
                        if (std::holds_alternative<binder::BoundColumnRef>(item.expr)) {
                            auto k = item.result_type.kind;
                            if (k == binder::BoundTypeKind::VERTEX || k == binder::BoundTypeKind::EDGE ||
                                k == binder::BoundTypeKind::PATH)
                                result.insert(item.alias);
                        }
                    }
                    findWholeObj(v->child, result);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (v)
                    findWholeObj(v->child, result);
            }
        },
        op);
}

std::set<std::string> findWholeObjectVariables(const binder::BoundLogicalOperator& op) {
    std::set<std::string> result;
    findWholeObj(op, result);
    return result;
}

} // namespace optimizer
} // namespace eugraph
