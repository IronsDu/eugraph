#include "query/optimizer/memo.hpp"

#include <algorithm>

#include "query/optimizer/operator_eq.hpp"
#include "query/optimizer/operator_hash.hpp"
#include "query/optimizer/requirement_collector.hpp"
#include "query/planner/bound_logical_plan.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_semi_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

namespace eugraph {
namespace optimizer {

// ============================================================
// Helper: setChild — rebuild child reference in a BoundLogicalOperator
// ============================================================
void setChild(binder::BoundLogicalOperator& op, binder::BoundLogicalOperator child) {
    std::visit(
        [&child](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                          std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                          std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                // Leaf operators — no child field
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                // Binary operators have left/right, not a single child — skip
            } else {
                // All non-leaf operators are wrapped in unique_ptr
                // BoundCreateNodeOp has optional<BoundLogicalOperator> child,
                // which is assignable from BoundLogicalOperator
                if (!val) {
                    // Defensive: null unique_ptr in variant (moved-from or shared group)
                    return;
                }
                val->child = std::move(child);
            }
        },
        op);
}

int getChildCount(const binder::BoundLogicalOperator& op) {
    return std::visit(
        [](const auto& val) -> int {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                          std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                          std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                return 0;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
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

// ============================================================
// Memo — global hash (Columbia: SSP::HashTbl)
// ============================================================

GroupId Memo::newGroupId() {
    auto id = next_group_id_++;
    groups_.push_back(std::make_unique<Group>(id));
    return id;
}

ExprId Memo::newExprId() {
    return next_expr_id_++;
}

uint64_t Memo::computeHash(const binder::BoundLogicalOperator& op, const std::vector<GroupId>& child_groups) const {
    // FNV-1a style hash combining op type, op content, and child group IDs.
    uint64_t h = hashBoundLogicalOperator(op);
    for (GroupId gid : child_groups) {
        h ^= gid;
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t Memo::computePhysHash(const PhysicalExpr& phys, const std::vector<GroupId>& child_groups) const {
    uint64_t h = hashPhysicalExpr(phys);
    for (GroupId gid : child_groups) {
        h ^= gid;
        h *= 1099511628211ULL;
    }
    return h;
}

void Memo::registerInHash(ExprId eid) {
    const GroupExpr& expr = getExpr(eid);
    uint64_t h =
        expr.isPhysical() ? computePhysHash(expr.physOp(), expr.child_groups) : computeHash(expr.op, expr.child_groups);
    hash_table_[h].push_back(eid);
}

void Memo::unregisterInHash(const GroupExpr& expr) {
    uint64_t h =
        expr.isPhysical() ? computePhysHash(expr.physOp(), expr.child_groups) : computeHash(expr.op, expr.child_groups);
    auto it = hash_table_.find(h);
    if (it != hash_table_.end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), expr.id), vec.end());
        if (vec.empty())
            hash_table_.erase(it);
    }
}

GroupId Memo::copyIn(binder::BoundLogicalOperator& op) {
    // Recursively insert children first (bottom-up)
    std::vector<GroupId> child_groups;
    int n_children = getChildCount(op);
    if (n_children == 1) {
        binder::BoundLogicalOperator child = std::visit(
            [](auto& val) -> binder::BoundLogicalOperator {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, binder::BoundSingletonOp> ||
                              std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                              std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                    return binder::BoundScanOp{};
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                    return binder::BoundScanOp{};
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                    auto c = std::move(val->child.value());
                    val->child = std::nullopt;
                    return c;
                } else {
                    if (!val)
                        return binder::BoundScanOp{};
                    auto c = std::move(val->child);
                    val->child = binder::BoundScanOp{};
                    return c;
                }
            },
            op);
        child_groups.push_back(copyIn(child));
    } else if (n_children == 2) {
        if (std::holds_alternative<std::unique_ptr<binder::BoundBinaryJoinOp>>(op)) {
            auto& join = std::get<std::unique_ptr<binder::BoundBinaryJoinOp>>(op);
            auto left = std::move(join->left);
            join->left = binder::BoundScanOp{};
            child_groups.push_back(copyIn(left));
            auto right = std::move(join->right);
            join->right = binder::BoundScanOp{};
            child_groups.push_back(copyIn(right));
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundSemiJoinOp>>(op)) {
            auto& sj = std::get<std::unique_ptr<binder::BoundSemiJoinOp>>(op);
            auto left = std::move(sj->left);
            sj->left = binder::BoundScanOp{};
            child_groups.push_back(copyIn(left));
            auto right = std::move(sj->right);
            sj->right = binder::BoundScanOp{};
            child_groups.push_back(copyIn(right));
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundLeftJoinOp>>(op)) {
            auto& lj = std::get<std::unique_ptr<binder::BoundLeftJoinOp>>(op);
            auto left = std::move(lj->left);
            lj->left = binder::BoundScanOp{};
            child_groups.push_back(copyIn(left));
            auto right = std::move(lj->right);
            lj->right = binder::BoundScanOp{};
            child_groups.push_back(copyIn(right));
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundUnionOp>>(op)) {
            auto& uo = std::get<std::unique_ptr<binder::BoundUnionOp>>(op);
            auto left = std::move(uo->left);
            uo->left = binder::BoundScanOp{};
            child_groups.push_back(copyIn(left));
            auto right = std::move(uo->right);
            uo->right = binder::BoundScanOp{};
            child_groups.push_back(copyIn(right));
        }
    }

    // Content-aware dedup (Columbia SSP::FindDup + OP::operator==): if an
    // equivalent expression is already in the memo, reuse its group. The
    // hash narrows the bucket; equalBoundLogicalOperator confirms structural
    // equality before we merge. Distinct operators of the same variant
    // type (e.g. two BoundLabelScanOp with different label_ids) no longer
    // collide — they hash differently and compare unequal.
    if (auto* dup = findDuplicate(op, child_groups)) {
        return dup->group_id;
    }

    // No duplicate — create a new group
    GroupId gid = newGroupId();
    ExprId eid = newExprId();
    auto expr = std::make_unique<GroupExpr>(eid, gid, std::move(op), std::move(child_groups));
    // Phase B: walk the operator once at copyIn time to derive the
    // materialization requirements for this group. The walk is on a
    // *clone* — `op` is about to be moved into the GroupExpr and we
    // need a stable reference to collect from.
    {
        VarRequirements reqs = collectOpRequirements(expr->op, catalog_);
        groups_[gid]->setRequirements(std::move(reqs));
    }
    groups_[gid]->logical_exprs.push_back(eid);
    exprs_.push_back(std::move(expr));
    registerInHash(eid);
    return gid;
}

binder::BoundLogicalOperator Memo::copyOut(GroupId root_gid) {
    Group& group = getGroup(root_gid);
    // RBO: take the last (most recently added) logical expression
    ExprId eid = group.logical_exprs.back();
    GroupExpr& expr = getExpr(eid);

    // Clone (not move) the operator out — copyOut may be invoked recursively on
    // the same group when a child group is referenced from multiple parents
    // (this is the norm once content-aware dedup is on, since Columbia-style
    // memoization canonicalizes shared subtrees into one group). Moving the
    // operator out would empty the variant for the second parent.
    binder::BoundLogicalOperator result = cloneBoundLogicalOperator(expr.op);

    // Recursively rebuild children
    if (expr.child_groups.size() == 1) {
        auto child = copyOut(expr.child_groups[0]);
        setChild(result, std::move(child));
    } else if (expr.child_groups.size() == 2) {
        if (std::holds_alternative<std::unique_ptr<binder::BoundBinaryJoinOp>>(result)) {
            auto& join = std::get<std::unique_ptr<binder::BoundBinaryJoinOp>>(result);
            join->left = copyOut(expr.child_groups[0]);
            join->right = copyOut(expr.child_groups[1]);
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundSemiJoinOp>>(result)) {
            auto& sj = std::get<std::unique_ptr<binder::BoundSemiJoinOp>>(result);
            sj->left = copyOut(expr.child_groups[0]);
            sj->right = copyOut(expr.child_groups[1]);
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundLeftJoinOp>>(result)) {
            auto& lj = std::get<std::unique_ptr<binder::BoundLeftJoinOp>>(result);
            lj->left = copyOut(expr.child_groups[0]);
            lj->right = copyOut(expr.child_groups[1]);
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundUnionOp>>(result)) {
            auto& uo = std::get<std::unique_ptr<binder::BoundUnionOp>>(result);
            uo->left = copyOut(expr.child_groups[0]);
            uo->right = copyOut(expr.child_groups[1]);
        }
    }

    return result;
}

binder::BoundLogicalOperator Memo::copyOut(GroupId root_gid, const PhysProp& prop) {
    Group& group = getGroup(root_gid);

    // CBO: try winner's circle first
    Winner* winner = group.getWinner(prop);
    if (winner && winner->done() && winner->plan() != INVALID_EXPR_ID) {
        GroupExpr& expr = getExpr(winner->plan());
        // For physical expressions, use the cloned source operator;
        // for logical expressions, use the operator directly.
        binder::BoundLogicalOperator result =
            expr.isPhysical() ? cloneBoundLogicalOperator(expr.physOp().source) : cloneBoundLogicalOperator(expr.op);

        // Children: recurse with PhysProp{} (any-property), matching
        // extractChosen's winner-chain walk. Previously this called the
        // no-prop overload, which silently fell back to RBO for subtrees
        // — making plan.root diverge from plan.chosen when winners exist.
        if (expr.child_groups.size() == 1) {
            auto child = copyOut(expr.child_groups[0], PhysProp{});
            setChild(result, std::move(child));
        } else if (expr.child_groups.size() == 2) {
            if (std::holds_alternative<std::unique_ptr<binder::BoundBinaryJoinOp>>(result)) {
                auto& join = std::get<std::unique_ptr<binder::BoundBinaryJoinOp>>(result);
                join->left = copyOut(expr.child_groups[0], PhysProp{});
                join->right = copyOut(expr.child_groups[1], PhysProp{});
            } else if (std::holds_alternative<std::unique_ptr<binder::BoundSemiJoinOp>>(result)) {
                auto& sj = std::get<std::unique_ptr<binder::BoundSemiJoinOp>>(result);
                sj->left = copyOut(expr.child_groups[0], PhysProp{});
                sj->right = copyOut(expr.child_groups[1], PhysProp{});
            } else if (std::holds_alternative<std::unique_ptr<binder::BoundLeftJoinOp>>(result)) {
                auto& lj = std::get<std::unique_ptr<binder::BoundLeftJoinOp>>(result);
                lj->left = copyOut(expr.child_groups[0], PhysProp{});
                lj->right = copyOut(expr.child_groups[1], PhysProp{});
            } else if (std::holds_alternative<std::unique_ptr<binder::BoundUnionOp>>(result)) {
                auto& uo = std::get<std::unique_ptr<binder::BoundUnionOp>>(result);
                uo->left = copyOut(expr.child_groups[0], PhysProp{});
                uo->right = copyOut(expr.child_groups[1], PhysProp{});
            }
        }
        return result;
    }

    // Fallback: RBO extraction (last logical expression)
    return copyOut(root_gid);
}

std::unique_ptr<ChosenPlan> Memo::extractChosen(GroupId root_gid, const PhysProp& prop) {
    Group& group = getGroup(root_gid);
    Winner* winner = group.getSatisfyingWinner(prop);
    if (!winner || !winner->done() || winner->plan() == INVALID_EXPR_ID) {
        return nullptr;
    }

    GroupExpr& expr = getExpr(winner->plan());
    if (!expr.isPhysical()) {
        return nullptr;
    }

    auto plan = std::make_unique<ChosenPlan>();
    plan->tag = expr.physOp().tag;
    plan->op = cloneBoundLogicalOperator(expr.physOp().source);
    plan->enrich_variable = expr.physOp().enrich_variable;
    plan->enrich_output = expr.physOp().output_mat;

    for (size_t i = 0; i < expr.child_groups.size(); ++i) {
        GroupId child_gid = expr.child_groups[i];
        // Compute the property required of this child: union of the
        // operator's per-input declaration and what the parent context
        // demands transitively. Must mirror OInputsTask::inputRequiredProp.
        VarRequirements child_mat;
        if (i < expr.physOp().required_input_mat.size())
            mergeVarRequirements(child_mat, expr.physOp().required_input_mat[i]);
        mergeVarRequirements(child_mat, prop.materializations());
        // Enricher enforcers themselves contribute their output to the
        // transitively-required materializations for their (recursive)
        // input — but Enrichers don't *require* materialization from
        // their input, they only upgrade it. So if this child is the
        // enricher's own group (recursive), require "any".
        if (child_gid == root_gid && expr.physOp().tag != PhysicalOpTag::VertexEnrich &&
            expr.physOp().tag != PhysicalOpTag::EdgeEnrich && expr.physOp().tag != PhysicalOpTag::PathEnrich &&
            expr.physOp().tag != PhysicalOpTag::VertexPropertyExtract &&
            expr.physOp().tag != PhysicalOpTag::EdgePropertyExtract &&
            expr.physOp().tag != PhysicalOpTag::PathPropertyExtract) {
            // Self-referential non-enforcer expr — unusual; leave as-is.
        }
        if (expr.physOp().tag == PhysicalOpTag::VertexEnrich || expr.physOp().tag == PhysicalOpTag::EdgeEnrich ||
            expr.physOp().tag == PhysicalOpTag::PathEnrich ||
            expr.physOp().tag == PhysicalOpTag::VertexPropertyExtract ||
            expr.physOp().tag == PhysicalOpTag::EdgePropertyExtract ||
            expr.physOp().tag == PhysicalOpTag::PathPropertyExtract) {
            // Enricher/Extract's input is topology form — require "any".
            child_mat.clear();
        }
        PhysProp child_prop;
        child_prop.setMaterializations(std::move(child_mat));
        auto child_plan = extractChosen(child_gid, child_prop);
        if (!child_plan) {
            // Fallback: try with any-prop (covers cases where the child
            // winner was registered under a less-specific property).
            child_plan = extractChosen(child_gid, PhysProp{});
            if (!child_plan)
                return nullptr;
        }
        plan->children.push_back(std::move(child_plan));
    }

    return plan;
}

GroupExpr* Memo::findDuplicate(const binder::BoundLogicalOperator& op, const std::vector<GroupId>& child_groups) {
    uint64_t h = computeHash(op, child_groups);
    auto it = hash_table_.find(h);
    if (it == hash_table_.end())
        return nullptr;
    for (ExprId eid : it->second) {
        const GroupExpr& existing = getExpr(eid);
        if (existing.isPhysical())
            continue; // skip physical exprs when looking for logical dup
        if (existing.child_groups != child_groups)
            continue;
        if (!equalBoundLogicalOperator(existing.op, op))
            continue;
        return const_cast<GroupExpr*>(&existing);
    }
    return nullptr;
}

GroupExpr* Memo::findPhysDuplicate(const PhysicalExpr& phys, const std::vector<GroupId>& child_groups) {
    uint64_t h = computePhysHash(phys, child_groups);
    auto it = hash_table_.find(h);
    if (it == hash_table_.end())
        return nullptr;
    for (ExprId eid : it->second) {
        const GroupExpr& existing = getExpr(eid);
        if (!existing.isPhysical())
            continue;
        if (existing.child_groups != child_groups)
            continue;
        if (!equalPhysicalExpr(existing.physOp(), phys))
            continue;
        return const_cast<GroupExpr*>(&existing);
    }
    return nullptr;
}

GroupId Memo::mergeGroups(GroupId g1, GroupId g2) {
    // Merge the larger group into the smaller one
    if (g1 > g2)
        std::swap(g1, g2);

    Group& to_group = getGroup(g1);
    Group& from_group = getGroup(g2);

    for (ExprId eid : from_group.logical_exprs) {
        exprs_[eid]->group_id = g1;
        to_group.logical_exprs.push_back(eid);
    }
    from_group.logical_exprs.clear();

    for (ExprId eid : from_group.physical_exprs) {
        exprs_[eid]->group_id = g1;
        to_group.physical_exprs.push_back(eid);
    }
    from_group.physical_exprs.clear();

    // Update all parent GroupExprs that reference the merged-away group (g2)
    // to now reference the surviving group (g1).
    for (auto& expr : exprs_) {
        for (GroupId& cg : expr->child_groups) {
            if (cg == g2)
                cg = g1;
        }
    }

    // Invalidate LogProp on both groups — g1 gained new expressions,
    // g2's parents_ cascade handles groups that previously derived from g2.
    to_group.changed = true;
    to_group.invalidateLogProp(*this);
    from_group.invalidateLogProp(*this);

    return g1;
}

GroupExpr* Memo::insertExpr(std::unique_ptr<GroupExpr> expr, GroupId target_group) {
    expr->group_id = target_group;
    GroupExpr* raw = expr.get();
    exprs_.push_back(std::move(expr));
    getGroup(target_group).logical_exprs.push_back(raw->id);
    registerInHash(raw->id);
    // Mark the group as changed (Columbia: Group->set_changed(true))
    getGroup(target_group).changed = true;
    // Invalidate cached LogProp (new expression may change cardinality/schema)
    getGroup(target_group).invalidateLogProp(*this);
    return raw;
}

GroupExpr* Memo::insertPhysExpr(std::unique_ptr<GroupExpr> expr, GroupId target_group) {
    expr->group_id = target_group;
    GroupExpr* raw = expr.get();
    exprs_.push_back(std::move(expr));
    getGroup(target_group).physical_exprs.push_back(raw->id);
    registerInHash(raw->id);
    // Intentionally NOT setting changed=true. The flag's contract is
    // "a new LOGICAL equivalence was added during this optimization
    // round, so O_GROUP should iterate again to apply transformation
    // rules to it." Physical exprs (impl rules, enforcer insertions)
    // don't expand the logical search space — they only physicalize
    // what's already there — so they must not trigger re-iteration.
    // Setting changed here previously caused an infinite loop: every
    // ImplXxx insertion flipped changed, ~OExprTask saw it and refused
    // to mark the group optimized, O_GROUP re-ran, the impl rule had
    // already fired (rule_mask) so no new work was pushed, but the
    // group was still optimizing=true → next O_GROUP fell through
    // without clearing, and the cycle repeated.
    return raw;
}

ExprId Memo::insertEnricherEnforcer(GroupId g, const VarRequirements& req_mat) {
    // No requirements → nothing to insert.
    bool any_needed = false;
    for (const auto& [_, req] : req_mat) {
        if (!req.empty()) {
            any_needed = true;
            break;
        }
    }
    if (!any_needed)
        return INVALID_EXPR_ID;

    Group& group = getGroup(g);
    Winner* any_w = group.getWinner(PhysProp{});
    if (!any_w || !any_w->done() || any_w->plan() == INVALID_EXPR_ID)
        return INVALID_EXPR_ID;

    const LogProp& lp = group.getLogProp(*this, catalog_);

    // Insert one enforcer per non-empty variable.
    // - need_entire → full Enricher (VertexEnrich/EdgeEnrich/PathEnrich)
    // - only need_props/need_labels → flat PropertyExtract
    ExprId last_enforcer_id = INVALID_EXPR_ID;
    for (const auto& [var, req] : req_mat) {
        if (req.empty())
            continue;

        // Mutable copy for need_entire expansion.
        MaterializationReq mut_req = req;

        // Determine the base tag from the variable's type in the schema.
        PhysicalOpTag base_vertex = PhysicalOpTag::VertexEnrich;
        PhysicalOpTag base_edge = PhysicalOpTag::EdgeEnrich;
        PhysicalOpTag base_path = PhysicalOpTag::PathEnrich;
        PhysicalOpTag base_tag = base_vertex;
        for (const auto& col : lp.columns) {
            if (col.variable != var)
                continue;
            if (col.type_kind == binder::BoundTypeKind::EDGE || col.type_kind == binder::BoundTypeKind::EDGE_KEY) {
                base_tag = base_edge;
            } else if (col.type_kind == binder::BoundTypeKind::PATH ||
                       col.type_kind == binder::BoundTypeKind::PATH_TOPOLOGY) {
                base_tag = base_path;
            }
            break;
        }

        // Choose Enricher vs PropertyExtract based on need_entire flag.
        PhysicalOpTag tag;
        if (mut_req.need_entire) {
            tag = base_tag;
            if (catalog_) {
                const auto& labels = catalog_->allLabels();
                for (const auto& [lid, ldef] : labels) {
                    for (const auto& pd : ldef.properties)
                        mut_req.need_props[lid].insert(pd.id);
                }
            }
        } else {
            // PropertyExtract: downgrade Enricher tag to the corresponding extract tag.
            if (base_tag == PhysicalOpTag::EdgeEnrich)
                tag = PhysicalOpTag::EdgePropertyExtract;
            else if (base_tag == PhysicalOpTag::PathEnrich)
                tag = PhysicalOpTag::PathPropertyExtract;
            else
                tag = PhysicalOpTag::VertexPropertyExtract;
        }

        auto phys = std::make_unique<PhysicalExpr>();
        phys->tag = tag;
        phys->enrich_variable = var;
        phys->output_mat[var] = mut_req;

        ExprId eid = newExprId();
        auto gexpr = std::make_unique<GroupExpr>(eid, g, std::move(phys), std::vector<GroupId>{g});
        GroupExpr* raw = insertPhysExpr(std::move(gexpr), g);
        last_enforcer_id = raw->id;
    }
    return last_enforcer_id;
}

GroupExpr* Memo::createGroupWithExpr(binder::BoundLogicalOperator op, std::vector<GroupId> child_groups) {
    GroupId gid = newGroupId();
    ExprId eid = newExprId();
    auto expr = std::make_unique<GroupExpr>(eid, gid, std::move(op), std::move(child_groups));
    GroupExpr* raw = expr.get();
    groups_[gid]->logical_exprs.push_back(eid);
    exprs_.push_back(std::move(expr));
    registerInHash(eid);
    getGroup(gid).changed = true;
    return raw;
}

// ============================================================
// Helper: clone a BoundSetOp::SetItem (contains optional<BoundExpression>)
// ============================================================
namespace {

binder::BoundSetOp::SetItem cloneSetItem(const binder::BoundSetOp::SetItem& item) {
    binder::BoundSetOp::SetItem ci;
    ci.kind = item.kind;
    ci.target_variable = item.target_variable;
    ci.prop_name = item.prop_name;
    ci.prop_id = item.prop_id;
    ci.value_expr = item.value_expr.has_value()
                        ? std::optional<binder::BoundExpression>(cloneBoundExpression(*item.value_expr))
                        : std::nullopt;
    ci.label_id = item.label_id;
    ci.strong_mode = item.strong_mode;
    ci.is_add_assign = item.is_add_assign;
    return ci;
}

} // namespace

// ============================================================
// Deep clone BoundLogicalOperator (operator data only, child/left/right NOT cloned)
// ============================================================
binder::BoundLogicalOperator cloneBoundLogicalOperator(const binder::BoundLogicalOperator& op) {
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
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
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
                c->direction = val->direction;
                c->edge_prop_ids = val->edge_prop_ids;
                c->dst_label_prop_ids = val->dst_label_prop_ids;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                auto c = std::make_unique<binder::BoundSkipOp>();
                c->count = val->count;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                auto c = std::make_unique<binder::BoundLimitOp>();
                c->count = val->count;
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
                c->targets = val->targets;
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
                c->direction = val->direction;
                c->min_hops = val->min_hops;
                c->max_hops = val->max_hops;
                c->dst_label_prop_ids = val->dst_label_prop_ids;
                c->path_variable = val->path_variable;
                c->path_column_index = val->path_column_index;
                c->path_handled_by_varlen = val->path_handled_by_varlen;
                c->edge_variable = val->edge_variable;
                c->edge_column_index = val->edge_column_index;
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
            }
            // Operators with BoundExpression — clone expressions, skip child
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                auto c = std::make_unique<binder::BoundFilterOp>();
                c->predicate = cloneBoundExpression(val->predicate);
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
                    ca.argument = cloneBoundExpression(agg.argument);
                    ca.alias = agg.alias;
                    ca.result_type = agg.result_type;
                    ca.func_def = agg.func_def;
                    ca.distinct = agg.distinct;
                    ca.is_internal = agg.is_internal;
                    c->aggregates.push_back(std::move(ca));
                }
                c->output_names = val->output_names;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                auto c = std::make_unique<binder::BoundCreateNodeOp>();
                c->variable = val->variable;
                c->label_ids = val->label_ids;
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
                return c;
            }
            // This line reached only if variant type not handled — compile error preferred
            return binder::BoundScanOp{};
        },
        op);
}

