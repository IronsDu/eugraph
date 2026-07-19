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

#include <algorithm>

namespace eugraph {
namespace optimizer {

namespace {

/// Extract the source variable name when a Project item's expression is a
/// bare forward (`WITH n` or `WITH n AS m`). Returns empty string for
/// derived expressions (`n.age + 1`, `count(*)`, etc.).
std::string forwardRefName(const binder::BoundExpression& expr) {
    if (auto* vr = std::get_if<binder::BoundVariableRef>(&expr))
        return vr->name;
    if (auto* cr = std::get_if<binder::BoundColumnRef>(&expr))
        return cr->name;
    return "";
}

/// Extract a variable name from an object expression (BoundVariableRef or
/// BoundColumnRef). Also unwraps BoundLabelCast and startNode/endNode
/// function calls. Returns empty string if not extractable.
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

/// Look up the slot for a variable name. The caller guarantees the name has
/// been registered via allocateAllSlots, so this never returns INVALID.
binder::SlotId slotForName(const NameSlotMap& name_to_slot, const std::string& name) {
    if (name.empty())
        return binder::INVALID_SLOT_ID;
    auto it = name_to_slot.find(name);
    return it != name_to_slot.end() ? it->second : binder::INVALID_SLOT_ID;
}

/// Lookup-or-allocate: ensures `name` has a slot. Used by allocateAllSlots.
binder::SlotId ensureSlot(NameSlotMap& name_to_slot, binder::SlotAllocator& alloc, const std::string& name) {
    if (name.empty())
        return binder::INVALID_SLOT_ID;
    auto it = name_to_slot.find(name);
    if (it != name_to_slot.end())
        return it->second;
    binder::SlotId sid = alloc.next();
    name_to_slot.emplace(name, sid);
    return sid;
}

/// Iterate over every child of `op`, invoking `visit(child)` once per child.
/// Unifies child traversal across the four tree-walking passes
/// (collectOpReqs / collectSourceTypesOp / lowerAliasPassthroughOp / rewriteOp)
/// so the set of "which operators have children, and how many" lives in one
/// place. Adding a new op or closing a recursion gap is a single edit instead
/// of four.
///
/// Categories:
///   - Leaves (no children): BoundScanOp, BoundLabelScanOp, BoundSingletonOp,
///     BoundCorrelatedSourceOp.
///   - One child via `op->child`: Filter, Project, Sort, Aggregate, Expand,
///     VarLenExpand, Unwind, PathBuild, Skip, Limit, Distinct, Set, Remove,
///     Delete, Merge, CreateEdge.
///   - Optional child via `op->child` (std::optional): CreateNode.
///   - Two children via `op->left` / `op->right`: BinaryJoin, LeftJoin,
///     SemiJoin, Union.
template <typename OpRef, typename Visit> void forEachChild(OpRef&& op, Visit&& visit) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (v)
                    visit(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v && v->child)
                    visit(*v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (v) {
                    visit(v->left);
                    visit(v->right);
                }
            }
            // Leaves: BoundScanOp / BoundLabelScanOp / BoundSingletonOp /
            // BoundCorrelatedSourceOp — no children.
        },
        std::forward<OpRef>(op));
}

