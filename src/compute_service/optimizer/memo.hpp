#pragma once

#include "compute_service/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace eugraph {
namespace optimizer {

using GroupId = uint32_t;
using ExprId = uint32_t;

inline constexpr GroupId INVALID_GROUP_ID = UINT32_MAX;
inline constexpr ExprId INVALID_EXPR_ID = UINT32_MAX;

// ============================================================
// Group — a set of logically equivalent GroupExprs
// ============================================================
class Group {
public:
    explicit Group(GroupId id) : id(id) {}

    GroupId id;

    std::vector<ExprId> logical_exprs;
    std::vector<ExprId> physical_exprs; // CBO phase

    bool explored = false;
    bool optimized = false;
};

// ============================================================
// GroupExpr — operator data + child group references
// (corresponds to Columbia M_EXPR)
// ============================================================
class GroupExpr {
public:
    GroupExpr(ExprId id, GroupId group_id, binder::BoundLogicalOperator op, std::vector<GroupId> child_groups)
        : id(id), group_id(group_id), op(std::move(op)), child_groups(std::move(child_groups)) {}

    ExprId id;
    GroupId group_id;

    // The operator data. The `child` field within is unused in Memo mode;
    // children are tracked via `child_groups` (GroupId references).
    binder::BoundLogicalOperator op;
    std::vector<GroupId> child_groups;

    // Bit mask tracking which rules have been applied to avoid re-firing.
    uint32_t rule_mask = 0;

    void markRuleFired(int rule_idx) {
        rule_mask |= (1u << rule_idx);
    }
    bool canFire(int rule_idx) const {
        return (rule_mask & (1u << rule_idx)) == 0;
    }
};

// ============================================================
// Memo — the search space (corresponds to Columbia SSP)
// ============================================================
class Memo {
public:
    // Insert a BoundLogicalOperator tree into the Memo.
    // Returns the GroupId of the root group.
    GroupId copyIn(binder::BoundLogicalOperator& op);

    // Extract the best plan from the Memo as a BoundLogicalOperator tree.
    binder::BoundLogicalOperator copyOut(GroupId root_gid);

    // Check if an equivalent expression already exists in the Memo.
    GroupExpr* findDuplicate(const GroupExpr& expr);

    // Merge two equivalent groups (larger ID into smaller ID).
    GroupId mergeGroups(GroupId g1, GroupId g2);

    // Insert a new GroupExpr into an existing group (or a new group).
    GroupExpr* insertExpr(std::unique_ptr<GroupExpr> expr, GroupId target_group);

    // Create a new group with a single GroupExpr. Returns the GroupExpr pointer.
    GroupExpr* createGroupWithExpr(binder::BoundLogicalOperator op, std::vector<GroupId> child_groups);

    // Accessors
    Group& getGroup(GroupId id) {
        return *groups_[id];
    }
    GroupExpr& getExpr(ExprId id) {
        return *exprs_[id];
    }

    GroupId newGroupId();
    ExprId newExprId();

private:
    std::vector<std::unique_ptr<Group>> groups_;
    std::vector<std::unique_ptr<GroupExpr>> exprs_;
    GroupId next_group_id_ = 0;
    ExprId next_expr_id_ = 0;
};

// ============================================================
// Helper: set the child field of a BoundLogicalOperator.
// The child field of leaf operators (BoundScanOp, BoundLabelScanOp) is left unchanged.
// ============================================================
void setChild(binder::BoundLogicalOperator& op, binder::BoundLogicalOperator child);

// Get the number of children for a BoundLogicalOperator (0, 1).
int getChildCount(const binder::BoundLogicalOperator& op);

} // namespace optimizer
} // namespace eugraph