// ============================================================
// Deep clone BoundExpression
// ============================================================
namespace {

std::optional<binder::BoundExpression> cloneOptExpr(const std::optional<binder::BoundExpression>& opt) {
    if (opt.has_value())
        return cloneBoundExpression(*opt);
    return std::nullopt;
}

} // namespace

binder::BoundExpression cloneBoundExpression(const binder::BoundExpression& expr) {
    return std::visit(
        [](const auto& val) -> binder::BoundExpression {
            using T = std::decay_t<decltype(val)>;
            // Value types: trivially copyable
            if constexpr (std::is_same_v<T, binder::BoundLiteral> || std::is_same_v<T, binder::BoundColumnRef> ||
                          std::is_same_v<T, binder::BoundVariableRef> || std::is_same_v<T, binder::BoundParameter>) {
                return val;
            }
            // unique_ptr types that need deep cloning for nested BoundExpressions
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundBinaryOp>();
                c->op = val->op;
                c->left = cloneBoundExpression(val->left);
                c->right = cloneBoundExpression(val->right);
                c->result_type = val->result_type;
                c->batch_fn = val->batch_fn;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundUnaryOp>();
                c->op = val->op;
                c->operand = cloneBoundExpression(val->operand);
                c->result_type = val->result_type;
                c->batch_fn = val->batch_fn;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundFunctionCall>();
                c->func_def = val->func_def;
                for (const auto& arg : val->args)
                    c->args.push_back(cloneBoundExpression(arg));
                c->return_type = val->return_type;
                c->distinct = val->distinct;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundList>();
                for (const auto& elem : val->elements)
                    c->elements.push_back(cloneBoundExpression(elem));
                c->element_type = val->element_type;
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundMap>();
                for (const auto& [k, v] : val->entries)
                    c->entries.emplace_back(k, cloneBoundExpression(v));
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundCase>();
                c->subject = cloneOptExpr(val->subject);
                for (const auto& [w, t] : val->when_thens)
                    c->when_thens.emplace_back(cloneBoundExpression(w), cloneBoundExpression(t));
                c->else_expr = cloneOptExpr(val->else_expr);
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundSubscript>();
                c->list = cloneBoundExpression(val->list);
                c->index = cloneBoundExpression(val->index);
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundSlice>();
                c->list = cloneBoundExpression(val->list);
                c->from = cloneOptExpr(val->from);
                c->to = cloneOptExpr(val->to);
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundDynamicPropertyRef>();
                c->object = cloneBoundExpression(val->object);
                c->property = val->property;
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundPropertyRef>();
                c->property_name = val->property_name;
                c->object = cloneBoundExpression(val->object);
                c->candidates = val->candidates;
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundLabelCast>();
                c->object = cloneBoundExpression(val->object);
                c->label_id = val->label_id;
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundAllExpr>();
                c->variable = val->variable;
                c->loop_column_index = val->loop_column_index;
                c->list_expr = cloneBoundExpression(val->list_expr);
                c->where_pred = cloneOptExpr(val->where_pred);
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundAnyExpr>();
                c->variable = val->variable;
                c->loop_column_index = val->loop_column_index;
                c->list_expr = cloneBoundExpression(val->list_expr);
                c->where_pred = cloneOptExpr(val->where_pred);
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundNoneExpr>();
                c->variable = val->variable;
                c->loop_column_index = val->loop_column_index;
                c->list_expr = cloneBoundExpression(val->list_expr);
                c->where_pred = cloneOptExpr(val->where_pred);
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundSingleExpr>();
                c->variable = val->variable;
                c->loop_column_index = val->loop_column_index;
                c->list_expr = cloneBoundExpression(val->list_expr);
                c->where_pred = cloneOptExpr(val->where_pred);
                c->result_type = val->result_type;
                return c;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                if (!val)
                    return binder::BoundLiteral{};
                auto c = std::make_unique<binder::BoundListComprehension>();
                c->variable = val->variable;
                c->loop_column_index = val->loop_column_index;
                c->list_expr = cloneBoundExpression(val->list_expr);
                c->where_pred = cloneOptExpr(val->where_pred);
                c->projection = cloneBoundExpression(val->projection);
                c->result_type = val->result_type;
                return c;
            } else {
                // Fallback for any type not explicitly handled above
                if (!val)
                    return binder::BoundLiteral{};
                return std::make_unique<std::decay_t<decltype(*val)>>(*val);
            }
        },
        expr);
}