/// Visit each direct sub-expression of `expr` in source order. The visitor
/// is called once per child with a `BoundExpression&` (or `const
/// BoundExpression&` if `expr` is const). Leaves (BoundLiteral /
/// BoundColumnRef / BoundVariableRef / BoundParameter) and the
/// property-reference family (BoundPropertyRef / BoundDynamicPropertyRef)
/// have NO children visited here — property refs carry per-visitor
/// semantics around their `object` child (e.g. collectExprReqs must NOT
/// recurse into a PropertyRef's object, since the leaf already collected
/// the prop requirement), so each visitor handles those explicitly.
///
/// Used by the three expression visitors (ensureSlotsInExpr /
/// collectExprReqs / rewriteExpr) so that "which children does variant X
/// have" lives in one place rather than three. New expression variants
/// with uniform child-recursion semantics must be added here. Scope-
/// changing variants (BoundListComprehension / All / Any / None / Single)
/// ARE visited here — visitors that need to thread per-scope context
/// (collectExprReqs's loop_var_skip) capture the context by reference and
/// update it before invoking this helper.
template <typename ExprRef, typename Visit> void forEachSubExpr(ExprRef&& expr, Visit&& visit) {
    std::visit(
        [&](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (val) {
                    visit(val->left);
                    visit(val->right);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (val)
                    visit(val->operand);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (val)
                    for (auto& a : val->args)
                        visit(a);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (val)
                    for (auto& e : val->elements)
                        visit(e);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                if (val)
                    for (auto& [k, v] : val->entries)
                        visit(v);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (val) {
                    if (val->subject)
                        visit(*val->subject);
                    for (auto& [w, t] : val->when_thens) {
                        visit(w);
                        visit(t);
                    }
                    if (val->else_expr)
                        visit(*val->else_expr);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (val)
                    visit(val->object);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                if (val) {
                    visit(val->list);
                    visit(val->index);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                if (val) {
                    visit(val->list);
                    if (val->from)
                        visit(*val->from);
                    if (val->to)
                        visit(*val->to);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                if (val) {
                    visit(val->list_expr);
                    if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>)
                        visit(val->projection);
                    if (val->where_pred)
                        visit(*val->where_pred);
                }
            }
            // Out of scope: BoundPropertyRef / BoundDynamicPropertyRef — visitors handle their `object`
            // explicitly (per-visitor semantics around the inner reference differ).
            // Leaves: BoundLiteral / BoundColumnRef / BoundVariableRef / BoundParameter — no children.
        },
        std::forward<ExprRef>(expr));
}

/// Walk a BoundExpression and ensure every variable name referenced in it
/// has a slot. Used by allocateAllSlots.
void ensureSlotsInExpr(const binder::BoundExpression& expr, NameSlotMap& name_to_slot, binder::SlotAllocator& alloc) {
    // Per-variant leaf work: ensure slots for the variable names referenced
    // at this node (object names of PropertyRef / LabelCast / FunctionCall
    // args, plus BoundVariableRef / BoundColumnRef). PropertyRef and
    // DynamicPropertyRef carry per-visitor semantics around their `object`
    // child (handled here explicitly), so forEachSubExpr skips them.
    std::visit(
        [&](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (val) {
                    std::string var = varNameFromObject(val->object);
                    if (!var.empty())
                        ensureSlot(name_to_slot, alloc, var);
                    ensureSlotsInExpr(val->object, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (val)
                    ensureSlotsInExpr(val->object, name_to_slot, alloc);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (val) {
                    for (auto& arg : val->args) {
                        std::string var = varNameFromObject(arg);
                        if (!var.empty())
                            ensureSlot(name_to_slot, alloc, var);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (val) {
                    std::string var = varNameFromObject(val->object);
                    if (!var.empty())
                        ensureSlot(name_to_slot, alloc, var);
                }
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                if (!val.name.empty())
                    ensureSlot(name_to_slot, alloc, val.name);
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                if (!val.name.empty())
                    ensureSlot(name_to_slot, alloc, val.name);
            }
        },
        expr);
    forEachSubExpr(expr, [&](const binder::BoundExpression& child) { ensureSlotsInExpr(child, name_to_slot, alloc); });
}

/// Recursive helper for allocateAllSlots. Walks the bound tree and ensures
/// every variable-producing op and every alias gets a slot.
void allocateSlotsInOp(const binder::BoundLogicalOperator& op, NameSlotMap& name_to_slot,
                       binder::SlotAllocator& alloc) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                if (!v.variable.empty())
                    ensureSlot(name_to_slot, alloc, v.variable);
            } else if constexpr (std::is_same_v<T, binder::BoundSingletonOp>) {
                // Leaf with no output columns.
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                for (const auto& var : v.variables)
                    ensureSlot(name_to_slot, alloc, var);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v) {
                    ensureSlotsInExpr(v->predicate, name_to_slot, alloc);
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    // Bottom-up: recurse into child FIRST so the input scope's
                    // names are in name_to_slot before we capture item.input_slot.
                    // Then process items in three passes:
                    //   Pass 1: ensure input scope complete + capture
                    //           item.input_slot from CURRENT (input) state.
                    //   Pass 2: allocate alias slots. Forward renames
                    //           (src_name≠alias) update name_to_slot; non-forward
                    //           (computed) aliases don't — they must not shadow
                    //           the inner-scope definition.
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                    for (auto& item : v->items)
                        ensureSlotsInExpr(item.expr, name_to_slot, alloc);
                    for (auto& item : v->items) {
                        std::string src_name = forwardRefName(item.expr);
                        item.input_slot =
                            src_name.empty() ? binder::INVALID_SLOT_ID : slotForName(name_to_slot, src_name);
                    }
                    for (auto& item : v->items) {
                        if (item.alias.empty())
                            continue;
                        std::string src_name = forwardRefName(item.expr);
                        if (!src_name.empty() && item.alias == src_name && item.input_slot != binder::INVALID_SLOT_ID) {
                            item.alias_slot = item.input_slot;
                            continue;
                        }
                        binder::SlotId fresh = alloc.next();
                        // Forward rename (WITH n AS a): update name_to_slot.
                        // Non-forward (RETURN a.id AS a): do NOT update — the
                        // inner scope's slot for 'a' must survive for
                        // downstream canonicalisation.
                        if (!src_name.empty())
                            name_to_slot[item.alias] = fresh;
                        item.alias_slot = fresh;
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v) {
                    for (auto& item : v->items)
                        ensureSlotsInExpr(item.expr, name_to_slot, alloc);
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    for (auto& k : v->group_keys)
                        ensureSlotsInExpr(k, name_to_slot, alloc);
                    for (auto& agg : v->aggregates)
                        ensureSlotsInExpr(agg.argument, name_to_slot, alloc);
                    for (const auto& name : v->output_names)
                        if (!name.empty())
                            ensureSlot(name_to_slot, alloc, name);
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    if (!v->edge_variable.empty()) {
                        binder::SlotId sid = ensureSlot(name_to_slot, alloc, v->edge_variable);
                        if (v->edge_slot_id == binder::INVALID_SLOT_ID)
                            v->planner_edge_slot_id = sid;
                    }
                    if (!v->dst_variable.empty()) {
                        binder::SlotId sid = ensureSlot(name_to_slot, alloc, v->dst_variable);
                        if (v->dst_slot_id == binder::INVALID_SLOT_ID)
                            v->planner_dst_slot_id = sid;
                    }
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v) {
                    if (!v->edge_variable.empty()) {
                        binder::SlotId sid = ensureSlot(name_to_slot, alloc, v->edge_variable);
                        if (v->edge_slot_id == binder::INVALID_SLOT_ID)
                            v->planner_edge_slot_id = sid;
                    }
                    if (!v->dst_variable.empty()) {
                        binder::SlotId sid = ensureSlot(name_to_slot, alloc, v->dst_variable);
                        if (v->dst_slot_id == binder::INVALID_SLOT_ID)
                            v->planner_dst_slot_id = sid;
                    }
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v) {
                    ensureSlotsInExpr(v->list_expr, name_to_slot, alloc);
                    if (!v->variable.empty())
                        ensureSlot(name_to_slot, alloc, v->variable);
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                if (v)
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>>) {
                if (v) {
                    for (auto& item : v->items)
                        if (item.value_expr)
                            ensureSlotsInExpr(*item.value_expr, name_to_slot, alloc);
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>>) {
                if (v)
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>>) {
                if (v)
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (v) {
                    for (const auto& [pn, e] : v->start_pending_props)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    for (const auto& [pn, e] : v->edge_pending_props)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    for (const auto& [pn, e] : v->end_pending_props)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    for (const auto& [pid, e] : v->start_prop_filters)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    for (const auto& [pid, e] : v->edge_prop_filters)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    for (const auto& [pid, e] : v->end_prop_filters)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (v) {
                    for (const auto& [pid, e] : v->properties)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    for (const auto& [pn, e] : v->pending_props)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    allocateSlotsInOp(v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v) {
                    for (const auto& [lid, props] : v->label_properties)
                        for (const auto& [pid, e] : props)
                            ensureSlotsInExpr(e, name_to_slot, alloc);
                    for (const auto& [pn, e] : v->pending_props)
                        ensureSlotsInExpr(e, name_to_slot, alloc);
                    if (v->child)
                        allocateSlotsInOp(*v->child, name_to_slot, alloc);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (v) {
                    allocateSlotsInOp(v->left, name_to_slot, alloc);
                    allocateSlotsInOp(v->right, name_to_slot, alloc);
                }
            }
        },
        op);
}

/// Walk a BoundExpression and collect requirements into `reqs` keyed by
/// canonical slot. Variable references are resolved + canonicalized via
/// `resolver`.
void collectExprReqs(const binder::BoundExpression& expr, PlanRequirements& reqs, const SlotResolver& resolver,
                     const std::string& loop_var_skip = "") {
    // Per-variant leaf work + scope-context updates. We carry
    // `current_skip` by reference so the list-comprehension family — which
    // introduces a fresh per-iteration variable — can update it before
    // forEachSubExpr recurses into the children below.
    std::string current_skip = loop_var_skip;
    std::visit(
        [&](const auto& ptr) {
            using T = std::decay_t<decltype(ptr)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!ptr)
                    return;
                std::string var = varNameFromObject(ptr->object);
                if (var.empty() || var == current_skip)
                    return;
                binder::SlotId canon = resolver.canonicalForName(var);
                if (canon == binder::INVALID_SLOT_ID)
                    return;
                bool is_edge = false;
                if (auto* vref = std::get_if<binder::BoundVariableRef>(&ptr->object))
                    is_edge = (vref->type.kind == binder::BoundTypeKind::EDGE);
                else if (auto* cref = std::get_if<binder::BoundColumnRef>(&ptr->object))
                    is_edge = (cref->type.kind == binder::BoundTypeKind::EDGE);
                auto& vr = reqs[canon];
                if (is_edge) {
                    // Multi-candidate edge property access (e.g. r.x where r's
                    // label is ambiguous across >1 edge labels): cannot resolve
                    // to a single flat column at plan time, since each row's
                    // edge may carry the property under a different label.
                    // Force whole-edge construction so the runtime evaluator
                    // does candidate iteration on EdgeValue.
                    if (ptr->candidates.size() > 1) {
                        vr.need_whole_edge = true;
                    }
                    for (const auto& cand : ptr->candidates)
                        vr.edge_props.emplace_back(EdgeLabelId{cand.label_id}, cand.prop_id);
                } else {
                    // Multi-candidate vertex property access (e.g. n.name on
                    // an unlabeled n with name present on Person, City, __anon__).
                    // The flat-column rewrite picks ONE candidate's slot, but
                    // each row's vertex only carries the property under its
                    // actual label. Force whole-vertex construction so the
                    // runtime evaluator iterates candidates on VertexValue.
                    if (ptr->candidates.size() > 1) {
                        vr.need_whole_vertex = true;
                    }
                    for (const auto& cand : ptr->candidates)
                        vr.vertex_props.emplace_back(cand.label_id, cand.prop_id);
                }
                // NB: do NOT recurse into ptr->object — that would set
                // need_whole_vertex/edge on the variable ref, which is
                // wrong for PropertyRef (the prop collection above is the
                // precise requirement).
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (!ptr)
                    return;
                std::string var = varNameFromObject(ptr->object);
                if (!var.empty() && var != current_skip) {
                    binder::SlotId canon = resolver.canonicalForName(var);
                    if (canon != binder::INVALID_SLOT_ID) {
                        // Discriminate by the OBJECT's static type, not the
                        // result type. BoundDynamicPropertyRef is also created
                        // for MAP/ANY objects (e.g. `prop.key` where prop came
                        // from UNWIND of an untyped parameter list); forcing
                        // whole-vertex materialisation on a non-graph variable
                        // allocates a bogus object_slot_id and the rewrite then
                        // overwrites the variable's slot_id to that internal
                        // slot, breaking downstream column resolution.
                        // The runtime evaluator (evalDynamicPropertyRef) handles
                        // VertexValue / EdgeValue / MapValue directly, so PE
                        // construction is only required when the object is
                        // actually a graph entity reference.
                        binder::BoundTypeKind obj_kind = binder::BoundTypeKind::ANY;
                        if (auto* vref = std::get_if<binder::BoundVariableRef>(&ptr->object))
                            obj_kind = vref->type.kind;
                        else if (auto* cref = std::get_if<binder::BoundColumnRef>(&ptr->object))
                            obj_kind = cref->type.kind;
                        if (obj_kind == binder::BoundTypeKind::EDGE || obj_kind == binder::BoundTypeKind::EDGE_KEY)
                            reqs[canon].need_whole_edge = true;
                        else if (obj_kind == binder::BoundTypeKind::VERTEX ||
                                 obj_kind == binder::BoundTypeKind::VERTEX_REF)
                            reqs[canon].need_whole_vertex = true;
                    }
                }
                collectExprReqs(ptr->object, reqs, resolver, current_skip);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (!ptr)
                    return;
                std::string fname = ptr->func_def ? ptr->func_def->name : "";
                auto reqVarIfPlan = [&](const std::string& var) -> std::string {
                    return (!var.empty() && var != current_skip) ? var : std::string{};
                };
                if (fname == "labels" && ptr->args.size() == 1) {
                    std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                    if (!var.empty()) {
                        binder::SlotId canon = resolver.canonicalForName(var);
                        if (canon != binder::INVALID_SLOT_ID) {
                            reqs[canon].need_vertex_labels = true;
                            reqs[canon].need_whole_vertex = true;
                        }
                    }
                } else if (fname == "type" && ptr->args.size() == 1) {
                    std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                    if (!var.empty()) {
                        binder::SlotId canon = resolver.canonicalForName(var);
                        if (canon != binder::INVALID_SLOT_ID) {
                            reqs[canon].need_edge_type = true;
                            reqs[canon].need_whole_edge = true;
                        }
                    }
                } else if ((fname == "properties" || fname == "keys") && ptr->args.size() == 1) {
                    std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                    if (!var.empty()) {
                        binder::SlotId canon = resolver.canonicalForName(var);
                        if (canon != binder::INVALID_SLOT_ID) {
                            auto kind = binder::BoundTypeKind::ANY;
                            if (auto* vref = std::get_if<binder::BoundVariableRef>(&ptr->args[0]))
                                kind = vref->type.kind;
                            else if (auto* cref = std::get_if<binder::BoundColumnRef>(&ptr->args[0]))
                                kind = cref->type.kind;
                            // keys()/properties() also accept plain maps — those
                            // need no materialisation (constructing a vertex out
                            // of a map column would evaluate to NULL).
                            if (kind == binder::BoundTypeKind::EDGE)
                                reqs[canon].need_whole_edge = true;
                            else if (kind != binder::BoundTypeKind::MAP)
                                reqs[canon].need_whole_vertex = true;
                        }
                    }
                } else if ((fname == "startNode" || fname == "endNode") && ptr->args.size() == 1) {
                    std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                    if (!var.empty()) {
                        binder::SlotId canon = resolver.canonicalForName(var);
                        if (canon != binder::INVALID_SLOT_ID)
                            reqs[canon].need_whole_edge = true;
                    }
                } else if ((fname == "toInteger" || fname == "toFloat" || fname == "toBoolean" ||
                            fname == "toString") &&
                           ptr->args.size() == 1) {
                    std::string var = reqVarIfPlan(varNameFromObject(ptr->args[0]));
                    if (!var.empty()) {
                        binder::SlotId canon = resolver.canonicalForName(var);
                        if (canon != binder::INVALID_SLOT_ID) {
                            auto setReqForType = [&](binder::BoundTypeKind kind) {
                                if (kind == binder::BoundTypeKind::EDGE)
                                    reqs[canon].need_whole_edge = true;
                                else if (kind == binder::BoundTypeKind::VERTEX)
                                    reqs[canon].need_whole_vertex = true;
                            };
                            if (auto* vref = std::get_if<binder::BoundVariableRef>(&ptr->args[0]))
                                setReqForType(vref->type.kind);
                            else if (auto* cref = std::get_if<binder::BoundColumnRef>(&ptr->args[0]))
                                setReqForType(cref->type.kind);
                        }
                    }
                }
                // Argument recursion is handled by forEachSubExpr below.
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (!ptr)
                    return;
                std::string var = varNameFromObject(ptr->object);
                if (!var.empty()) {
                    binder::SlotId canon = resolver.canonicalForName(var);
                    if (canon != binder::INVALID_SLOT_ID)
                        reqs[canon].need_whole_vertex = true;
                }
                // Object recursion is handled by forEachSubExpr below.
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                if (!ptr)
                    return;
                // List comprehension introduces a fresh per-iteration variable;
                // update current_skip so child collection does not flag the
                // loop variable as needing whole-object materialization.
                current_skip = ptr->variable;
                // Child recursion is handled by forEachSubExpr below.
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                if (!ptr.name.empty() && ptr.name != current_skip) {
                    binder::SlotId canon = resolver.canonicalForName(ptr.name);
                    if (canon != binder::INVALID_SLOT_ID) {
                        if (ptr.type.kind == binder::BoundTypeKind::EDGE)
                            reqs[canon].need_whole_edge = true;
                        else if (ptr.type.kind == binder::BoundTypeKind::VERTEX)
                            reqs[canon].need_whole_vertex = true;
                    }
                }
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                if (!ptr.name.empty() && ptr.name != current_skip) {
                    binder::SlotId canon = resolver.canonicalForName(ptr.name);
                    if (canon != binder::INVALID_SLOT_ID) {
                        if (ptr.type.kind == binder::BoundTypeKind::EDGE)
                            reqs[canon].need_whole_edge = true;
                        else if (ptr.type.kind == binder::BoundTypeKind::VERTEX)
                            reqs[canon].need_whole_vertex = true;
                    }
                }
            }
        },
        expr);
    forEachSubExpr(expr,
                   [&](const binder::BoundExpression& child) { collectExprReqs(child, reqs, resolver, current_skip); });
}

/// Walk the BoundLogicalOperator tree and accumulate requirements. Keyed by
/// canonical slot (variable refs canonicalized through resolver).
void collectOpReqs(const binder::BoundLogicalOperator& op, PlanRequirements& reqs, const SlotResolver& resolver) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                if (!v.variable.empty()) {
                    binder::SlotId canon = resolver.canonicalForName(v.variable);
                    if (canon != binder::INVALID_SLOT_ID) {
                        for (const auto& [lid, pids] : v.label_prop_ids)
                            for (uint16_t pid : pids)
                                reqs[canon].vertex_props.emplace_back(lid, pid);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v)
                    collectExprReqs(v->predicate, reqs, resolver);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    for (const auto& item : v->items) {
                        if (auto* cref = std::get_if<binder::BoundColumnRef>(&item.expr)) {
                            if (cref->type.kind == binder::BoundTypeKind::VERTEX && !cref->name.empty()) {
                                binder::SlotId canon = resolver.canonicalForName(cref->name);
                                if (canon != binder::INVALID_SLOT_ID)
                                    reqs[canon].need_whole_vertex = true;
                            } else if (cref->type.kind == binder::BoundTypeKind::EDGE && !cref->name.empty()) {
                                binder::SlotId canon = resolver.canonicalForName(cref->name);
                                if (canon != binder::INVALID_SLOT_ID)
                                    reqs[canon].need_whole_edge = true;
                            }
                        }
                        collectExprReqs(item.expr, reqs, resolver);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v) {
                    for (const auto& item : v->items)
                        collectExprReqs(item.expr, reqs, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    for (const auto& k : v->group_keys)
                        collectExprReqs(k, reqs, resolver);
                    for (const auto& agg : v->aggregates)
                        collectExprReqs(agg.argument, reqs, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    if (!v->edge_variable.empty() && !v->edge_prop_ids.empty() && !v->edge_label_ids.empty()) {
                        binder::SlotId canon = resolver.canonicalForName(v->edge_variable);
                        if (canon != binder::INVALID_SLOT_ID)
                            for (uint16_t pid : v->edge_prop_ids)
                                reqs[canon].edge_props.emplace_back(v->edge_label_ids[0], pid);
                    }
                    if (!v->dst_variable.empty()) {
                        binder::SlotId canon = resolver.canonicalForName(v->dst_variable);
                        if (canon != binder::INVALID_SLOT_ID) {
                            for (const auto& [lid, pids] : v->dst_label_prop_ids)
                                for (uint16_t pid : pids)
                                    reqs[canon].vertex_props.emplace_back(lid, pid);
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v && !v->dst_variable.empty()) {
                    binder::SlotId canon = resolver.canonicalForName(v->dst_variable);
                    if (canon != binder::INVALID_SLOT_ID) {
                        for (const auto& [lid, pids] : v->dst_label_prop_ids)
                            for (uint16_t pid : pids)
                                reqs[canon].vertex_props.emplace_back(lid, pid);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v)
                    collectExprReqs(v->list_expr, reqs, resolver);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>>) {
                if (v) {
                    for (const auto& item : v->items) {
                        if (!item.target_variable.empty()) {
                            binder::SlotId canon = resolver.canonicalForName(item.target_variable);
                            if (canon != binder::INVALID_SLOT_ID) {
                                reqs[canon].need_whole_vertex = true;
                                reqs[canon].need_whole_edge = true;
                            }
                        }
                        if (item.value_expr)
                            collectExprReqs(*item.value_expr, reqs, resolver);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>>) {
                if (v) {
                    for (const auto& item : v->items) {
                        if (!item.target_variable.empty()) {
                            binder::SlotId canon = resolver.canonicalForName(item.target_variable);
                            if (canon != binder::INVALID_SLOT_ID) {
                                reqs[canon].need_whole_vertex = true;
                                reqs[canon].need_whole_edge = true;
                            }
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>>) {
                if (v) {
                    for (const auto& t : v->targets) {
                        if (!t.variable_name.empty()) {
                            binder::SlotId canon = resolver.canonicalForName(t.variable_name);
                            if (canon != binder::INVALID_SLOT_ID) {
                                if (t.kind == binder::BoundDeleteOp::TargetKind::EDGE)
                                    reqs[canon].need_whole_edge = true;
                                else
                                    reqs[canon].need_whole_vertex = true;
                            }
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (v) {
                    auto setWhole = [&](const std::string& var, bool is_edge) {
                        if (var.empty())
                            return;
                        binder::SlotId canon = resolver.canonicalForName(var);
                        if (canon == binder::INVALID_SLOT_ID)
                            return;
                        if (is_edge)
                            reqs[canon].need_whole_edge = true;
                        else
                            reqs[canon].need_whole_vertex = true;
                    };
                    // Always mark MERGE variables as needing whole-object
                    // (pre-bound or not).  MERGE's physical op emits
                    // VertexValue/EdgeValue for every variable — pre-bound
                    // ones arrive from upstream, others are built by
                    // createNode/createEdge/buildVertexValue — and
                    // downstream RETURN/WHERE property refs must resolve
                    // through the covering (object-slot) path.
                    setWhole(v->start_var, false);
                    if (v->has_relationship) {
                        setWhole(v->end_var, false);
                        setWhole(v->edge_var, true);
                    }
                    for (const auto& [prop_name, expr] : v->start_pending_props)
                        collectExprReqs(expr, reqs, resolver);
                    for (const auto& [prop_name, expr] : v->edge_pending_props)
                        collectExprReqs(expr, reqs, resolver);
                    for (const auto& [prop_name, expr] : v->end_pending_props)
                        collectExprReqs(expr, reqs, resolver);
                    for (const auto& [prop_id, expr] : v->start_prop_filters)
                        collectExprReqs(expr, reqs, resolver);
                    for (const auto& [prop_id, expr] : v->edge_prop_filters)
                        collectExprReqs(expr, reqs, resolver);
                    for (const auto& [prop_id, expr] : v->end_prop_filters)
                        collectExprReqs(expr, reqs, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (v) {
                    for (const auto& [prop_id, expr] : v->properties)
                        collectExprReqs(expr, reqs, resolver);
                    for (const auto& [prop_name, expr] : v->pending_props)
                        collectExprReqs(expr, reqs, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v) {
                    for (const auto& [lid, props] : v->label_properties)
                        for (const auto& [prop_id, expr] : props)
                            collectExprReqs(expr, reqs, resolver);
                    for (const auto& [prop_name, expr] : v->pending_props)
                        collectExprReqs(expr, reqs, resolver);
                }
            }
            // Leaves (Scan/LabelScan/Singleton/CorrelatedSource) and pure-passthrough
            // ops (PathBuild/Skip/Limit/Distinct, BinaryJoin/LeftJoin/SemiJoin/Union)
            // have no per-op requirements; their children are visited below.
        },
        op);

    forEachChild(op, [&](const auto& child) { collectOpReqs(child, reqs, resolver); });
}

void dedupeVarReqs(VariableRequirement& r) {
    auto dedupe = [](auto& vec) {
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
    };
    dedupe(r.vertex_props);
    dedupe(r.edge_props);
}

void collectSourceTypesOp(const binder::BoundLogicalOperator& op, SourceTypes& types, const SlotResolver& resolver) {
    auto setKind = [&](const std::string& var, binder::BoundTypeKind kind) {
        if (var.empty())
            return;
        binder::SlotId canon = resolver.canonicalForName(var);
        if (canon == binder::INVALID_SLOT_ID)
            return;
        types[canon] = kind;
    };
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundLabelScanOp> || std::is_same_v<T, binder::BoundScanOp>) {
                setKind(v.variable, binder::BoundTypeKind::VERTEX_REF);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    setKind(v->edge_variable, binder::BoundTypeKind::EDGE_KEY);
                    setKind(v->dst_variable, binder::BoundTypeKind::VERTEX_REF);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v)
                    setKind(v->dst_variable, binder::BoundTypeKind::VERTEX_REF);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v)
                    setKind(v->variable, binder::BoundTypeKind::VERTEX);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (v)
                    setKind(v->variable, binder::BoundTypeKind::EDGE);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (v) {
                    // Only set source type for variables this MERGE actually
                    // CREATES. Pre-bound variables keep their source type from
                    // the upstream operator (e.g. Scan → VERTEX_REF). Marking a
                    // pre-bound VERTEX_REF as VERTEX would suppress the
                    // ConstructVertex op in buildExtractionInfo.
                    if (!v->start_pre_bound)
                        setKind(v->start_var, binder::BoundTypeKind::VERTEX);
                    if (v->has_relationship)
                        setKind(v->edge_var, binder::BoundTypeKind::EDGE);
                    if (!v->end_pre_bound)
                        setKind(v->end_var, binder::BoundTypeKind::VERTEX);
                }
            }
            // Other ops don't introduce new variables; their children are visited below.
        },
        op);

    forEachChild(op, [&](const auto& child) { collectSourceTypesOp(child, types, resolver); });
}

/// Look up the slot_id for a (label_id, prop_id) pair in a PEPlan.
binder::SlotId findVertexPropSlot(const PEPlan& plan, LabelId lid, uint16_t pid) {
    for (size_t i = 0; i < plan.prop_order.size(); ++i) {
        if (plan.prop_order[i].first == lid && plan.prop_order[i].second == pid)
            return plan.prop_slot_ids[i];
    }
    return binder::INVALID_SLOT_ID;
}

binder::SlotId findEdgePropSlot(const PEPlan& plan, EdgeLabelId elid, uint16_t pid) {
    for (size_t i = 0; i < plan.edge_prop_order.size(); ++i) {
        if (plan.edge_prop_order[i].first == elid && plan.edge_prop_order[i].second == pid)
            return plan.edge_prop_slot_ids[i];
    }
    return binder::INVALID_SLOT_ID;
}

/// Rewrite a BoundExpression, replacing BoundPropertyRef with BoundColumnRef
/// referencing the slot_id of the corresponding flat property column (or, in
/// the covering case, leaving BoundPropertyRef intact but pointing its inner
/// BoundColumnRef at the appended object slot).
bool rewriteExpr(binder::BoundExpression& expr, const PEPlans& plans, const SlotResolver& resolver) {
    auto planForName = [&](const std::string& name) -> const PEPlan* {
        binder::SlotId canon = resolver.canonicalForName(name);
        if (canon == binder::INVALID_SLOT_ID)
            return nullptr;
        auto it = plans.find(canon);
        return it != plans.end() ? &it->second : nullptr;
    };
    /// Prefer the bind-time slot_id carried on the BoundColumnRef, falling
    /// back to name-based lookup. The bind-time slot is scope-stable.
    auto planForSlot = [&](binder::SlotId slot, const std::string& name) -> const PEPlan* {
        if (slot != binder::INVALID_SLOT_ID) {
            binder::SlotId canon = resolver.canonicalOf(slot);
            if (canon != binder::INVALID_SLOT_ID) {
                auto it = plans.find(canon);
                if (it != plans.end())
                    return &it->second;
            }
        }
        return planForName(name);
    };
    bool changed = std::visit(
        [&](auto& val) -> bool {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!val)
                    return false;
                std::string var = varNameFromObject(val->object);
                if (var.empty())
                    return false;
                binder::SlotId bind_slot = binder::INVALID_SLOT_ID;
                if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object))
                    bind_slot = cref->slot_id;
                const PEPlan* pi = planForSlot(bind_slot, var);
                if (!pi)
                    return false;

                bool is_edge = false;
                if (auto* vref = std::get_if<binder::BoundVariableRef>(&val->object))
                    is_edge = (vref->type.kind == binder::BoundTypeKind::EDGE);
                else if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object))
                    is_edge = (cref->type.kind == binder::BoundTypeKind::EDGE);

                // Covering case: object column exists → rewrite the inner
                // BoundColumnRef/BoundVariableRef to point at the object slot,
                // and keep the BoundPropertyRef wrapper so the runtime
                // evaluator extracts the property out of the constructed
                // VertexValue/EdgeValue.
                if (pi->object_slot_id != binder::INVALID_SLOT_ID) {
                    bool changed_inner = false;
                    if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object)) {
                        if (cref->slot_id != pi->object_slot_id) {
                            cref->slot_id = pi->object_slot_id;
                            cref->column_index = pi->object_slot_id;
                            changed_inner = true;
                        }
                    } else if (auto* vref = std::get_if<binder::BoundVariableRef>(&val->object)) {
                        val->object = binder::BoundColumnRef{0, vref->type, vref->name, pi->object_slot_id};
                        changed_inner = true;
                    }
                    return changed_inner;
                }

                // Flat-column case: pick the matching candidate slot and
                // replace the whole BoundPropertyRef with a BoundColumnRef.
                if (is_edge) {
                    for (const auto& cand : val->candidates) {
                        binder::SlotId sid = findEdgePropSlot(*pi, EdgeLabelId{cand.label_id}, cand.prop_id);
                        if (sid != binder::INVALID_SLOT_ID) {
                            expr = binder::BoundColumnRef{0, cand.type, var, sid};
                            return true;
                        }
                    }
                } else {
                    for (const auto& cand : val->candidates) {
                        binder::SlotId sid = findVertexPropSlot(*pi, cand.label_id, cand.prop_id);
                        if (sid != binder::INVALID_SLOT_ID) {
                            expr = binder::BoundColumnRef{0, cand.type, var, sid};
                            return true;
                        }
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (!val)
                    return false;
                bool changed_inner = false;
                std::string var = varNameFromObject(val->object);
                if (!var.empty()) {
                    binder::SlotId bind_slot = binder::INVALID_SLOT_ID;
                    if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object))
                        bind_slot = cref->slot_id;
                    const PEPlan* pi = planForSlot(bind_slot, var);
                    if (pi && pi->object_slot_id != binder::INVALID_SLOT_ID) {
                        if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object)) {
                            if (cref->slot_id != pi->object_slot_id) {
                                cref->slot_id = pi->object_slot_id;
                                cref->column_index = pi->object_slot_id;
                                changed_inner = true;
                            }
                        } else if (auto* vref = std::get_if<binder::BoundVariableRef>(&val->object)) {
                            val->object = binder::BoundColumnRef{0, vref->type, vref->name, pi->object_slot_id};
                            changed_inner = true;
                        }
                    }
                }
                changed_inner |= rewriteExpr(val->object, plans, resolver);
                return changed_inner;
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                const PEPlan* pi = planForSlot(binder::INVALID_SLOT_ID, val.name);
                if (!pi)
                    return false;
                binder::SlotId target =
                    pi->object_slot_id != binder::INVALID_SLOT_ID ? pi->object_slot_id : pi->source_slot_id;
                if (target == binder::INVALID_SLOT_ID)
                    return false;
                expr = binder::BoundColumnRef{0, val.type, val.name, target};
                return true;
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                const PEPlan* pi = planForSlot(val.slot_id, val.name);
                if (!pi)
                    return false;
                binder::SlotId target =
                    pi->object_slot_id != binder::INVALID_SLOT_ID ? pi->object_slot_id : pi->source_slot_id;
                if (target == binder::INVALID_SLOT_ID || target == val.slot_id)
                    return false;
                val.slot_id = target;
                val.column_index = target;
                return true;
            }
            // Variants with only recursion (no leaf rewrite) fall through;
            // their children are visited by forEachSubExpr below.
            return false;
        },
        expr);
    forEachSubExpr(expr, [&](binder::BoundExpression& child) { changed |= rewriteExpr(child, plans, resolver); });
    return changed;
}

void rewriteOp(binder::BoundLogicalOperator& op, const PEPlans& plans, const SlotResolver& resolver) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v)
                    rewriteExpr(v->predicate, plans, resolver);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    for (auto& item : v->items)
                        rewriteExpr(item.expr, plans, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v) {
                    for (auto& item : v->items)
                        rewriteExpr(item.expr, plans, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    for (auto& k : v->group_keys)
                        rewriteExpr(k, plans, resolver);
                    for (auto& agg : v->aggregates)
                        rewriteExpr(agg.argument, plans, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v)
                    rewriteExpr(v->list_expr, plans, resolver);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>>) {
                // SET items' value_expr may reference entity variables (e.g.
                // `SET r = a` where a is a vertex). Rewrite those BoundColumnRefs
                // to point at the ConstructVertex object slot so the runtime
                // sees a fully-built VertexValue/EdgeValue, not a topology-form
                // VertexRef/EdgeKey.
                if (v) {
                    for (auto& item : v->items) {
                        if (item.value_expr)
                            rewriteExpr(*item.value_expr, plans, resolver);
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                // ON CREATE / ON MATCH SET items have the same need as BoundSetOp.
                if (v) {
                    auto rewriteItems = [&](std::vector<binder::BoundSetOp::SetItem>& items) {
                        for (auto& item : items) {
                            if (item.value_expr)
                                rewriteExpr(*item.value_expr, plans, resolver);
                        }
                    };
                    rewriteItems(v->on_create_items);
                    rewriteItems(v->on_match_items);
                    // Property filters / pending props may reference bound
                    // variables (e.g. MERGE (x {name: a.name})) whose props
                    // were lowered to flat extract slots.
                    for (auto& [pid, e] : v->start_prop_filters)
                        rewriteExpr(e, plans, resolver);
                    for (auto& [pid, e] : v->edge_prop_filters)
                        rewriteExpr(e, plans, resolver);
                    for (auto& [pid, e] : v->end_prop_filters)
                        rewriteExpr(e, plans, resolver);
                    for (auto& [pn, e] : v->start_pending_props)
                        rewriteExpr(e, plans, resolver);
                    for (auto& [pn, e] : v->edge_pending_props)
                        rewriteExpr(e, plans, resolver);
                    for (auto& [pn, e] : v->end_pending_props)
                        rewriteExpr(e, plans, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                // CREATE (x {name: c.name + '0'}) — property value expressions
                // reference matched variables whose props were lowered to
                // flat extract slots.
                if (v) {
                    for (auto& [lid, props] : v->label_properties)
                        for (auto& [pid, e] : props)
                            rewriteExpr(e, plans, resolver);
                    for (auto& [pn, e] : v->pending_props)
                        rewriteExpr(e, plans, resolver);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (v) {
                    for (auto& [pid, e] : v->properties)
                        rewriteExpr(e, plans, resolver);
                    for (auto& [pn, e] : v->pending_props)
                        rewriteExpr(e, plans, resolver);
                }
            }
            // Write ops (Remove/Delete) and pure-passthrough ops
            // (PathBuild/Skip/Limit/Distinct, Expand/VarLenExpand,
            // BinaryJoin/LeftJoin/SemiJoin/Union) don't need expr rewriting at
            // this op; their children are visited below. Leaves (Scan /
            // LabelScan / Singleton / CorrelatedSource) are unmodified.
        },
        op);

    forEachChild(op, [&](auto& child) { rewriteOp(child, plans, resolver); });
}

/// Append a passthrough item `BoundColumnRef(slot=S) AS __alias_<Y>_<kind>`
/// to the given Project. Also register the alias name → slot in name_to_slot.
///
/// The appended BoundColumnRef is deliberately ANONYMOUS (empty name): the
/// subsequent rewriteColumnIndices pass re-targets any *named* BoundColumnRef
/// to its variable's canonical/source slot, which would clobber the derived
/// slot we just wired up. An empty name resolves to no PEPlan, so the ref is
/// left untouched and the derived slot survives into the Project's output.
void appendAliasPassthroughItem(binder::BoundProjectOp& proj, const std::string& alias_name, binder::SlotId source_slot,
                                const binder::BoundType& result_type, NameSlotMap& name_to_slot) {
    binder::BoundProjectOp::ProjectItem item;
    item.expr = binder::BoundColumnRef{0, result_type, "", source_slot};
    item.alias = alias_name;
    item.result_type = result_type;
    item.output_slot = source_slot;
    name_to_slot[alias_name] = source_slot;
    proj.items.push_back(std::move(item));
}

/// Check whether the immediate child of an operator is a fresh Expand
/// (re-)introducing `name`. Returns its planner slot or INVALID_SLOT_ID.
/// Only the direct child is checked — nested Expands belong to a different
/// scope and must not shadow the outer fresh binding (§6.2).
binder::SlotId findFreshExpandSlot(const binder::BoundLogicalOperator& op, const std::string& name) {
    binder::SlotId found = binder::INVALID_SLOT_ID;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v) {
                    if (v->dst_variable == name && v->planner_dst_slot_id != binder::INVALID_SLOT_ID)
                        found = v->planner_dst_slot_id;
                    if (v->edge_variable == name && v->planner_edge_slot_id != binder::INVALID_SLOT_ID)
                        found = v->planner_edge_slot_id;
                }
            }
        },
        op);
    return found;
}

