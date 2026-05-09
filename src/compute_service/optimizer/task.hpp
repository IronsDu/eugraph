#pragma once

#include "compute_service/optimizer/memo.hpp"
#include "compute_service/optimizer/opt_rule.hpp"

#include <deque>
#include <memory>

namespace eugraph {
namespace optimizer {

class TaskQueue;

// ============================================================
// Task base class
// ============================================================
class Task {
public:
    virtual ~Task() = default;
    virtual void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) = 0;
};

// ============================================================
// TaskQueue — holds pending tasks (stack-based like Columbia)
// ============================================================
class TaskQueue {
public:
    void push(std::unique_ptr<Task> task) {
        queue_.push_back(std::move(task));
    }

    std::unique_ptr<Task> pop() {
        if (queue_.empty())
            return nullptr;
        auto task = std::move(queue_.back());
        queue_.pop_back();
        return task;
    }

    bool empty() const {
        return queue_.empty();
    }

private:
    std::deque<std::unique_ptr<Task>> queue_;
};

// ============================================================
// O_GROUP — optimize a group: iterate all logical GroupExprs,
// create O_EXPR task for each
// ============================================================
class OGroupTask : public Task {
public:
    explicit OGroupTask(GroupId group_id) : group_id_(group_id) {}

    void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) override;

private:
    GroupId group_id_;
};

// ============================================================
// O_EXPR — optimize a GroupExpr: find matching rules, push APPLY_RULE
// ============================================================
class OExprTask : public Task {
public:
    explicit OExprTask(ExprId expr_id) : expr_id_(expr_id) {}

    void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) override;

private:
    ExprId expr_id_;
};

// ============================================================
// APPLY_RULE — apply a single rule to a GroupExpr
// ============================================================
class ApplyRuleTask : public Task {
public:
    ApplyRuleTask(int rule_idx, ExprId expr_id) : rule_idx_(rule_idx), expr_id_(expr_id) {}

    void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) override;

private:
    int rule_idx_;
    ExprId expr_id_;
};

} // namespace optimizer
} // namespace eugraph