// ============================================================
// Group — CBO methods
// ============================================================

Winner* Group::getWinner(const PhysProp& prop) {
    for (auto& w : winners) {
        if (w.physProp() == prop)
            return &w;
    }
    return nullptr;
}

Winner* Group::getSatisfyingWinner(const PhysProp& prop) {
    Winner* best = nullptr;
    for (auto& w : winners) {
        if (!w.done() || w.plan() == INVALID_EXPR_ID)
            continue;
        if (!w.physProp().satisfies(prop))
            continue;
        if (best == nullptr || w.cost() < best->cost())
            best = &w;
    }
    return best;
}

void Group::newWinner(PhysProp prop, ExprId plan, Cost cost, bool done) {
    for (auto& w : winners) {
        if (w.physProp() == prop) {
            w.setPlan(plan);
            w.setCost(std::move(cost));
            w.setDone(done);
            return;
        }
    }
    winners.emplace_back(plan, std::move(prop), std::move(cost), done);
}

bool Group::searchCircle(const Context& ctx, bool& moreSearch) const {
    const Winner* winner = nullptr;
    for (const auto& w : winners) {
        if (w.physProp() == ctx.getPhysProp()) {
            winner = &w;
            break;
        }
    }

    // No winner for this property → case (3): more search needed
    if (!winner) {
        moreSearch = true;
        return false;
    }

    // Winner exists, check cost vs context upper bound
    const Cost& wCost = winner->cost();
    const Cost& cCost = ctx.getUpperBound();

    if (winner->plan() != INVALID_EXPR_ID) {
        // Non-null plan
        if (wCost <= cCost) {
            // Case (2): satisfying winner exists
            moreSearch = false;
            return true;
        }
        // Case (1): search impossible (winner too expensive for context)
        moreSearch = false;
        return false;
    } else {
        // Null plan — previous search failed
        if (wCost >= cCost) {
            // Case (1): search impossible
            moreSearch = false;
            return false;
        }
        // Case (4): less restrictive context, try again
        moreSearch = true;
        return true;
    }
}

