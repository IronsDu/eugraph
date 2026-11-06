#include "query/optimizer/rules/logical/edge_uniqueness_pushdown.hpp"

#include "query/planner/bound_expression/bound_function_call.hpp"
#include "query/planner/bound_expression/bound_property_ref.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"

namespace eugraph {
namespace optimizer {

namespace {

using ExpandOp = binder::BoundExpandOp;

const binder::BoundFunctionCall* edgeUniqueCall(const binder::BoundFilterOp& filter) {
    auto* call = std::get_if<std::unique_ptr<binder::BoundFunctionCall>>(&filter.predicate);
    if (!call || !*call || !(*call)->func_def || (*call)->func_def->name != "__edge_unique")
        return nullptr;
    return call->get();
}

std::string edgeArgName(const binder::BoundExpression& expr) {
    if (auto* ref = std::get_if<binder::BoundColumnRef>(&expr))
        return ref->name;
    if (auto* ref = std::get_if<binder::BoundVariableRef>(&expr))
        return ref->name;
    return {};
}

/// Collect a linear fixed-length Expand chain starting at gid. Chain is
/// top-down: chain[0] is the last hop, chain.back() the first hop. Returns
/// false when the group has more than one logical alternative: at that point
/// the shape is ambiguous and this rule must not guess.
bool collectExpandChain(GroupId gid, Memo& memo, std::vector<const binder::BoundLogicalOperator*>& chain,
                        GroupId& bottom_child_group) {
    for (;;) {
        Group& group = memo.getGroup(gid);
        if (group.logical_exprs.size() != 1)
            return false;
        GroupExpr& expr = memo.getExpr(group.logical_exprs[0]);
        auto* expand = std::get_if<std::unique_ptr<ExpandOp>>(&expr.op);
        if (!expand || !*expand) {
            if (chain.empty())
                return false;
            bottom_child_group = gid;
            return true;
        }
        if (expr.child_groups.size() != 1)
            return false;
        chain.push_back(&expr.op);
        gid = expr.child_groups[0];
    }
}

const ExpandOp* asExpand(const binder::BoundLogicalOperator& op) {
    auto* expand = std::get_if<std::unique_ptr<ExpandOp>>(&op);
    return (expand && *expand) ? expand->get() : nullptr;
}

/// The predicate args are in written/bottom-up order e0,e1,...; the chain is
/// top-down. Return true only when they describe exactly the same chain.
bool matchesChain(const binder::BoundFunctionCall& call,
                  const std::vector<const binder::BoundLogicalOperator*>& chain) {
    if (chain.size() < 2 || call.args.size() != chain.size())
        return false;
    for (size_t i = 0; i < chain.size(); ++i) {
        const ExpandOp* expand = asExpand(*chain[chain.size() - 1 - i]);
        if (!expand || expand->edge_variable.empty() || edgeArgName(call.args[i]) != expand->edge_variable)
            return false;
    }
    return true;
}

binder::BoundLogicalOperator makeUniqueFilter(const binder::BoundFunctionCall& call, size_t prefix) {
    auto unique_call = std::make_unique<binder::BoundFunctionCall>();
    unique_call->func_def = call.func_def;
    unique_call->return_type = binder::BoundType::Bool();
    unique_call->args.reserve(prefix);
    for (size_t i = 0; i < prefix; ++i)
        unique_call->args.push_back(cloneBoundExpression(call.args[i]));

    auto filter = std::make_unique<binder::BoundFilterOp>();
    filter->predicate = binder::BoundExpression(std::move(unique_call));
    return binder::BoundLogicalOperator(std::move(filter));
}

std::unique_ptr<GroupExpr> makeFinalFilter(GroupId root_group, const binder::BoundFunctionCall& call, size_t prefix,
                                           GroupId child_group) {
    auto filter = makeUniqueFilter(call, prefix);
    return std::make_unique<GroupExpr>(INVALID_EXPR_ID, root_group, std::move(filter),
                                       std::vector<GroupId>{child_group});
}

} // namespace

bool EdgeUniquenessPushdownRule::condition(GroupExpr& expr, Memo& memo) const {
    if (expr.child_groups.size() != 1)
        return false;
    auto* filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!filter || !*filter)
        return false;
    const binder::BoundFunctionCall* call = edgeUniqueCall(**filter);
    if (!call)
        return false;

    std::vector<const binder::BoundLogicalOperator*> chain;
    GroupId bottom_child_group = INVALID_GROUP_ID;
    return collectExpandChain(expr.child_groups[0], memo, chain, bottom_child_group) && matchesChain(*call, chain);
}

std::vector<std::unique_ptr<GroupExpr>> EdgeUniquenessPushdownRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto* filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!filter || !*filter)
        return {};
    const binder::BoundFunctionCall* call = edgeUniqueCall(**filter);
    if (!call)
        return {};

    std::vector<const binder::BoundLogicalOperator*> chain;
    GroupId bottom_child_group = INVALID_GROUP_ID;
    if (!collectExpandChain(expr.child_groups[0], memo, chain, bottom_child_group) || !matchesChain(*call, chain))
        return {};

    GroupId current = bottom_child_group;
    for (size_t bi = 0; bi < chain.size(); ++bi) {
        const binder::BoundLogicalOperator& old_expand = *chain[chain.size() - 1 - bi];
        auto expand = cloneBoundLogicalOperator(old_expand);
        GroupExpr* expand_expr = memo.createGroupWithExpr(std::move(expand), {current});
        current = expand_expr->group_id;

        const size_t prefix = bi + 1;
        if (prefix < 2)
            continue;
        if (prefix == chain.size()) {
            std::vector<std::unique_ptr<GroupExpr>> result;
            result.push_back(makeFinalFilter(expr.group_id, *call, prefix, current));
            return result;
        }
        GroupExpr* prefix_filter = memo.createGroupWithExpr(makeUniqueFilter(*call, prefix), {current});
        current = prefix_filter->group_id;
    }
    return {};
}

} // namespace optimizer
} // namespace eugraph
