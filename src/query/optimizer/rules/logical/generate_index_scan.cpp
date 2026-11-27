#include "query/optimizer/rules/logical/generate_index_scan.hpp"

#include "query/optimizer/analysis/index_access_path.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"

namespace eugraph {
namespace optimizer {

PatternNode GenerateIndexScanRule::pattern() const {
    return {OptNodeType::Filter, {{}}};
}

std::vector<std::unique_ptr<GroupExpr>> GenerateIndexScanRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto* fp = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!fp || !*fp || expr.child_groups.empty() || memo.getCatalog() == nullptr)
        return {};
    Group& child = memo.getGroup(expr.child_groups[0]);
    if (child.logical_exprs.size() != 1)
        return {};
    const auto* scan = std::get_if<binder::BoundLabelScanOp>(&memo.getExpr(child.logical_exprs[0]).op);
    if (!scan || scan->label_ids.size() != 1 || scan->index_scan || scan->index_scan_values)
        return {};

    binder::BoundFilterOp temp;
    temp.predicate = cloneBoundExpression((*fp)->predicate);
    temp.uniqueness_scope = (*fp)->uniqueness_scope;
    temp.child = *scan;
    binder::BoundLogicalOperator temp_op = std::make_unique<binder::BoundFilterOp>(std::move(temp));
    applyIndexAccessPaths(temp_op, *memo.getCatalog(), memo.getSlotSemantics());
    auto* chosen = std::get_if<binder::BoundLabelScanOp>(&temp_op);
    if (!chosen || !chosen->index_scan)
        return {};

    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(
        std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(*chosen), std::vector<GroupId>{}));
    return result;
}

} // namespace optimizer
} // namespace eugraph