bool Group::hasDoneWinner() const {
    for (const auto& w : winners) {
        if (w.done())
            return true;
    }
    return false;
}

// ============================================================
// Group — LogProp methods (CBO Phase 3)
// ============================================================

const LogProp& Group::getLogProp(Memo& memo, const catalog::Catalog* catalog) {
    if (log_prop_valid_)
        return log_prop_;

    if (logical_exprs.empty()) {
        log_prop_.cardinality = 0.0;
        log_prop_.columns.clear();
        log_prop_.valid = true;
        log_prop_valid_ = true;
        return log_prop_;
    }

    GroupExpr& expr = memo.getExpr(logical_exprs.front());

    // Recursively derive child LogProps and register as parent
    std::vector<LogProp> input_lps;
    input_lps.reserve(expr.child_groups.size());
    for (GroupId child_gid : expr.child_groups) {
        Group& child = memo.getGroup(child_gid);
        input_lps.push_back(child.getLogProp(memo, catalog));
        child.parents_.insert(id);
    }

    LogPropDeriver deriver(catalog);
    log_prop_ = deriver.derive(expr.op, input_lps);
    log_prop_valid_ = true;
    return log_prop_;
}

void Group::invalidateLogProp(Memo& memo) {
    if (!log_prop_valid_ && parents_.empty())
        return;

    log_prop_valid_ = false;
    log_prop_.valid = false;

    // Move parents out before clearing — recursive invalidation may re-add
    auto parents_copy = std::move(parents_);
    parents_.clear();

    for (GroupId parent_gid : parents_copy) {
        memo.getGroup(parent_gid).invalidateLogProp(memo);
    }
}

