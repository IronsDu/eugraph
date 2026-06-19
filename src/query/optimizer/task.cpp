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
        if (!rule->topMatch(expr))
            continue;

        if (explore_ && rule->isImplementation())
            continue;

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
// Stateless single-pass: checks all inputs each invocation.
//   - If all inputs have done winners → compute total cost, register winner
//   - If any input lacks a winner and is not yet optimized → push O_GROUP
//     for it and re-push self (LIFO ensures input optimized before re-run)
//   - If any input is optimized but has no winner → impossible plan, terminate
// ============================================================
void OInputsTask::perform(Memo& memo, RuleSet& /*rules*/, TaskQueue& queue) {
    GroupExpr& expr = memo.getExpr(expr_id_);
    Group& group = memo.getGroup(expr.group_id);

    if (context_id_ < 0 || context_id_ >= static_cast<int>(memo.contexts().size())) {
        return;
    }
    const Context& ctx = memo.contexts()[context_id_];
    const PhysProp& localReqdProp = ctx.getPhysProp();

    int arity = static_cast<int>(expr.child_groups.size());

    // Zero-arity: leaf operator. Cost is its own local cost (e.g. LabelScan
    // cost = cardinality), not 0. Using 0 here made every leaf free, which
    // distorted downstream comparisons.
    if (arity == 0) {
        LogProp group_lp = group.getLogProp(memo, memo.getCatalog());
        Cost localCost = findLocalCost(expr.physOp().tag, group_lp, {});
        group.newWinner(localReqdProp, expr_id_, localCost, /*done=*/true);
        return;
    }

    // Check all inputs for done winners
    Cost totalCost(0.0);
    bool allCosted = true;
    bool impossible = false;

    PhysProp anyProp;
    for (int i = 0; i < arity; i++) {
        Group& ig = memo.getGroup(expr.child_groups[i]);
        Winner* w = ig.getWinner(anyProp);
        if (w && w->done() && w->plan() != INVALID_EXPR_ID) {
            totalCost = totalCost + w->cost();
        } else if (ig.optimized) {
            // Input is fully optimized but has no winner — impossible plan
            impossible = true;
            break;
        } else {
            allCosted = false;
        }
    }

    if (impossible) {
        return; // Can't cost this plan
    }

    if (!allCosted) {
        // Push O_GROUP for uncosted inputs, then re-push self.
        queue.push(std::make_unique<OInputsTask>(expr_id_, context_id_, last_));
        last_ = false;

        for (int i = 0; i < arity; i++) {
            Group& ig = memo.getGroup(expr.child_groups[i]);
            Winner* w = ig.getWinner(anyProp);
            if (!(w && w->done() && w->plan() != INVALID_EXPR_ID) && !ig.optimized && !ig.optimizing) {
                Context inputCtx(anyProp, Cost::infinity());
                int inputCtxId = memo.addContext(std::move(inputCtx));
                queue.push(std::make_unique<OGroupTask>(expr.child_groups[i], inputCtxId, /*last=*/true));
            }
        }
        return;
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

} // namespace optimizer
} // namespace eugraph
