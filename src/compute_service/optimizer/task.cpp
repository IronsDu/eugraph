#include "compute_service/optimizer/task.hpp"

#include "compute_service/planner/bound_logical_plan.hpp"

#include <algorithm>

namespace eugraph {
namespace optimizer {

// ============================================================
// OGroupTask
// ============================================================
void OGroupTask::perform(Memo& memo, RuleSet& rules, TaskQueue& queue) {
    Group& group = memo.getGroup(group_id_);
    for (ExprId eid : group.logical_exprs) {
        queue.push(std::make_unique<OExprTask>(eid));
    }
}

// ============================================================
// OExprTask
// ============================================================
void OExprTask::perform(Memo& memo, RuleSet& rules, TaskQueue& queue) {
    GroupExpr& expr = memo.getExpr(expr_id_);

    // Collect applicable rules sorted by promise (descending)
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
        int p = rule->promise();
        if (p <= 0)
            continue;
        applicable.push_back({p, rule->index()});
    }
    std::sort(applicable.begin(), applicable.end(), [](const auto& a, const auto& b) { return a.promise > b.promise; });

    for (const auto& rp : applicable) {
        queue.push(std::make_unique<ApplyRuleTask>(rp.index, expr_id_));
    }
}

// ============================================================
// ApplyRuleTask
// ============================================================
void ApplyRuleTask::perform(Memo& memo, RuleSet& rules, TaskQueue& queue) {
    GroupExpr& expr = memo.getExpr(expr_id_);
    const auto& rule = rules.rules()[rule_idx_];

    if (!expr.canFire(rule->index()))
        return;

    expr.markRuleFired(rule->index());

    if (!rule->condition(expr, memo))
        return;

    auto new_exprs = rule->substitute(expr, memo);
    for (auto& new_expr : new_exprs) {
        GroupId target_group = expr.group_id;

        GroupExpr* dup = memo.findDuplicate(*new_expr);
        if (dup) {
            // Duplicate found — merge groups if they differ
            if (dup->group_id != target_group) {
                memo.mergeGroups(target_group, dup->group_id);
            }
            continue;
        }

        GroupExpr* inserted = memo.insertExpr(std::move(new_expr), target_group);
        // Continue optimizing the new expression
        queue.push(std::make_unique<OExprTask>(inserted->id));

        // Also optimize child groups that may contain newly created expressions
        for (GroupId child_gid : inserted->child_groups) {
            Group& child_group = memo.getGroup(child_gid);
            if (!child_group.logical_exprs.empty()) {
                ExprId last_eid = child_group.logical_exprs.back();
                queue.push(std::make_unique<OExprTask>(last_eid));
            }
        }
    }
}

} // namespace optimizer
} // namespace eugraph
