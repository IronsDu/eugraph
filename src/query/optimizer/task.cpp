#include "query/optimizer/task.hpp"

#include <algorithm>

namespace eugraph {
namespace optimizer {

// ============================================================
// OGroupTask — matches Columbia O_GROUP::perform
//
// CBO logic (Columbia lines 195-275):
//   1. If group is const → register winner with cost 0, terminate
//   2. search_circle → if satisfying winner exists, terminate
//   3. If not yet optimized:
//      a. Create winner with null plan (case 3)
//      b. Push O_EXPR on every logical expression (see below)
//   4. RBO path: push O_EXPR on every logical expression
// ============================================================
void OGroupTask::perform(Memo& memo, RuleSet& /*rules*/, TaskQueue& queue) {
    Group& group = memo.getGroup(group_id_);

    if (group.logical_exprs.empty())
        return;

    // CBO: check search_circle first
    if (context_id_ >= 0 && context_id_ < static_cast<int>(memo.contexts().size())) {
        const Context& ctx = memo.contexts()[context_id_];

        // Const group short-circuit (Columbia: O_GROUP lines 208-215)
        // If first expression has no children and is a leaf type, treat as const.
        // (Graph DB doesn't have traditional const, but the framework supports it.)

        bool moreSearch = false;
        // searchCircle's return value distinguishes case (1) impossible vs
        // case (2) satisfying — both terminate O_GROUP, so we only need
        // moreSearch to decide whether to push O_EXPR.
        (void)group.searchCircle(ctx, moreSearch);

        if (!moreSearch) {
            // Case (1) or (2): terminate
            return;
        }

        // Case (3) or (4): need to optimize
        if (!group.optimized && !group.optimizing) {
            // Prevent re-entrant O_GROUP (Columbia: set_optimizing flag)
            group.optimizing = true;
            group.optimized = false;

            // Create winner with null plan (Columbia line 270)
            group.newWinner(ctx.getPhysProp(), INVALID_EXPR_ID, Cost(ctx.getUpperBound()), /*done=*/false);

            // Reset changed flag before optimization — will be set by insertExpr
            // if rules add new expressions during this round.
            group.changed = false;

            // Push O_EXPR on EVERY logical expression, not just the first.
            // Columbia's O_GROUP only pushes on the first expr and relies on
            // transformation-rule confluence to reach the rest. Our
            // FilterPushdown violates that assumption — its substitute
            // produces a different operator type (Filter → Expand), so once
            // it fires on the first expr the new Expand has no remaining
            // rule connecting it back, and its ImplExpand never fires.
            // Iterating all exprs here is the same shape Columbia uses for
            // case (4) (re-cost under new context → O_INPUTS on every
            // physical expr).
            //
            // Reverse order so logical_exprs[0] ends up on top of the LIFO
            // stack (popped first); last_ goes on logical_exprs[N-1] which
            // fires last and triggers cleanup in ~OExprTask.
            bool has_last = last_;
            last_ = false;
            for (size_t i = group.logical_exprs.size(); i > 0; --i) {
                ExprId eid = group.logical_exprs[i - 1];
                bool is_last = (i == group.logical_exprs.size()) && has_last;
                queue.push(std::make_unique<OExprTask>(eid, /*explore=*/false, memo, context_id_, is_last));
            }
        } else if (group.optimized) {
            // Phase B case (4): the group was already optimized for some
            // property; the new context requires re-costing under a new
            // property (e.g. a child group now needs materialized form).
            // Physical exprs are already in place — re-run O_INPUTS on
            // each under the new context to register winners for this
            // property. Columbia: O_GROUP case (4) re-cost path.
            group.newWinner(ctx.getPhysProp(), INVALID_EXPR_ID, Cost(ctx.getUpperBound()), /*done=*/false);
            bool has_last = last_;
            last_ = false;
            // If there are no physical exprs yet (group has only logical
            // exprs and no impl rule has fired), fall back to O_EXPR so
            // impl rules fire under the new context.
            if (group.physical_exprs.empty()) {
                for (size_t i = group.logical_exprs.size(); i > 0; --i) {
                    ExprId eid = group.logical_exprs[i - 1];
                    bool is_last = (i == group.logical_exprs.size()) && has_last;
                    queue.push(std::make_unique<OExprTask>(eid, /*explore=*/false, memo, context_id_, is_last));
                }
            } else {
                for (size_t i = group.physical_exprs.size(); i > 0; --i) {
                    ExprId eid = group.physical_exprs[i - 1];
                    bool is_last = (i == group.physical_exprs.size()) && has_last;
                    queue.push(std::make_unique<OInputsTask>(eid, context_id_, is_last));
                }
            }
        }
        return;
    }

    // RBO path: no context → push O_EXPR on every logical expr.
    for (size_t i = group.logical_exprs.size(); i > 0; --i) {
        ExprId eid = group.logical_exprs[i - 1];
        bool is_last = (i == group.logical_exprs.size()) && last_;
        queue.push(std::make_unique<OExprTask>(eid, /*explore=*/false, memo, context_id_, is_last));
    }
}

// ============================================================
// EGroupTask — matches Columbia E_GROUP::perform
// ============================================================
void EGroupTask::perform(Memo& memo, RuleSet& /*rules*/, TaskQueue& queue) {
    Group& group = memo.getGroup(group_id_);

    if (group.optimized)
        return;
    if (group.explored)
        return;
    if (group.exploring)
        return;

    group.exploring = true;
    group.explored = false;

    if (!group.logical_exprs.empty()) {
        ExprId first_eid = group.logical_exprs.front();
        queue.push(std::make_unique<OExprTask>(first_eid, /*explore=*/true, memo, context_id_, /*last=*/true));
    }
}

// ============================================================
// OExprTask — matches Columbia O_EXPR::perform
// ============================================================
void OExprTask::perform(Memo& memo, RuleSet& rules, TaskQueue& queue) {
    GroupExpr& expr = memo.getExpr(expr_id_);

    // Collect applicable rules sorted by promise (descending).
    // Columbia's O_EXPR (tasks.cpp 553-583) iterates the rule set, calls
    // top_match + promise, and collects into a MOVE[] array.
    struct RulePromise {
        int promise;
        int index;
    };
    std::vector<RulePromise> applicable;
    for (const auto& rule : rules.rules()) {
        if (!expr.canFire(rule->index()))
            continue;
        if (!rule->topMatch(expr)) {
            continue;
        }

        if (explore_ && rule->isImplementation()) {
            continue;
        }

        int p = rule->promise();
        if (p <= 0)
            continue;
        applicable.push_back({p, rule->index()});
    }
    std::sort(applicable.begin(), applicable.end(), [](const auto& a, const auto& b) { return a.promise > b.promise; });

    // Push in reverse so highest-promise rule ends up on top (popped first,
    // fires first — matches Columbia's O_EXPR::perform ordering, helps the
    // upper-bound prune kick in early). last_ goes on the lowest-promise
    // rule: it's pushed first (deepest), fires last, and its cascade's
    // ~OExprTask does the group cleanup.
    bool has_last = last_;
    last_ = false;
    for (size_t i = applicable.size(); i > 0; --i) {
        size_t idx = i - 1;
        bool is_last_rule = (i == applicable.size()) && has_last;
        queue.push(
            std::make_unique<ApplyRuleTask>(applicable[idx].index, expr_id_, explore_, context_id_, is_last_rule));
    }
    // If no rules were applicable at all, re-arm last_ so the destructor
    // still marks the group explored/optimized. Without this, groups with
    // operators that have no matching impl rules (e.g. BoundCreateNodeOp)
    // stay optimizing=true forever, and parent O_INPUTS loops.
    if (applicable.empty()) {
        last_ = has_last;
    }

    // Push E_GROUP for child inputs that haven't been explored.
    for (GroupId child_gid : expr.child_groups) {
        Group& child_group = memo.getGroup(child_gid);
        if (child_group.explored || child_group.exploring || child_group.optimized)
            continue;
        queue.push(std::make_unique<EGroupTask>(child_gid, context_id_));
    }
}

OExprTask::~OExprTask() {
    // Columbia ~O_EXPR: if Last, mark group as explored/optimized.
    if (!last_ || !memo_)
        return;
    Group& group = memo_->getGroup(memo_->getExpr(expr_id_).group_id);
    if (explore_) {
        group.explored = true;
        group.exploring = false;
    } else {
        // CBO: check whether the group was changed during optimization.
        // If new expressions were added (by rule firing), the winner is not
        // yet final — skip marking it done and leave the group open for
        // another optimization round.
        if (group.changed) {
            group.optimized = false;
            group.optimizing = false;
            group.changed = false;
        } else {
            if (context_id_ >= 0 && context_id_ < static_cast<int>(memo_->contexts().size())) {
                const Context& ctx = memo_->contexts()[context_id_];
                Winner* w = group.getWinner(ctx.getPhysProp());
                if (w) {
                    w->setDone(true);
                }
            }
            group.optimized = true;
            group.optimizing = false;
        }
    }
}

// ============================================================
// ApplyRuleTask — matches Columbia APPLY_RULE::perform
// ============================================================
void ApplyRuleTask::perform(Memo& memo, RuleSet& rules, TaskQueue& queue) {
    GroupExpr& expr = memo.getExpr(expr_id_);
    const auto& rule = rules.rules()[rule_idx_];

    if (!expr.canFire(rule->index())) {
        return;
    }

    expr.markRuleFired(rule->index());

    if (!rule->condition(expr, memo)) {
        return;
    }

    auto new_exprs = rule->substitute(expr, memo);
    for (size_t i = 0; i < new_exprs.size(); ++i) {
        auto& new_expr = new_exprs[i];
        GroupId target_group = expr.group_id;

        // Phase 4: dispatch dedup on is_physical
        GroupExpr* dup = new_expr->isPhysical() ? memo.findPhysDuplicate(new_expr->physOp(), new_expr->child_groups)
                                                : memo.findDuplicate(new_expr->op, new_expr->child_groups);
        if (dup) {
            if (dup->group_id != target_group) {
                memo.mergeGroups(target_group, dup->group_id);
            }
            continue;
        }

        // Columbia sets the new expr's RuleMask from Rule->get_mask()
        // (a per-rule subsumption bitmap). We don't expose subsumption
        // masks yet, so the new expr keeps the default empty mask.
        // Previously this copied `expr.rule_mask` from the source, which
        // incorrectly suppressed rules fired on the source (e.g.
        // ImplFilter on a Filter would block ImplExpand on the resulting
        // Expand — different operator, different applicable rules).

        bool is_physical = new_expr->isPhysical();

        GroupExpr* inserted;
        if (is_physical) {
            inserted = memo.insertPhysExpr(std::move(new_expr), target_group);
        } else {
            inserted = memo.insertExpr(std::move(new_expr), target_group);
        }

        bool is_first_new = (i == 0) && last_;
        if (is_first_new)
            last_ = false;

        // Columbia APPLY_RULE lines 1656-1722:
        //   explore → O_EXPR(explore=true) for logical
        //   optimize → logical: O_EXPR(explore=false); physical: O_INPUTS
        if (explore_) {
            queue.push(std::make_unique<OExprTask>(inserted->id, true, memo, context_id_, is_first_new));
        } else {
            if (is_physical) {
                queue.push(std::make_unique<OInputsTask>(inserted->id, context_id_, is_first_new));
            } else {
                queue.push(std::make_unique<OExprTask>(inserted->id, false, memo, context_id_, is_first_new));
            }
        }
    }
}

// ============================================================
// OInputsTask — costs a physical expression by optimizing inputs
//
// Materialization-aware (Phase B):
//   - Each input is looked up under the materialization property
//     required by THIS operator for that input (expr.required_input_mat[i])
//     unioned with what the parent context demands of this group's
//     output (localReqdProp.materializations).
//   - If the input group has been optimized but no winner satisfies
//     the required input property, attempt Enricher enforcer insertion
//     (Memo::insertEnricherEnforcer) to wrap the input's "any"-winner.
//   - Before registering a winner, the operator's own output_mat must
//     satisfy localReqdProp — operators that don't provide the required
//     materialization are skipped (an Enricher enforcer in the current
//     group will handle it instead).
// ============================================================
void OInputsTask::perform(Memo& memo, RuleSet& /*rules*/, TaskQueue& queue) {
    memo_ = &memo; // re-arm for destructor — perform always runs before destruction
    GroupExpr& expr = memo.getExpr(expr_id_);
    Group& group = memo.getGroup(expr.group_id);

    if (context_id_ < 0 || context_id_ >= static_cast<int>(memo.contexts().size())) {
        return;
    }
    const Context& ctx = memo.contexts()[context_id_];
    // Copy required properties upfront — the loop below calls memo.addContext()
    // which may reallocate the contexts vector and invalidate this reference.
    PhysProp localReqdProp = ctx.getPhysProp();
    const VarRequirements& localReqdMat = localReqdProp.materializations();

    int arity = static_cast<int>(expr.child_groups.size());

    // Zero-arity: leaf operator. Cost is its own local cost (e.g. LabelScan
    // cost = cardinality), not 0. Using 0 here made every leaf free, which
    // distorted downstream comparisons.
    if (arity == 0) {
        // Phase C: always register the winner under the property the leaf
        // actually provides (its `output_mat`). For topology-stage leaves
        // (Scan, Expand) this is "any". The parent's input lookup uses
        // getSatisfyingWinner, which finds this winner when the parent's
        // required input property is a SUBSET of provided. When the parent
        // demands materialization the leaf cannot satisfy, the parent's
        // enforcer-insertion path picks up the any-winner and wraps it
        // with an Enricher.
        LogProp group_lp = group.getLogProp(memo, memo.getCatalog());
        Cost localCost = findLocalCost(expr.physOp().tag, group_lp, {});
        PhysProp provided;
        provided.setMaterializations(expr.physOp().output_mat);
        group.newWinner(provided, expr_id_, localCost, /*done=*/true);
        return;
    }

    // Build per-input required materializations: union of what this operator
    // declares per-input and what the parent context demands transitively.
    // Conservative: the full parent materialization is passed down to every
    // input. Refinement using schema visibility can come later.
    //
    // Enricher enforcers are exempt: they PROVIDE materialization (so they
    // satisfy the parent's demand themselves) and their input is the same
    // group's topology-form winner — they must NOT propagate the parent's
    // materialization demand to the input, or they recurse forever.
    auto inputRequiredProp = [&](int i) -> PhysProp {
        PhysicalOpTag tag = expr.physOp().tag;
        if (tag == PhysicalOpTag::VertexEnrich || tag == PhysicalOpTag::EdgeEnrich ||
            tag == PhysicalOpTag::PathEnrich || tag == PhysicalOpTag::VertexPropertyExtract ||
            tag == PhysicalOpTag::EdgePropertyExtract || tag == PhysicalOpTag::PathPropertyExtract) {
            return PhysProp{}; // any — enricher/extract provides its own materializations
        }
        VarRequirements mat;
        if (i < static_cast<int>(expr.physOp().required_input_mat.size()))
            mergeVarRequirements(mat, expr.physOp().required_input_mat[i]);
        mergeVarRequirements(mat, localReqdMat);
        PhysProp p;
        p.setMaterializations(std::move(mat));
        return p;
    };

    // Cost pass: check each input for a satisfying done winner.
    Cost totalCost(0.0);
    bool allCosted = true;
    bool impossible = false;

    for (int i = 0; i < arity; i++) {
        Group& ig = memo.getGroup(expr.child_groups[i]);
        PhysProp inputProp = inputRequiredProp(i);

        Winner* w = ig.getSatisfyingWinner(inputProp);
        if (w && w->done() && w->plan() != INVALID_EXPR_ID) {
            totalCost = totalCost + w->cost();
            continue;
        }

        // No satisfying winner. If the input is fully optimized, attempt
        // Enricher enforcer insertion.
        if (ig.optimized && !ig.optimizing) {
            if (inputProp.hasMaterializations()) {
                ExprId enforcer_id = memo.insertEnricherEnforcer(expr.child_groups[i], inputProp.materializations());
                if (enforcer_id != INVALID_EXPR_ID) {
                    // Cost the enforcer: local cost (input cardinality) +
                    // wrapped "any"-winner cost.
                    LogProp child_lp = ig.getLogProp(memo, memo.getCatalog());
                    GroupExpr& enforcer_expr = memo.getExpr(enforcer_id);
                    Cost enforcer_local = findLocalCost(enforcer_expr.physOp().tag, child_lp, {child_lp});
                    Winner* any_w = ig.getWinner(PhysProp{});
                    Cost wrapped =
                        (any_w && any_w->done() && any_w->plan() != INVALID_EXPR_ID) ? any_w->cost() : Cost(0.0);
                    Cost enforcer_total = enforcer_local + wrapped;
                    ig.newWinner(inputProp, enforcer_id, enforcer_total, /*done=*/true);
                    totalCost = totalCost + enforcer_total;
                    continue;
                }
            }
            impossible = true;
            break;
        }

        allCosted = false;
    }

    if (impossible) {
        return; // Can't cost this plan
    }

    if (!allCosted) {
        // Push O_GROUP for uncosted inputs (under input-specific property),
        // then re-push self.
        queue.push(std::make_unique<OInputsTask>(expr_id_, context_id_, last_));
        last_ = false;

        for (int i = 0; i < arity; i++) {
            Group& ig = memo.getGroup(expr.child_groups[i]);
            PhysProp inputProp = inputRequiredProp(i);
            Winner* w = ig.getSatisfyingWinner(inputProp);
            if (!(w && w->done() && w->plan() != INVALID_EXPR_ID) && !ig.optimized && !ig.optimizing) {
                Context inputCtx(inputProp, Cost::infinity());
                int inputCtxId = memo.addContext(std::move(inputCtx));
                queue.push(std::make_unique<OGroupTask>(expr.child_groups[i], inputCtxId, /*last=*/true));
            }
        }
        return;
    }

    // Phase B: before registering, verify this operator's output satisfies
    // what the parent requires. Operators that cannot satisfy (e.g. a
    // topology-stage Scan asked to provide materialized data) are skipped —
    // an Enricher enforcer in this group will satisfy it instead.
    {
        PhysProp provided;
        provided.setMaterializations(expr.physOp().output_mat);
        if (!provided.satisfies(localReqdProp)) {
            return;
        }
    }

    // Compute local cost via Phase 3 cost model
    LogProp group_lp = group.getLogProp(memo, memo.getCatalog());
    std::vector<LogProp> input_lps;
    input_lps.reserve(arity);
    for (int i = 0; i < arity; i++) {
        input_lps.push_back(memo.getGroup(expr.child_groups[i]).getLogProp(memo, memo.getCatalog()));
    }
    Cost localCost = findLocalCost(expr.physOp().tag, group_lp, input_lps);
    totalCost = totalCost + localCost;

    // Check upper bound
    const Cost& localUB = ctx.getUpperBound();
    if (!localUB.isInfinity() && totalCost >= localUB) {
        return;
    }

    // Compare with existing winner
    Winner* existingW = group.getWinner(localReqdProp);
    if (existingW && existingW->plan() != INVALID_EXPR_ID) {
        if (totalCost >= existingW->cost()) {
            return;
        }
    }

    group.newWinner(localReqdProp, expr_id_, totalCost, /*done=*/true);
}

OInputsTask::~OInputsTask() {
    // Columbia ~O_INPUTS: when Last, mark owning group optimized so
    // O_GROUP can iterate (case 4) or terminate (search_circle). The
    // Last flag is propagated down the cascade O_GROUP → O_EXPR →
    // APPLY_RULE → O_INPUTS, so the O_INPUTS at the bottom of the
    // cascade is responsible for closing out the round. Without this
    // destructor the group stayed optimizing=true forever, breaking
    // the parent group's O_INPUTS re-run scheduling — that manifested
    // as the max-iterations infinite loop.
    if (!last_ || !memo_)
        return;
    Group& group = memo_->getGroup(memo_->getExpr(expr_id_).group_id);
    if (group.changed) {
        // New logical exprs appeared during this round — keep the
        // group open so O_GROUP iterates again to optimize them.
        group.optimized = false;
        group.optimizing = false;
        group.changed = false;
    } else {
        if (context_id_ >= 0 && context_id_ < static_cast<int>(memo_->contexts().size())) {
            const Context& ctx = memo_->contexts()[context_id_];
            Winner* w = group.getWinner(ctx.getPhysProp());
            if (w)
                w->setDone(true);
        }
        group.optimized = true;
        group.optimizing = false;
    }
}

} // namespace optimizer
} // namespace eugraph