/// Recursive helper for lowerAliasPassthrough. Walks the tree and mutates
/// each Project containing alias items.
void lowerAliasPassthroughOp(binder::BoundLogicalOperator& op, const PEPlans& plans, NameSlotMap& name_to_slot,
                             AliasSlotMap& alias_map, binder::SlotAllocator& alloc) {
    // lowerAliasPassthrough both reads (canonical lookup) and writes
    // (allocate fresh alias slots, register name→slot). Construct a local
    // SlotResolver over the maps for read paths; write paths continue to
    // mutate the maps directly.
    SlotResolver resolver(name_to_slot, alias_map);
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (!v)
                    return;
                // Compute the output slot for each original item, and append
                // flat-derived passthrough columns for forwarded variables.
                //
                // items[i].output_slot must equal the slot the item's
                // *rewritten* expression will produce, so the Project's
                // physical layout is consistent with the data it emits:
                //   - forward (item.expr is a bare variable/column ref X):
                //     rewrite will promote X to its object slot when whole-object
                //     demand was raised, else leave it at the source slot. We
                //     mirror that choice here so downstream refs (covering
                //     X.name, RETURN X) resolve against the right column.
                //   - derived (computed expr / property alias): the output slot
                //     is the binder's alias slot; the column holds the computed
                //     value under that fresh slot.
                //
                // A "forward" is a bare BoundVariableRef OR BoundColumnRef — the
                // binder emits `WITH n` as a BoundColumnRef and `WITH n AS m` as
                // a BoundVariableRef, so both must be recognised. We also carry
                // the ref's static type: PEPlan-based promotion / flat-append
                // only applies when the forwarded column is a graph entity.
                // When a projection alias SHADOWS a graph variable with a scalar
                // (`WITH a.name AS a`), the binder reuses the variable's slot,
                // so the slot has a stale PEPlan; the type guard prevents us
                // from wrongly re-materialising the old node's columns.
                auto forwardRef =
                    [](const binder::BoundExpression& e) -> std::pair<std::string, binder::BoundTypeKind> {
                    if (auto* vr = std::get_if<binder::BoundVariableRef>(&e))
                        return {vr->name, vr->type.kind};
                    if (auto* cr = std::get_if<binder::BoundColumnRef>(&e))
                        return {cr->name, cr->type.kind};
                    return {"", binder::BoundTypeKind::ANY};
                };
                auto isGraphKind = [](binder::BoundTypeKind k) {
                    return k == binder::BoundTypeKind::VERTEX || k == binder::BoundTypeKind::EDGE ||
                           k == binder::BoundTypeKind::VERTEX_REF || k == binder::BoundTypeKind::EDGE_KEY;
                };
                size_t original_count = v->items.size();
                for (size_t i = 0; i < original_count; ++i) {
                    auto [fwd, fwd_kind] = forwardRef(v->items[i].expr);
                    if (!fwd.empty()) {
                        // When a direct child Expand reintroduces `fwd`,
                        // use its planner slot. Otherwise prefer the
                        // per-item input_slot captured during allocation,
                        // falling back to name-based lookup (§6.2).
                        binder::SlotId fresh_expand = findFreshExpandSlot(v->child, fwd);
                        binder::SlotId canon;
                        if (fresh_expand != binder::INVALID_SLOT_ID)
                            canon = resolver.canonicalOf(fresh_expand);
                        else if (v->items[i].input_slot != binder::INVALID_SLOT_ID)
                            canon = resolver.canonicalOf(v->items[i].input_slot);
                        else
                            canon = resolver.canonicalForName(fwd);
                        const PEPlan* pi = nullptr;
                        if (isGraphKind(fwd_kind) && canon != binder::INVALID_SLOT_ID) {
                            auto it = plans.find(canon);
                            if (it != plans.end())
                                pi = &it->second;
                        }
                        binder::SlotId out_slot;
                        if (pi && pi->object_slot_id != binder::INVALID_SLOT_ID) {
                            out_slot = pi->object_slot_id;
                        } else if (pi) {
                            out_slot = canon;
                        } else {
                            binder::SlotId alias_own = v->items[i].alias_slot;
                            out_slot = alias_own != binder::INVALID_SLOT_ID ? alias_own : canon;
                        }
                        v->items[i].output_slot = out_slot;
                        // When canon came from a fresh Expand, also rewrite
                        // the BoundExpression's slot_id so rewriteExpr
                        // finds the correct PEPlan via planForSlot (§6.2).
                        if (fresh_expand != binder::INVALID_SLOT_ID) {
                            if (auto* bcr = std::get_if<binder::BoundColumnRef>(&v->items[i].expr))
                                bcr->slot_id = fresh_expand;
                            else if (auto* bvr = std::get_if<binder::BoundVariableRef>(&v->items[i].expr))
                                v->items[i].expr =
                                    binder::BoundColumnRef{0, bvr->type, bvr->name, fresh_expand};
                        }
                        continue;
                    }
                    // Derived column: keep the binder's alias slot.
                    binder::SlotId alias_slot = v->items[i].alias_slot;
                    if (alias_slot == binder::INVALID_SLOT_ID) {
                        alias_slot = alloc.next();
                        if (!v->items[i].alias.empty())
                            name_to_slot[v->items[i].alias] = alias_slot;
                    }
                    v->items[i].output_slot = alias_slot;
                }

                // Append flat-derived passthrough columns for forwarded
                // variables (props / labels / type). Only the FLAT case needs
                // this: when a whole-object column exists, downstream covering
                // reads properties out of the object slot (already carried by
                // the forward item's output slot), so no flat columns exist.
                // appendAliasPassthroughItem sets item.output_slot directly.
                for (size_t i = 0; i < original_count; ++i) {
                    auto [fwd, fwd_kind] = forwardRef(v->items[i].expr);
                    if (fwd.empty() || !isGraphKind(fwd_kind))
                        continue;
                    binder::SlotId fresh_expand = findFreshExpandSlot(v->child, fwd);
                    binder::SlotId canon;
                    if (fresh_expand != binder::INVALID_SLOT_ID)
                        canon = resolver.canonicalOf(fresh_expand);
                    else if (v->items[i].input_slot != binder::INVALID_SLOT_ID)
                        canon = resolver.canonicalOf(v->items[i].input_slot);
                    else
                        canon = resolver.canonicalForName(fwd);
                    if (canon == binder::INVALID_SLOT_ID)
                        continue;
                    auto it = plans.find(canon);
                    if (it == plans.end())
                        continue;
                    const PEPlan& pi = it->second;
                    if (pi.object_slot_id != binder::INVALID_SLOT_ID)
                        continue; // covering — flat slots don't exist
                    const std::string tag = v->items[i].alias.empty() ? fwd : v->items[i].alias;
                    for (size_t j = 0; j < pi.prop_order.size(); ++j) {
                        binder::SlotId pslot = pi.prop_slot_ids[j];
                        appendAliasPassthroughItem(*v,
                                                   "__fwd_" + tag + "_v_" + std::to_string(pi.prop_order[j].first) +
                                                       "_" + std::to_string(pi.prop_order[j].second),
                                                   pslot, binder::BoundType::Any(), name_to_slot);
                    }
                    for (size_t j = 0; j < pi.edge_prop_order.size(); ++j) {
                        binder::SlotId pslot = pi.edge_prop_slot_ids[j];
                        appendAliasPassthroughItem(*v,
                                                   "__fwd_" + tag + "_e_" +
                                                       std::to_string(pi.edge_prop_order[j].first) + "_" +
                                                       std::to_string(pi.edge_prop_order[j].second),
                                                   pslot, binder::BoundType::Any(), name_to_slot);
                    }
                    if (pi.labels_slot_id != binder::INVALID_SLOT_ID) {
                        appendAliasPassthroughItem(*v, "__fwd_" + tag + "_labels", pi.labels_slot_id,
                                                   binder::BoundType::List(binder::BoundType::String()), name_to_slot);
                    }
                    if (pi.type_slot_id != binder::INVALID_SLOT_ID) {
                        appendAliasPassthroughItem(*v, "__fwd_" + tag + "_type", pi.type_slot_id,
                                                   binder::BoundType::String(), name_to_slot);
                    }
                }
            }
            // Non-Project ops have no per-op work; their children (which may
            // contain Projects) are visited below. Leaves (Scan / LabelScan /
            // Singleton / CorrelatedSource) have no children.
        },
        op);

    forEachChild(op, [&](auto& child) { lowerAliasPassthroughOp(child, plans, name_to_slot, alias_map, alloc); });
}

