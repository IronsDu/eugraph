#include "query/optimizer/rules/logical/pattern_comprehension_anchor_share.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/optimizer/framework/operator_eq.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_expression/bound_expression_walk.hpp"
#include "query/planner/bound_logical_plan.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_fork_op.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace optimizer {

namespace {

using binder::BoundLogicalOperator;

bool debugEnabled() {
    static const bool enabled = std::getenv("EUGRAPH_ANCHOR_SHARE_DEBUG") != nullptr;
    return enabled;
}

} // namespace

#define AS_DEBUG(...)                                                                                                  \
    do {                                                                                                               \
        if (::eugraph::optimizer::debugEnabled())                                                                      \
            spdlog::info("[anchor-share] " __VA_ARGS__);                                                               \
    } while (0)

namespace {

using binder::BoundLogicalOperator;

// ============================================================
// Structural helpers
// ============================================================

std::vector<BoundLogicalOperator*> childRefs(BoundLogicalOperator& op) {
    std::vector<BoundLogicalOperator*> out;
    forEachLogicalChild(op, [&out](BoundLogicalOperator& child) { out.push_back(&child); });
    return out;
}

std::vector<const BoundLogicalOperator*> childRefs(const BoundLogicalOperator& op) {
    std::vector<const BoundLogicalOperator*> out;
    forEachLogicalChild(op, [&out](const BoundLogicalOperator& child) { out.push_back(&child); });
    return out;
}

/// The single child slot of a unary operator, or nullptr.
BoundLogicalOperator* singleChild(BoundLogicalOperator& op) {
    auto refs = childRefs(op);
    return refs.size() == 1 ? refs.front() : nullptr;
}

/// Follow the single-child chain below `root` (inclusive). Stops at the first
/// operator with zero or several children.
std::vector<BoundLogicalOperator*> spineOf(BoundLogicalOperator& root) {
    std::vector<BoundLogicalOperator*> spine;
    BoundLogicalOperator* node = &root;
    for (;;) {
        spine.push_back(node);
        BoundLogicalOperator* next = singleChild(*node);
        if (!next)
            break;
        node = next;
    }
    return spine;
}

/// True when the two plans are structurally identical (operator payload and the
/// whole child subtree). `equalBoundLogicalOperator` compares payloads only, so
/// this adds the recursion the memo deliberately leaves out.
bool planTreesEqualImpl(const BoundLogicalOperator& a, const BoundLogicalOperator& b, std::string& why, int depth) {
    if (a.index() != b.index()) {
        why = "type index " + std::to_string(a.index()) + " vs " + std::to_string(b.index());
        return false;
    }
    if (!equalBoundLogicalOperator(a, b)) {
        why = "payload differs at depth " + std::to_string(depth) + " node_index=" + std::to_string(a.index());
        if (auto* ea = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&a)) {
            auto* eb = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&b);
            if (ea && eb && *ea && *eb) {
                why += " [Expand src=" + (*ea)->src_variable + "/" + (*eb)->src_variable +
                       " src_slot=" + std::to_string((*ea)->src_slot_id) + "/" + std::to_string((*eb)->src_slot_id) +
                       " dst=" + (*ea)->dst_variable + "/" + (*eb)->dst_variable +
                       " dst_slot=" + std::to_string((*ea)->dst_slot_id) + "/" + std::to_string((*eb)->dst_slot_id) +
                       " edge_slot=" + std::to_string((*ea)->edge_slot_id) + "/" + std::to_string((*eb)->edge_slot_id) +
                       "]";
            }
        } else if (auto* ua = std::get_if<std::unique_ptr<binder::BoundUnwindOp>>(&a)) {
            auto* ub = std::get_if<std::unique_ptr<binder::BoundUnwindOp>>(&b);
            if (ua && ub && *ua && *ub) {
                why += " [Unwind var=" + (*ua)->variable + "/" + (*ub)->variable +
                       " slot=" + std::to_string((*ua)->variable_slot_id) + "/" +
                       std::to_string((*ub)->variable_slot_id) + "]";
            }
        } else if (std::holds_alternative<binder::BoundCorrelatedSourceOp>(a)) {
            const auto& ca = std::get<binder::BoundCorrelatedSourceOp>(a);
            const auto& cb = std::get<binder::BoundCorrelatedSourceOp>(b);
            why += " [CS vars=" + std::to_string(ca.variables.size()) + "/" + std::to_string(cb.variables.size());
            for (size_t i = 0; i < std::min(ca.slot_ids.size(), cb.slot_ids.size()); ++i)
                why += " " + std::to_string(ca.slot_ids[i]) + "/" + std::to_string(cb.slot_ids[i]);
            why += "]";
        }
        return false;
    }
    auto ca = childRefs(a);
    auto cb = childRefs(b);
    if (ca.size() != cb.size()) {
        why = "child count differs";
        return false;
    }
    for (size_t i = 0; i < ca.size(); ++i) {
        if (!planTreesEqualImpl(*ca[i], *cb[i], why, depth + 1))
            return false;
    }
    return true;
}

