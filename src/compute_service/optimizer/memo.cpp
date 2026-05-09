#include "compute_service/optimizer/memo.hpp"

#include "compute_service/planner/bound_logical_plan.hpp"

namespace eugraph {
namespace optimizer {

// ============================================================
// Helper: setChild — rebuild child reference in a BoundLogicalOperator
// ============================================================
void setChild(binder::BoundLogicalOperator& op, binder::BoundLogicalOperator child) {
    std::visit(
        [&child](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                // Leaf operators — no child field
            } else {
                // All non-leaf operators are wrapped in unique_ptr
                // BoundCreateNodeOp has optional<BoundLogicalOperator> child,
                // which is assignable from BoundLogicalOperator
                val->child = std::move(child);
            }
        },
        op);
}

int getChildCount(const binder::BoundLogicalOperator& op) {
    return std::visit(
        [](const auto& val) -> int {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                return 0;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                return val->child.has_value() ? 1 : 0;
            } else {
                return 1;
            }
        },
        op);
}

// ============================================================
// Memo
// ============================================================

GroupId Memo::newGroupId() {
    auto id = next_group_id_++;
    groups_.push_back(std::make_unique<Group>(id));
    return id;
}

ExprId Memo::newExprId() {
    return next_expr_id_++;
}

GroupId Memo::copyIn(binder::BoundLogicalOperator& op) {
    // Recursively insert children first (bottom-up)
    std::vector<GroupId> child_groups;
    if (getChildCount(op) == 1) {
        binder::BoundLogicalOperator child = std::visit(
            [](auto& val) -> binder::BoundLogicalOperator {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                    // Should not reach here
                    return binder::BoundScanOp{};
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                    // BoundCreateNodeOp has optional<BoundLogicalOperator> child
                    auto c = std::move(val->child.value());
                    val->child = std::nullopt;
                    return c;
                } else {
                    // All other non-leaf operators have BoundLogicalOperator child
                    auto c = std::move(val->child);
                    val->child = binder::BoundScanOp{};
                    return c;
                }
            },
            op);
        child_groups.push_back(copyIn(child));
    }

    // Create a new group for this operator
    GroupId gid = newGroupId();
    ExprId eid = newExprId();
    auto expr = std::make_unique<GroupExpr>(eid, gid, std::move(op), std::move(child_groups));
    groups_[gid]->logical_exprs.push_back(eid);
    exprs_.push_back(std::move(expr));
    return gid;
}

binder::BoundLogicalOperator Memo::copyOut(GroupId root_gid) {
    Group& group = getGroup(root_gid);
    // RBO: take the last (most recently added) logical expression
    ExprId eid = group.logical_exprs.back();
    GroupExpr& expr = getExpr(eid);

    // Move the operator out — copyOut is called once after optimization completes
    binder::BoundLogicalOperator result = std::move(expr.op);

    // Recursively rebuild children
    if (!expr.child_groups.empty()) {
        auto child = copyOut(expr.child_groups[0]);
        setChild(result, std::move(child));
    }

    return result;
}

GroupExpr* Memo::findDuplicate(const GroupExpr& expr) {
    // Simple linear search for now; can be optimized with a hash table
    for (const auto& existing : exprs_) {
        if (existing->group_id == expr.group_id && existing->child_groups == expr.child_groups) {
            // Check operator equivalence by comparing variant index
            if (existing->op.index() != expr.op.index())
                continue;
            // For simplicity, treat same type + same children as duplicate
            return existing.get();
        }
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

    return g1;
}

GroupExpr* Memo::insertExpr(std::unique_ptr<GroupExpr> expr, GroupId target_group) {
    expr->group_id = target_group;
    GroupExpr* raw = expr.get();
    exprs_.push_back(std::move(expr));
    getGroup(target_group).logical_exprs.push_back(raw->id);
    return raw;
}

GroupExpr* Memo::createGroupWithExpr(binder::BoundLogicalOperator op, std::vector<GroupId> child_groups) {
    GroupId gid = newGroupId();
    ExprId eid = newExprId();
    auto expr = std::make_unique<GroupExpr>(eid, gid, std::move(op), std::move(child_groups));
    GroupExpr* raw = expr.get();
    groups_[gid]->logical_exprs.push_back(eid);
    exprs_.push_back(std::move(expr));
    return raw;
}

} // namespace optimizer
} // namespace eugraph