void collectAliasSlotMapOp(const binder::BoundLogicalOperator& op, AliasSlotMap& alias_map,
                           const NameSlotMap& name_to_slot) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    for (const auto& item : v->items) {
                        // A rename `X AS Y` may be bound as either a
                        // BoundVariableRef (fresh alias) or a BoundColumnRef
                        // (forward of an existing column). Both establish an
                        // alias slot → source slot mapping.
                        std::string src_name = forwardRefName(item.expr);
                        if (src_name.empty() || item.alias.empty() || item.alias == src_name)
                            continue;
                        // Read the per-item input_slot / alias_slot captured by
                        // allocateSlotsInOp rather than consulting the global
                        // name_to_slot. The global map reflects the outermost
                        // scope's state and would form cycles (§13.12.1).
                        if (item.input_slot == binder::INVALID_SLOT_ID || item.alias_slot == binder::INVALID_SLOT_ID)
                            continue;
                        alias_map.emplace(item.alias_slot, item.input_slot);
                    }
                    collectAliasSlotMapOp(v->child, alias_map, name_to_slot);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v)
                    collectAliasSlotMapOp(v->child, alias_map, name_to_slot);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v && v->child)
                    collectAliasSlotMapOp(*v->child, alias_map, name_to_slot);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (v) {
                    collectAliasSlotMapOp(v->left, alias_map, name_to_slot);
                    collectAliasSlotMapOp(v->right, alias_map, name_to_slot);
                }
            }
        },
        op);
}

} // namespace