bool planTreesEqual(const BoundLogicalOperator& a, const BoundLogicalOperator& b) {
    std::string why;
    const bool equal = planTreesEqualImpl(a, b, why, 0);
    if (!equal)
        AS_DEBUG("plan mismatch: {}", why);
    return equal;
}

/// The shared suffix must contain the correlated traversal (an Expand or a
/// var-length Expand), not merely a vertex scan: sharing a single fetch is not
/// worth a spool, and it would fire on pairs that share nothing but their start
/// vertex.
bool containsSharedTraversal(const BoundLogicalOperator& plan) {
    bool found = false;
    std::function<void(const BoundLogicalOperator&)> visit = [&](const BoundLogicalOperator& node) {
        if (found)
            return;
        if (std::holds_alternative<std::unique_ptr<binder::BoundExpandOp>>(node) ||
            std::holds_alternative<std::unique_ptr<binder::BoundVarLenExpandOp>>(node)) {
            found = true;
            return;
        }
        for (const auto* child : childRefs(node))
            visit(*child);
    };
    visit(plan);
    return found;
}

// ============================================================
// Renaming the generated variables of one comprehension onto the other
// ============================================================

struct NameMap {
    std::unordered_map<std::string, std::string> names; // other -> canonical
    std::unordered_map<binder::SlotId, binder::SlotId> slots;
    // ScopeId is provenance only (see AGENTS.md: never a second identity
    // source), but the content comparator includes it, so equivalent bindings
    // from two sibling comprehensions must be mapped as well.
    std::unordered_map<uint32_t, uint32_t> scopes;
};

/// Collect slot -> scope from every column reference of a plan subtree.
void collectSlotScopes(const binder::BoundLogicalOperator& op, std::unordered_map<binder::SlotId, uint32_t>& out) {
    std::function<void(const binder::BoundExpression&)> walk = [&](const binder::BoundExpression& x) {
        if (auto* r = std::get_if<binder::BoundColumnRef>(&x)) {
            if (r->slot_id != binder::INVALID_SLOT_ID && r->scope_id != binder::INVALID_SCOPE_ID &&
                out.find(static_cast<binder::SlotId>(r->slot_id)) == out.end())
                out[static_cast<binder::SlotId>(r->slot_id)] = r->scope_id;
        }
        if (auto* p = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&x)) {
            if (*p)
                walk((*p)->object);
        } else if (auto* d = std::get_if<std::unique_ptr<binder::BoundDynamicPropertyRef>>(&x)) {
            if (*d)
                walk((*d)->object);
        } else if (auto* c = std::get_if<std::unique_ptr<binder::BoundLabelCast>>(&x)) {
            if (*c)
                walk((*c)->object);
        }
        binder::forEachSubExpr(x, walk);
    };
    std::function<void(const binder::BoundLogicalOperator&)> visit = [&](const binder::BoundLogicalOperator& node) {
        if (auto* f = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&node)) {
            if (*f)
                walk((*f)->predicate);
        } else if (auto* pr = std::get_if<std::unique_ptr<binder::BoundProjectOp>>(&node)) {
            if (*pr)
                for (const auto& item : (*pr)->items)
                    walk(item.expr);
        } else if (auto* agg = std::get_if<std::unique_ptr<binder::BoundAggregateOp>>(&node)) {
            if (*agg) {
                for (const auto& key : (*agg)->group_keys)
                    walk(key);
                for (const auto& item : (*agg)->aggregates)
                    for (const auto& arg : item.arguments)
                        walk(arg);
            }
        } else if (auto* uw = std::get_if<std::unique_ptr<binder::BoundUnwindOp>>(&node)) {
            if (*uw)
                walk((*uw)->list_expr);
        }
        for (const auto* child : childRefs(node))
            visit(*child);
    };
    visit(op);
}

void renameExpression(binder::BoundExpression& expr, const NameMap& map) {
    if (auto* ref = std::get_if<binder::BoundColumnRef>(&expr)) {
        auto it = map.names.find(ref->name);
        if (it != map.names.end())
            ref->name = it->second;
        auto sit = map.slots.find(static_cast<binder::SlotId>(ref->slot_id));
        if (sit != map.slots.end())
            ref->slot_id = sit->second;
        auto scit = map.scopes.find(ref->scope_id);
        if (scit != map.scopes.end())
            ref->scope_id = scit->second;
    } else if (auto* vref = std::get_if<binder::BoundVariableRef>(&expr)) {
        auto it = map.names.find(vref->name);
        if (it != map.names.end())
            vref->name = it->second;
    }
    // Property references carry their object expression outside forEachSubExpr.
    if (auto* pref = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&expr)) {
        if (*pref)
            renameExpression((*pref)->object, map);
    } else if (auto* dref = std::get_if<std::unique_ptr<binder::BoundDynamicPropertyRef>>(&expr)) {
        if (*dref)
            renameExpression((*dref)->object, map);
    } else if (auto* cast = std::get_if<std::unique_ptr<binder::BoundLabelCast>>(&expr)) {
        if (*cast)
            renameExpression((*cast)->object, map);
    }
    binder::forEachSubExpr(expr, [&map](binder::BoundExpression& child) { renameExpression(child, map); });
}