// ============================================================
// Dump methods for CBO types
// ============================================================

std::string Cost::dump() const {
    if (isInfinity())
        return "INF";
    return std::to_string(value_);
}

std::string PhysProp::dump() const {
    if (isAny())
        return "any";
    std::string out;
    if (order_ != SortOrder::any) {
        std::string dir = (order_ == SortOrder::ascending) ? "asc" : "desc";
        out = "sort(prop=" + std::to_string(prop_id_) + "," + dir + ")";
    }
    if (hasMaterializations()) {
        if (!out.empty())
            out += "+";
        out += "mat{";
        bool first = true;
        for (const auto& [var, req] : materializations_) {
            if (req.empty())
                continue;
            if (!first)
                out += ",";
            first = false;
            out += var;
            out += ":";
            if (req.need_labels)
                out += "L";
            for (const auto& [lid, props] : req.need_props) {
                out += "[label=" + std::to_string(lid) + ":";
                bool pfirst = true;
                for (uint16_t pid : props) {
                    if (!pfirst)
                        out += ",";
                    pfirst = false;
                    out += std::to_string(pid);
                }
                out += "]";
            }
        }
        out += "}";
    }
    return out.empty() ? "any" : out;
}

bool PhysProp::satisfies(const PhysProp& required) const {
    // Sort dimension: exact match required, except when `required` is any.
    if (required.order_ != SortOrder::any) {
        if (order_ != required.order_ || prop_id_ != required.prop_id_)
            return false;
    }
    // Materialization dimension: provider must satisfy each required variable.
    if (!satisfiesVarRequirements(materializations_, required.materializations_))
        return false;
    return true;
}

std::string Context::dump() const {
    return "Context{prop=" + prop_.dump() + ", ub=" + upper_bound_.dump() + (done_ ? ", done}" : "}");
}

} // namespace optimizer
} // namespace eugraph