NameSlotMap buildFreshExpandMap(const binder::BoundLogicalOperator& root) {
    NameSlotMap fresh;
    auto walk = [&](auto& self, const binder::BoundLogicalOperator& op) -> void {
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>> ||
                             std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                    if (v) {
                        if (v->planner_edge_slot_id != binder::INVALID_SLOT_ID)
                            fresh[v->edge_variable] = v->planner_edge_slot_id;
                        if (v->planner_dst_slot_id != binder::INVALID_SLOT_ID)
                            fresh[v->dst_variable] = v->planner_dst_slot_id;
                    }
                }
                forEachChild(op, [&](const auto& child) { self(self, child); });
            },
            op);
    };
    walk(walk, root);
    return fresh;
}

void allocateAllSlots(const binder::BoundLogicalOperator& root, NameSlotMap& name_to_slot,
                      binder::SlotAllocator& alloc) {
    allocateSlotsInOp(root, name_to_slot, alloc);
}

AliasSlotMap collectAliasSlotMap(const binder::BoundLogicalOperator& root, const NameSlotMap& name_to_slot) {
    AliasSlotMap alias_map;
    collectAliasSlotMapOp(root, alias_map, name_to_slot);
    return alias_map;
}

binder::SlotId getCanonicalSlot(const AliasSlotMap& alias_map, binder::SlotId slot) {
    if (slot == binder::INVALID_SLOT_ID)
        return slot;
    binder::SlotId cur = slot;
    for (size_t hop = 0; hop < 64; ++hop) {
        auto it = alias_map.find(cur);
        if (it == alias_map.end())
            return cur;
        cur = it->second;
        if (cur == slot) // cycle — defensive; should not happen
            return slot;
    }
    return cur;
}