void renameVars(std::string& name, binder::SlotId& slot, const NameMap& map) {
    auto it = map.names.find(name);
    if (it != map.names.end())
        name = it->second;
    auto sit = map.slots.find(slot);
    if (sit != map.slots.end())
        slot = sit->second;
}

/// Rename the operator's own payload (expressions, variable names, slots) and
/// recurse into every child.
void renameSubtree(BoundLogicalOperator& op, const NameMap& map) {
    std::visit(
        [&](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (val)
                    renameExpression(val->predicate, map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (val) {
                    for (auto& item : val->items) {
                        renameExpression(item.expr, map);
                        auto it = map.names.find(item.alias);
                        if (it != map.names.end())
                            item.alias = it->second;
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (val) {
                    for (auto& key : val->group_keys)
                        renameExpression(key, map);
                    for (auto& agg : val->aggregates)
                        for (auto& arg : agg.arguments)
                            renameExpression(arg, map);
                    for (auto& name : val->output_names) {
                        auto it = map.names.find(name);
                        if (it != map.names.end())
                            name = it->second;
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (val)
                    for (auto& item : val->items)
                        renameExpression(item.expr, map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (val) {
                    renameExpression(val->list_expr, map);
                    renameVars(val->variable, val->variable_slot_id, map);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (val) {
                    renameVars(val->src_variable, val->src_slot_id, map);
                    renameVars(val->dst_variable, val->dst_slot_id, map);
                    renameVars(val->edge_variable, val->edge_slot_id, map);
                    renameVars(val->edge_variable, val->planner_edge_slot_id, map);
                    renameVars(val->dst_variable, val->planner_dst_slot_id, map);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (val) {
                    // VLE carries no src slot (the source is resolved by name).
                    auto it = map.names.find(val->src_variable);
                    if (it != map.names.end())
                        val->src_variable = it->second;
                    renameVars(val->dst_variable, val->dst_slot_id, map);
                    renameVars(val->edge_variable, val->edge_slot_id, map);
                    renameVars(val->path_variable, val->path_slot_id, map);
                }
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                for (auto& name : val.variables) {
                    auto it = map.names.find(name);
                    if (it != map.names.end())
                        name = it->second;
                }
                for (auto& slot : val.slot_ids) {
                    auto sit = map.slots.find(slot);
                    if (sit != map.slots.end())
                        slot = sit->second;
                }
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp> ||
                                 std::is_same_v<T, binder::BoundLabelScanOp>) {
                auto it = map.names.find(val.variable);
                if (it != map.names.end())
                    val.variable = it->second;
            }
        },
        op);
    forEachLogicalChild(op, [&map](BoundLogicalOperator& child) { renameSubtree(child, map); });
}

/// Pair the named fields of two aligned operators and extend `map` with
/// other -> canonical. Returns false when the shapes do not line up.
bool collectNamePairs(BoundLogicalOperator& canonical, BoundLogicalOperator& other, NameMap& map) {
    if (canonical.index() != other.index())
        return false;

    std::visit(
        [&](auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                auto& o = std::get<std::unique_ptr<binder::BoundExpandOp>>(other);
                if (!c || !o)
                    return;
                // Pair by position, not by identity: the two comprehensions
                // bind their own copy of the same pattern variables, so the
                // slots differ while the values are the same sequence.
                auto add = [&map](const std::string& from, const std::string& to, binder::SlotId from_slot,
                                  binder::SlotId to_slot) {
                    if (!from.empty() && from != to)
                        map.names[from] = to;
                    if (from_slot != binder::INVALID_SLOT_ID && to_slot != binder::INVALID_SLOT_ID &&
                        from_slot != to_slot)
                        map.slots[from_slot] = to_slot;
                };
                add(o->src_variable, c->src_variable, o->src_slot_id, c->src_slot_id);
                add(o->dst_variable, c->dst_variable, o->dst_slot_id, c->dst_slot_id);
                add(o->edge_variable, c->edge_variable, o->edge_slot_id, c->edge_slot_id);
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                auto& o = std::get<binder::BoundCorrelatedSourceOp>(other);
                const size_t n = std::min(c.variables.size(), o.variables.size());
                for (size_t i = 0; i < n; ++i) {
                    // Two comprehensions bind their own copy of the same name
                    // (e.g. the `r` of two sibling list comprehensions), so the
                    // slot identity differs even though the values do not: the
                    // pairing is positional, and the driving sub-plan equality
                    // check proves the values agree.
                    if (o.variables[i] != c.variables[i])
                        map.names[o.variables[i]] = c.variables[i];
                    const binder::SlotId os = i < o.slot_ids.size() ? o.slot_ids[i] : binder::INVALID_SLOT_ID;
                    const binder::SlotId cs = i < c.slot_ids.size() ? c.slot_ids[i] : binder::INVALID_SLOT_ID;
                    if (os != binder::INVALID_SLOT_ID && cs != binder::INVALID_SLOT_ID && os != cs)
                        map.slots[os] = cs;
                }
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp> ||
                                 std::is_same_v<T, binder::BoundLabelScanOp>) {
                auto& o = std::get<T>(other);
                if (o.variable != c.variable)
                    map.names[o.variable] = c.variable;
            }
        },
        canonical);

    auto cc = childRefs(canonical);
    auto co = childRefs(other);
    if (cc.size() != co.size())
        return false;
    for (size_t i = 0; i < cc.size(); ++i) {
        if (!collectNamePairs(*cc[i], *co[i], map))
            return false;
    }
    return true;
}

/// Replace the child slot of `tail_root`'s single-child chain that holds the
/// shared suffix root with an empty correlated-source marker, so the branch tail
/// is re-rooted on the shared anchor rows.
bool cutAtChild(BoundLogicalOperator& tail_root, BoundLogicalOperator* cut) {
    BoundLogicalOperator* node = &tail_root;
    for (;;) {
        BoundLogicalOperator* next = singleChild(*node);
        if (!next)
            return false;
        if (next == cut) {
            setChild(*node, BoundLogicalOperator(binder::BoundCorrelatedSourceOp{}));
            return true;
        }
        node = next;
    }
}

using ApplyOp = binder::BoundPatternComprehensionApplyOp;
using ApplyHolder = std::unique_ptr<ApplyOp>;

void renameCorrelation(std::vector<binder::BoundPatternComprehensionApplyOp::Correlation>& corrs, const NameMap& map) {
    for (auto& corr : corrs) {
        auto it = map.names.find(corr.left_var);
        if (it != map.names.end())
            corr.left_var = it->second;
        auto sit = map.slots.find(corr.left_slot);
        if (sit != map.slots.end())
            corr.left_slot = sit->second;
        auto rsit = map.slots.find(corr.right_slot);
        if (rsit != map.slots.end())
            corr.right_slot = rsit->second;
    }
}

/// Two comprehensions are correlated on the same outer values when they read the
/// same left variables in the same order. The right-side column slots are
/// allocated per comprehension and are irrelevant: the fork re-injects the
/// anchor rows into each branch positionally.
bool sameCorrelationSources(const std::vector<binder::BoundPatternComprehensionApplyOp::Correlation>& a,
                            const std::vector<binder::BoundPatternComprehensionApplyOp::Correlation>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].left_slot != b[i].left_slot || a[i].left_var != b[i].left_var)
            return false;
    }
    return true;
}

/// Everything the rewrite needs from one of the two comprehensions.
struct Side {
    ApplyOp* outer = nullptr;                    // list-comprehension apply
    binder::BoundAggregateOp* collect = nullptr; // outer collect (keys=0)
    ApplyOp* inner = nullptr;                    // per-r comprehension apply
    BoundLogicalOperator* right = nullptr;       // inner.right (the correlated tree)
};

bool describeSide(BoundLogicalOperator& apply_op, Side& side) {
    auto* outer = std::get_if<ApplyHolder>(&apply_op);
    if (!outer || !*outer)
        return false;
    side.outer = outer->get();
    if (side.outer->existence_only || side.outer->correlation.empty() || side.outer->outputs.size() != 1)
        return false;

    auto* collect = std::get_if<std::unique_ptr<binder::BoundAggregateOp>>(&side.outer->right);
    if (!collect || !*collect || !(*collect)->group_keys.empty() || (*collect)->aggregates.size() != 1)
        return false;
    side.collect = collect->get();

    auto* inner = std::get_if<ApplyHolder>(&side.collect->child);
    if (!inner || !*inner)
        return false;
    side.inner = inner->get();
    if (side.inner->existence_only || side.inner->correlation.empty() || side.inner->outputs.size() != 1)
        return false;
    side.right = &side.inner->right;
    return true;
}

// ============================================================
// Hoisting startNode(r).id / endNode(r).id
// ============================================================
//
// The branch predicates compare the endpoints' *property* ids against
// startNode(r).id / endNode(r).id. `startNode` synthesises a bare vertex (no
// labels, no properties), so `.id` is a dynamic property access: one store
// fetch per evaluated row (complex-14: ~77k rows per branch, ~307k lookups).
// They are constant for the whole inner execution (the correlation row's `r`),
// so a projection on top of the anchor's correlated source computes them once
// and the branches compare columns. All-or-nothing: the plan is only touched
// once every piece has been built successfully.

enum class EndpointKind {
    None,
    Start,
    End
};

/// Recognise `startNode(r).id` / `endNode(r).id` in its bound form: a dynamic
/// property access on the vertex a topology function synthesises.
EndpointKind endpointOf(const binder::BoundExpression& expr, std::string& var_name, binder::SlotId& var_slot) {
    auto* dyn = std::get_if<std::unique_ptr<binder::BoundDynamicPropertyRef>>(&expr);
    if (!dyn || !*dyn || (*dyn)->property != "id")
        return EndpointKind::None;
    auto* fn = std::get_if<std::unique_ptr<binder::BoundFunctionCall>>(&(*dyn)->object);
    if (!fn || !*fn || !(*fn)->func_def || (*fn)->args.size() != 1)
        return EndpointKind::None;
    const std::string& fname = (*fn)->func_def->name;
    if (fname != "startNode" && fname != "endNode")
        return EndpointKind::None;
    auto* arg = std::get_if<binder::BoundColumnRef>(&(*fn)->args[0]);
    if (!arg)
        return EndpointKind::None;
    var_name = arg->name;
    var_slot = static_cast<binder::SlotId>(arg->slot_id);
    return fname == "startNode" ? EndpointKind::Start : EndpointKind::End;
}

/// Declared type of the column a topology function reads (`r` in startNode(r)).
binder::BoundType endpointArgumentType(const binder::BoundExpression& expr) {
    auto* dyn = std::get_if<std::unique_ptr<binder::BoundDynamicPropertyRef>>(&expr);
    if (!dyn || !*dyn)
        return binder::BoundType::Any();
    auto* fn = std::get_if<std::unique_ptr<binder::BoundFunctionCall>>(&(*dyn)->object);
    if (!fn || !*fn || (*fn)->args.size() != 1)
        return binder::BoundType::Any();
    if (auto* arg = std::get_if<binder::BoundColumnRef>(&(*fn)->args[0]))
        return binder::BoundType::clone(arg->type);
    return binder::BoundType::Any();
}

/// Depth-first search for the anchor's correlated source, recording its owner.
BoundLogicalOperator* findCorrelatedSource(BoundLogicalOperator& root, BoundLogicalOperator** parent_out) {
    if (std::holds_alternative<binder::BoundCorrelatedSourceOp>(root))
        return &root;
    for (auto* child : childRefs(root)) {
        if (auto* found = findCorrelatedSource(*child, parent_out)) {
            // First assignment on the way up is the immediate owner: ancestors
            // must not overwrite it.
            if (parent_out && *parent_out == nullptr)
                *parent_out = &root;
            return found;
        }
    }
    return nullptr;
}

/// Does `parent` own `child` in one of its child slots?
bool parentHoldsChild(BoundLogicalOperator& parent, const BoundLogicalOperator* child) {
    for (auto* slot : childRefs(parent))
        if (slot == child)
            return true;
    return false;
}

/// Replace the child slot of `parent` that holds `old_child`.
bool replaceChild(BoundLogicalOperator& parent, const BoundLogicalOperator* old_child,
                  BoundLogicalOperator replacement) {
    bool replaced = false;
    std::visit(
        [&](auto& val) {
            using T = std::decay_t<decltype(val)>;
            auto try_slot = [&](auto& slot) {
                if (!replaced && &slot == old_child) {
                    slot = std::move(replacement);
                    replaced = true;
                }
            };
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (val) {
                    try_slot(val->left);
                    try_slot(val->right);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>>) {
                if (val) {
                    try_slot(val->child);
                    try_slot(val->body);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionForkOp>>) {
                if (val) {
                    try_slot(val->left);
                    try_slot(val->anchor);
                    for (auto& branch : val->branches)
                        try_slot(branch);
                }
            } else if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                                 std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                                 std::is_same_v<T, binder::BoundScanOp> ||
                                 std::is_same_v<T, binder::BoundLabelScanOp> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundCallOp>>) {
                // leaves
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (val && val->child)
                    try_slot(*val->child);
            } else {
                if (val)
                    try_slot(val->child);
            }
        },
        parent);
    return replaced;
}

/// Replace `startNode(r).id` / `endNode(r).id` occurrences with column refs.
size_t substituteEndpointIds(binder::BoundExpression& expr, binder::SlotId start_slot, binder::SlotId end_slot,
                             const binder::BoundType& start_type, const binder::BoundType& end_type,
                             const std::string& start_name, const std::string& end_name) {
    std::string var_name;
    binder::SlotId var_slot = binder::INVALID_SLOT_ID;
    switch (endpointOf(expr, var_name, var_slot)) {
    case EndpointKind::Start:
        expr = binder::BoundExpression(binder::BoundColumnRef(0, start_type, start_name, start_slot));
        return 1;
    case EndpointKind::End:
        expr = binder::BoundExpression(binder::BoundColumnRef(0, end_type, end_name, end_slot));
        return 1;
    case EndpointKind::None:
        break;
    }
    size_t replaced = 0;
    binder::forEachSubExpr(expr, [&](binder::BoundExpression& child) {
        replaced += substituteEndpointIds(child, start_slot, end_slot, start_type, end_type, start_name, end_name);
    });
    return replaced;
}

bool hoistEndpointIds(binder::BoundPatternComprehensionForkOp& fork, binder::SlotAllocator& alloc,
                      std::unordered_map<std::string, binder::SlotId>& name_to_slot) {
    // The endpoints are computed on the fork's *left* pipeline (the unwind of
    // rels_in_path), never inside the anchor: inserting a projection between the
    // anchor's correlated source and its vertex scan would defeat the
    // CorrelatedVertexProbe rewrite and cost far more than it saves.
    auto* unwind = std::get_if<std::unique_ptr<binder::BoundUnwindOp>>(&fork.left);
    if (!unwind || !*unwind)
        return false;
    auto* left_source = std::get_if<binder::BoundCorrelatedSourceOp>(&(*unwind)->child);
    if (!left_source)
        return false;
    const std::string unwind_var = (*unwind)->variable;
    binder::SlotId unwind_slot = (*unwind)->variable_slot_id;

    // ---- Phase 1: discover (no mutation) ----
    std::string r_name;
    binder::SlotId r_slot = binder::INVALID_SLOT_ID;
    bool have_start = false;
    bool have_end = false;
    binder::BoundType start_type = binder::BoundType::Int64();
    binder::BoundType end_type = binder::BoundType::Int64();
    binder::BoundExpression start_expr;
    binder::BoundExpression end_expr;
    binder::BoundType r_type = binder::BoundType::Any();
    for (auto& branch : fork.branches) {
        std::function<void(binder::BoundExpression&)> scan = [&](binder::BoundExpression& expr) {
            std::string var_name;
            binder::SlotId var_slot = binder::INVALID_SLOT_ID;
            EndpointKind kind = endpointOf(expr, var_name, var_slot);
            if (kind == EndpointKind::Start && !have_start) {
                start_expr = cloneBoundExpression(expr);
                have_start = true;
                r_name = var_name;
                r_slot = var_slot;
                r_type = endpointArgumentType(expr);
                return;
            }
            if (kind == EndpointKind::End && !have_end) {
                end_expr = cloneBoundExpression(expr);
                have_end = true;
                r_name = var_name;
                r_slot = var_slot;
                if (r_type.kind == binder::BoundTypeKind::ANY)
                    r_type = endpointArgumentType(expr);
                return;
            }
            binder::forEachSubExpr(expr, scan);
        };
        std::function<void(binder::BoundLogicalOperator&)> walk = [&](binder::BoundLogicalOperator& node) {
            if (auto* filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&node)) {
                if (*filter)
                    scan((*filter)->predicate);
            }
            for (auto* child : childRefs(node))
                walk(*child);
        };
        walk(branch);
    }
    if (!have_start && !have_end)
        return false;
    if (r_name != unwind_var)
        return false;

    // The anchor has to consume the two new columns as well.
    BoundLogicalOperator* anchor_parent = nullptr;
    BoundLogicalOperator* anchor_source_node = findCorrelatedSource(fork.anchor, &anchor_parent);
    if (!anchor_source_node)
        return false;
    auto& anchor_source = std::get<binder::BoundCorrelatedSourceOp>(*anchor_source_node);

    // ---- Phase 2: build (the anchored plan is still untouched) ----
    const binder::SlotId start_slot = alloc.next();
    const binder::SlotId end_slot = alloc.next();
    const std::string start_name = "__pe_start_id_" + std::to_string(start_slot);
    const std::string end_name = "__pe_end_id_" + std::to_string(end_slot);

    auto project = std::make_unique<binder::BoundProjectOp>();
    auto add_passthrough = [&](const std::string& name, const binder::BoundType& type, binder::SlotId slot,
                               uint32_t index) {
        binder::BoundProjectOp::ProjectItem item;
        item.expr = binder::BoundExpression(binder::BoundColumnRef(index, type, name, slot));
        item.alias = name;
        item.result_type = type;
        item.output_slot = slot;
        project->items.push_back(std::move(item));
    };
    for (size_t i = 0; i < left_source->variables.size(); ++i) {
        const binder::SlotId slot =
            i < left_source->slot_ids.size() ? left_source->slot_ids[i] : binder::INVALID_SLOT_ID;
        add_passthrough(left_source->variables[i],
                        i < left_source->types.size() ? left_source->types[i] : binder::BoundType::Any(), slot,
                        static_cast<uint32_t>(i));
    }
    add_passthrough(unwind_var, r_type, unwind_slot, static_cast<uint32_t>(left_source->variables.size()));
    if (have_start) {
        binder::BoundProjectOp::ProjectItem item;
        item.expr = std::move(start_expr);
        item.alias = start_name;
        item.result_type = start_type;
        item.output_slot = start_slot;
        project->items.push_back(std::move(item));
    }
    if (have_end) {
        binder::BoundProjectOp::ProjectItem item;
        item.expr = std::move(end_expr);
        item.alias = end_name;
        item.result_type = end_type;
        item.output_slot = end_slot;
        project->items.push_back(std::move(item));
    }

    // ---- Phase 3: commit ----
    project->child = std::move(fork.left);
    fork.left = BoundLogicalOperator(std::move(project));
    if (have_start) {
        anchor_source.variables.push_back(start_name);
        anchor_source.types.push_back(start_type);
        anchor_source.column_indices.push_back(static_cast<uint32_t>(anchor_source.variables.size() - 1));
        anchor_source.slot_ids.push_back(start_slot);
        name_to_slot[start_name] = start_slot;
    }
    if (have_end) {
        anchor_source.variables.push_back(end_name);
        anchor_source.types.push_back(end_type);
        anchor_source.column_indices.push_back(static_cast<uint32_t>(anchor_source.variables.size() - 1));
        anchor_source.slot_ids.push_back(end_slot);
        name_to_slot[end_name] = end_slot;
    }
    for (auto& branch : fork.branches) {
        std::function<void(binder::BoundLogicalOperator&)> walk = [&](binder::BoundLogicalOperator& node) {
            if (auto* filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&node)) {
                if (*filter)
                    substituteEndpointIds((*filter)->predicate, start_slot, end_slot, start_type, end_type, start_name,
                                          end_name);
            }
            for (auto* child : childRefs(node))
                walk(*child);
        };
        walk(branch);
    }
    return true;
}

/// Attempt the rewrite rooted at `op` (which must itself be an apply). Returns
/// true when `op` was replaced by the merged plan.
bool tryRewrite(BoundLogicalOperator& op, binder::SlotAllocator& slot_alloc,
                std::unordered_map<std::string, binder::SlotId>& name_to_slot) {
    auto* outer_holder = std::get_if<ApplyHolder>(&op);
    if (!outer_holder || !*outer_holder)
        return false;
    AS_DEBUG("candidate: apply with left variant index {}", (*outer_holder)->left.index());

    // The two comprehensions are stacked: outer.left is the second apply.
    auto* inner_holder = std::get_if<ApplyHolder>(&(*outer_holder)->left);
    if (!inner_holder || !*inner_holder) {
        AS_DEBUG("bail: left is not a comprehension apply");
        return false;
    }

    Side side0;
    Side side1;
    if (!describeSide(op, side0)) {
        AS_DEBUG("bail: outer side is not a plain list comprehension");
        return false;
    }
    if (!describeSide((*outer_holder)->left, side1)) {
        AS_DEBUG("bail: stacked side is not a plain list comprehension");
        return false;
    }
    AS_DEBUG("pair: outer corr={} stacked corr={}", side0.outer->correlation.size(), side1.outer->correlation.size());

    ApplyOp& a0 = *side0.outer;
    ApplyOp& a1 = *side1.outer;

    // Guards: same outer correlation, plain list comprehensions, same batching.
    if (a0.correlation != a1.correlation) {
        AS_DEBUG("bail: outer correlations differ");
        return false;
    }
    if (a0.batch_enabled != a1.batch_enabled) {
        AS_DEBUG("bail: outer batching differs");
        return false;
    }
    // Same per-r correlation. The driving sub-plan (the unwind of rels_in_path)
    // is not duplicated by the fork, so both sides must agree on it; rename the
    // second comprehension's generated variables onto the first before
    // comparing, because the batch key is allocated per comprehension.
    if (side0.inner->correlation.empty() || side0.inner->batch_enabled != side1.inner->batch_enabled) {
        AS_DEBUG("bail: inner correlation/batching mismatch");
        return false;
    }

    // The two comprehensions bind their own `r` (different slots, same values:
    // both iterate the same unwound list). Rename the second comprehension's
    // generated variables onto the first — the pairing comes from the shared
    // pattern subtree, not from slot identity — and then require the driving
    // sub-plans and the correlation lists to be identical.
    NameMap map;
    if (!collectNamePairs(*side0.right, *side1.right, map)) {
        AS_DEBUG("bail: name pairing failed");
        return false;
    }
    {
        std::unordered_map<binder::SlotId, uint32_t> scope0;
        std::unordered_map<binder::SlotId, uint32_t> scope1;
        collectSlotScopes(*side0.right, scope0);
        collectSlotScopes(*side1.right, scope1);
        for (const auto& [slot_other, slot_canon] : map.slots) {
            auto it1 = scope1.find(slot_other);
            auto it0 = scope0.find(slot_canon);
            if (it1 != scope1.end() && it0 != scope0.end() && it1->second != it0->second)
                map.scopes[it1->second] = it0->second;
        }
        // Variables that keep the same slot (shared outer bindings) can still be
        // referenced from different sub-scopes; map those as well.
        for (const auto& [slot, scope_other] : scope1) {
            auto it0 = scope0.find(slot);
            if (it0 != scope0.end() && it0->second != scope_other)
                map.scopes[scope_other] = it0->second;
        }
    }
    renameSubtree(*side1.right, map);
    renameSubtree(side1.inner->left, map);
    renameCorrelation(side1.inner->correlation, map);
    if (side0.inner->correlation != side1.inner->correlation) {
        AS_DEBUG("bail: inner correlations differ ({} vs {})", side0.inner->correlation.size(),
                 side1.inner->correlation.size());
        return false;
    }
    if (!planTreesEqual(side0.inner->left, side1.inner->left)) {
        AS_DEBUG("bail: driving sub-plans differ");
        return false;
    }

    auto s0 = spineOf(*side0.right);
    auto s1 = spineOf(*side1.right);
    if (s0.size() != s1.size() || s0.size() < 3) {
        AS_DEBUG("bail: spine sizes {} vs {}", s0.size(), s1.size());
        return false;
    }

    // Longest common suffix of the two spines: everything from the deepest
    // identical subtree up to the last differing operator.
    size_t common = 0;
    while (common < s0.size() && planTreesEqual(*s0[s0.size() - 1 - common], *s1[s1.size() - 1 - common]))
        ++common;
    if (common == 0) {
        AS_DEBUG("bail: no common suffix");
        return false;
    }

    BoundLogicalOperator* anchor = s0[s0.size() - common];
    BoundLogicalOperator* cut1 = s1[s1.size() - common];

    // The shared suffix must carry the actual pattern work, and both branch
    // tails must be non-trivial (otherwise the fork adds structure for nothing).
    if (!containsSharedTraversal(*anchor)) {
        AS_DEBUG("bail: common suffix has no shared traversal to amortise");
        return false;
    }
    if (anchor == side0.right || cut1 == side1.right)
        return false;
    if (common >= s0.size())
        return false;

    // Detach the shared anchor from the first comprehension and re-root both
    // tails on an empty correlated source (the planner fills in its schema).
    BoundLogicalOperator anchor_op = std::move(*anchor);
    if (!cutAtChild(*side0.right, anchor) || !cutAtChild(*side1.right, cut1))
        return false;

    // Capture the identities that must survive the merge before any move.
    const auto out0 = side0.inner->outputs[0]; // per-r list of comprehension 0
    const auto out1 = side1.inner->outputs[0]; // per-r list of comprehension 1
    const auto weight0 = a0.outputs[0];        // final weight 0
    const auto weight1 = a1.outputs[0];        // final weight 1
    const auto correlation = a0.correlation;
    const bool batch_enabled = a0.batch_enabled;
    const auto batch_var = a0.batch_var;
    const auto batch_slot = a0.batch_slot_id;
    const auto inner_batch_var = side0.inner->batch_var;
    const auto inner_batch_slot = side0.inner->batch_slot_id;
    const bool inner_batch_enabled = side0.inner->batch_enabled;
    const auto inner_correlation = side0.inner->correlation;

    auto fork = std::make_unique<binder::BoundPatternComprehensionForkOp>();
    fork->left = std::move(side0.inner->left);
    fork->anchor = std::move(anchor_op);
    fork->branches.push_back(std::move(*side0.right));
    fork->branches.push_back(std::move(*side1.right));
    fork->correlation = inner_correlation;
    fork->outputs.push_back(out0);
    fork->outputs.push_back(out1);
    fork->batch_enabled = inner_batch_enabled;
    fork->batch_var = inner_batch_var;
    fork->batch_slot_id = inner_batch_slot;

    const bool endpoint_ids_hoisted = hoistEndpointIds(*fork, slot_alloc, name_to_slot);

    // One aggregate over the fork's two per-row lists: this is the list
    // comprehension's collect, now shared.
    auto collect = std::make_unique<binder::BoundAggregateOp>();
    collect->output_names.push_back(side0.collect->output_names.empty() ? std::string("__anchor_agg_0")
                                                                        : side0.collect->output_names[0]);
    collect->output_names.push_back(side1.collect->output_names.empty() ? std::string("__anchor_agg_1")
                                                                        : side1.collect->output_names[0]);
    // AggregateItem owns its argument expressions (move-only), so the items are
    // moved out of the two collects that this rewrite is replacing.
    collect->aggregates.push_back(std::move(side0.collect->aggregates[0]));
    collect->aggregates.push_back(std::move(side1.collect->aggregates[0]));
    // The aggregate arguments are kept verbatim: they reference the per-r
    // comprehension output (which may be wrapped in the list-comprehension's
    // reduce expression), and the fork appends exactly those slots at its
    // output, so slot-based resolution lands on the right column.
    collect->child = BoundLogicalOperator(std::move(fork));

    auto merged = std::make_unique<ApplyOp>();
    merged->left = std::move(a1.left);
    merged->right = BoundLogicalOperator(std::move(collect));
    merged->correlation = correlation;
    merged->outputs.push_back(weight0);
    merged->outputs.push_back(weight1);
    merged->batch_enabled = batch_enabled;
    merged->batch_var = batch_var;
    merged->batch_slot_id = batch_slot;
    merged->existence_only = false;

    op = BoundLogicalOperator(std::move(merged));
    AS_DEBUG("rewrote two comprehensions into one shared-anchor fork (anchor suffix depth {}, "
             "{} branches, endpoints hoisted: {})",
             common, 2, endpoint_ids_hoisted);
    return true;
}

} // namespace

bool rewritePatternComprehensionAnchorShare(BoundLogicalOperator& root, binder::SlotAllocator& slot_alloc,
                                            std::unordered_map<std::string, binder::SlotId>& name_to_slot) {
    // Interleaved A/B switch: the same binary can serve both plans, so a
    // performance comparison is never confounded by a different build.
    static const bool disabled = std::getenv("EUGRAPH_ANCHOR_SHARE_OFF") != nullptr;
    if (disabled)
        return false;

    std::function<bool(BoundLogicalOperator&)> visit = [&](BoundLogicalOperator& op) -> bool {
        if (tryRewrite(op, slot_alloc, name_to_slot))
            return true;
        for (auto* child : childRefs(op)) {
            if (visit(*child))
                return true;
        }
        return false;
    };
    return visit(root);
}

} // namespace optimizer
} // namespace eugraph
