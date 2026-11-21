#include "query/optimizer/rules/logical/join_associate.hpp"

#include "query/optimizer/framework/memo.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"

namespace eugraph {
namespace optimizer {
namespace {

bool commutableType(binder::JoinType type) {
    return type == binder::JoinType::Cross || type == binder::JoinType::Inner;
}

GroupExpr* findJoinExpr(Group& group, Memo& memo) {
    for (ExprId id : group.logical_exprs) {
        GroupExpr& e = memo.getExpr(id);
        if (auto* join = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&e.op)) {
            if (join && *join && commutableType((*join)->join_type))
                return &e;
        }
    }
    return nullptr;
}

} // namespace

PatternNode JoinAssociateRule::pattern() const {
    return {OptNodeType::BinaryJoin, {{}, {}}};
}

bool JoinAssociateRule::condition(GroupExpr& expr, Memo& memo) const {
    if (!memo.commuteSafe())
        return false;
    auto* outer = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&expr.op);
    if (!outer || !*outer || !commutableType((*outer)->join_type))
        return false;
    if (expr.child_groups.size() != 2)
        return false;
    return findJoinExpr(memo.getGroup(expr.child_groups[0]), memo) != nullptr;
}

std::vector<std::unique_ptr<GroupExpr>> JoinAssociateRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto* outer = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&expr.op);
    if (!outer || !*outer || expr.child_groups.size() != 2)
        return {};
    GroupExpr* inner_expr = findJoinExpr(memo.getGroup(expr.child_groups[0]), memo);
    if (inner_expr == nullptr || inner_expr->child_groups.size() != 2)
        return {};

    const GroupId a_group = inner_expr->child_groups[0];
    const GroupId b_group = inner_expr->child_groups[1];
    const GroupId c_group = expr.child_groups[1];
    auto* inner_op = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&inner_expr->op);
    if (inner_op == nullptr || !*inner_op)
        return {};

    auto new_inner = std::make_unique<binder::BoundBinaryJoinOp>();
    new_inner->join_type = (*outer)->join_type;
    GroupExpr* new_inner_expr =
        memo.createGroupWithExpr(binder::BoundLogicalOperator(std::move(new_inner)), {b_group, c_group});
    if (new_inner_expr == nullptr)
        return {};

    auto new_outer = std::make_unique<binder::BoundBinaryJoinOp>();
    new_outer->join_type = (*outer)->join_type;
    // join_conditions 是逻辑 hint；Filter 仍保留原谓词。重排后统一挂到最外层，
    // 保持“这是 equi-join”的信息，不把条件下放到错误子树。
    for (const auto& cond : (*inner_op)->join_conditions)
        new_outer->join_conditions.push_back(cloneBoundExpression(cond));
    for (const auto& cond : (*outer)->join_conditions)
        new_outer->join_conditions.push_back(cloneBoundExpression(cond));

    auto ge =
        std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, binder::BoundLogicalOperator(std::move(new_outer)),
                                    std::vector<GroupId>{a_group, new_inner_expr->group_id});
    std::vector<std::unique_ptr<GroupExpr>> out;
    out.push_back(std::move(ge));
    return out;
}

} // namespace optimizer
} // namespace eugraph