PlanRequirements collectPlanRequirements(const binder::BoundLogicalOperator& root, const SlotResolver& resolver,
                                         const NameSlotMap* fresh_expands) {
    PlanRequirements reqs;
    collectOpReqs(root, reqs, resolver);
    // When a name was reintroduced by a descendant Expand after WITH
    // aliasing, canonicalForName follows the alias chain to a different
    // slot. The fresh binding must have its own PEPlan so RETURN-level
    // consumers get a Construct column from the correct source (§6.2).
    if (fresh_expands) {
        for (const auto& [name, slot] : *fresh_expands) {
            binder::SlotId canon = resolver.canonicalForName(name);
            if (canon != binder::INVALID_SLOT_ID && canon != slot) {
                auto it = reqs.find(canon);
                if (it != reqs.end() && !it->second.empty())
                    reqs[slot] = it->second;
            }
        }
    }
    for (auto& [slot, r] : reqs)
        dedupeVarReqs(r);
    return reqs;
}

SourceTypes collectSourceTypes(const binder::BoundLogicalOperator& root, const SlotResolver& resolver,
                               const NameSlotMap* fresh_expands) {
    SourceTypes types;
    collectSourceTypesOp(root, types, resolver);
    if (fresh_expands) {
        for (const auto& [name, slot] : *fresh_expands) {
            binder::SlotId canon = resolver.canonicalForName(name);
            if (canon != binder::INVALID_SLOT_ID && canon != slot) {
                auto it = types.find(canon);
                if (it != types.end())
                    types[slot] = it->second;
            }
        }
    }
    return types;
}

