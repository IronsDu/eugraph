#pragma once

#include "query/optimizer/memo.hpp"
#include "query/optimizer/opt_rule.hpp"

#include <deque>
#include <memory>

namespace eugraph {
namespace optimizer {

class TaskQueue;

// ============================================================
// Task base class
//
// The `last_` flag matches Columbia's `Last` flag on TASK.
// `context_id_` matches Columbia's ContextID — index into the
// shared context vector (Memo::contexts()).
// ============================================================
class Task {
public:
    explicit Task(int context_id = 0, bool last = false) : context_id_(context_id), last_(last) {}
    virtual ~Task() = default;
    virtual void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) = 0;

    bool isLast() const {
        return last_;
    }
    void setLast(bool last) {
        last_ = last;
    }
    int contextId() const {
        return context_id_;
    }

protected:
    int context_id_;
    bool last_;
};

// ============================================================
// TaskQueue — LIFO stack (matches Columbia PTASKS)
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
// O_GROUP — optimize a group (Columbia: O_GROUP)
//
// CBO: checks search_circle before proceeding. If a satisfying
// winner exists, terminates early. Otherwise creates a new winner
// and pushes O_EXPR on the first logical expression.
// ============================================================
class OGroupTask : public Task {
public:
    explicit OGroupTask(GroupId group_id, int context_id = 0, bool last = true)
        : Task(context_id, last), group_id_(group_id) {}

    void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) override;

    GroupId groupId() const {
        return group_id_;
    }

private:
    GroupId group_id_;
};

// ============================================================
// E_GROUP — explore a group (Columbia: E_GROUP)
//
// Ensures all logical expressions are generated in the group
// by firing transformation rules. Uses exploring/explored flags
// to prevent re-entrancy and duplicate work.
// ============================================================
class EGroupTask : public Task {
public:
    explicit EGroupTask(GroupId group_id, int context_id = 0, bool last = false)
        : Task(context_id, last), group_id_(group_id) {}

    void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) override;

private:
    GroupId group_id_;
};

// ============================================================
// O_EXPR — optimize/explore a GroupExpr (Columbia: O_EXPR)
//
// Pushes APPLY_RULE for matching rules first, then E_GROUP for
// child inputs second. LIFO ensures children are explored before
// rules fire on this expression.
//
// Destructor: if `last_` is true, marks the owning group as
// explored or optimized (CBO: also marks winner as done).
// ============================================================
class OExprTask : public Task {
public:
    OExprTask(ExprId expr_id, bool explore, Memo& memo, int context_id = 0, bool last = false)
        : Task(context_id, last), expr_id_(expr_id), explore_(explore), memo_(&memo) {}

    ~OExprTask() override;

    void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) override;

    bool isExplore() const {
        return explore_;
    }
    ExprId exprId() const {
        return expr_id_;
    }

private:
    ExprId expr_id_;
    bool explore_;
    Memo* memo_; // non-owning, valid for lifetime of optimization
};

// ============================================================
// APPLY_RULE — apply a single rule (Columbia: APPLY_RULE)
//
// Fires the rule, inserts new expressions. When exploring, pushes
// O_EXPR(explore=true). When optimizing, pushes O_EXPR for
// logical results or O_INPUTS for physical results (CBO).
// ============================================================
class ApplyRuleTask : public Task {
public:
    ApplyRuleTask(int rule_idx, ExprId expr_id, bool explore, int context_id = 0, bool last = false)
        : Task(context_id, last), rule_idx_(rule_idx), expr_id_(expr_id), explore_(explore) {}

    void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) override;

private:
    int rule_idx_;
    ExprId expr_id_;
    bool explore_;
};

// ============================================================
// O_INPUTS — optimize inputs of a physical expression (Columbia: O_INPUTS)
//
// Stateless single-pass implementation. Columbia's O_INPUTS is
// multi-invocation (tracks InputNo, PrevInputNo, LocalCost, InputCost[]
// across re-pushes — tasks.cpp 803-1434). Our version re-scans all inputs
// on each invocation: if every input has a done winner, sum costs and
// register/update the winner; otherwise re-push self and push O_GROUP for
// each uncosted input. LIFO scheduling ensures the re-pushed self pops
// only after the O_GROUP cascades have registered winners.
//
// Trade-off: simpler code, no per-input state to manage; cost is
// O(arity) extra winner lookups on each re-entry, which is negligible
// for graph query plan arity (typically 1–2).
// ============================================================
class OInputsTask : public Task {
public:
    OInputsTask(ExprId expr_id, int context_id = 0, bool last = false) : Task(context_id, last), expr_id_(expr_id) {}

    ~OInputsTask() override;

    void perform(Memo& memo, RuleSet& rules, TaskQueue& queue) override;

private:
    ExprId expr_id_;
    Memo* memo_ = nullptr; // set in perform() so the destructor can mark the group
};

} // namespace optimizer
} // namespace eugraph