PEPlans buildExtractionInfo(const PlanRequirements& reqs, const SourceTypes& source_types, const SlotResolver& resolver,
                            binder::SlotAllocator& alloc) {
    (void)resolver; // currently unused; reserved for future diagnostic / name lookups
    PEPlans plans;
    for (const auto& [slot, r] : reqs) {
        if (r.empty())
            continue;

        PEPlan pi;
        pi.source_slot_id = slot;

        if (r.need_whole_vertex || r.need_whole_edge) {
            auto st = source_types.find(slot);
            bool source_already_object = st != source_types.end() && (st->second == binder::BoundTypeKind::VERTEX ||
                                                                      st->second == binder::BoundTypeKind::EDGE);
            if (source_already_object) {
                pi.object_slot_id = pi.source_slot_id;
            } else {
                pi.object_slot_id = alloc.nextInternal();
            }
            plans[slot] = std::move(pi);
            continue;
        }

        pi.prop_order = r.vertex_props;
        pi.prop_slot_ids.reserve(pi.prop_order.size());
        for (size_t i = 0; i < pi.prop_order.size(); ++i)
            pi.prop_slot_ids.push_back(alloc.nextInternal());

        pi.edge_prop_order = r.edge_props;
        pi.edge_prop_slot_ids.reserve(pi.edge_prop_order.size());
        for (size_t i = 0; i < pi.edge_prop_order.size(); ++i)
            pi.edge_prop_slot_ids.push_back(alloc.nextInternal());

        if (r.need_vertex_labels)
            pi.labels_slot_id = alloc.nextInternal();
        if (r.need_edge_type)
            pi.type_slot_id = alloc.nextInternal();

        plans[slot] = std::move(pi);
    }
    return plans;
}

void lowerAliasPassthrough(binder::BoundLogicalOperator& root, const PEPlans& plans, NameSlotMap& name_to_slot,
                           AliasSlotMap& alias_map, binder::SlotAllocator& alloc) {
    lowerAliasPassthroughOp(root, plans, name_to_slot, alias_map, alloc);
}

bool rewriteExpression(binder::BoundExpression& expr, const PEPlans& plans, const SlotResolver& resolver) {
    return rewriteExpr(expr, plans, resolver);
}

void rewriteColumnIndices(binder::BoundLogicalOperator& op, const PEPlans& plans, const SlotResolver& resolver) {
    rewriteOp(op, plans, resolver);
}

} // namespace optimizer
} // namespace eugraph
