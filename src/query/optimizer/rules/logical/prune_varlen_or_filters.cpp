#include "query/optimizer/rules/logical/prune_varlen_or_filters.hpp"

#include "query/optimizer/analysis/index_condition.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

namespace eugraph {
namespace optimizer {

namespace {

/// Find a usable (PUBLIC, current encoding) vertex index on `label` whose
/// accessors cover `property_name`. Returns 0 when none exists.
uint32_t findUsableIndex(const catalog::Catalog& catalog, LabelId label, const std::string& property_name) {
    auto it = catalog.allLabels().find(label);
    if (it == catalog.allLabels().end())
        return 0;
    for (const auto& index : it->second.indexes) {
        if (index.index_id == 0 || index.state != IndexState::PUBLIC || index.encoding_version != 2)
            continue;
        for (const auto& accessor : index.accessors) {
            if (accessor.property_name == property_name)
                return index.index_id;
        }
    }
    return 0;
}

} // namespace

PatternNode PruneVarlenOrFiltersRule::pattern() const {
    return {OptNodeType::Filter, {{}}};
}

std::vector<std::unique_ptr<GroupExpr>> PruneVarlenOrFiltersRule::substitute(GroupExpr& expr, Memo& memo) const {
    const auto* filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!filter || !*filter || expr.child_groups.empty())
        return {};

    const auto* or_op = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&(*filter)->predicate);
    if (!or_op || !*or_op || (*or_op)->op != cypher::BinaryOperator::OR)
        return {};

    // Child group must contain exactly one VarLenExpand expression.
    Group& child_group = memo.getGroup(expr.child_groups[0]);
    if (child_group.logical_exprs.size() != 1)
        return {};
    GroupExpr& child_expr = memo.getExpr(child_group.logical_exprs[0]);
    const auto* vle = std::get_if<std::unique_ptr<binder::BoundVarLenExpandOp>>(&child_expr.op);
    if (!vle || !*vle || child_expr.child_groups.size() != 1)
        return {};
    const binder::BoundVarLenExpandOp& expand = **vle;

    auto resolver = slotSemanticColumnResolver(memo.getSlotSemantics());
    auto side_condition = [&](const binder::BoundExpression& side) -> std::optional<IndexableCondition> {
        const auto* comparison = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&side);
        if (!comparison || !*comparison || (*comparison)->op != cypher::BinaryOperator::EQ)
            return std::nullopt;
        auto condition = extractNormalizedIndexableCondition(**comparison, INVALID_LABEL_ID, resolver);
        if (!condition || condition->property_name.empty() || condition->variable.empty())
            return std::nullopt;
        return condition;
    };

    auto left = side_condition((*or_op)->left);
    auto right = side_condition((*or_op)->right);
    if (!left || !right)
        return {};

    std::optional<IndexableCondition> src_condition;
    std::optional<IndexableCondition> dst_condition;
    if (left->variable == expand.src_variable && right->variable == expand.dst_variable) {
        src_condition = std::move(left);
        dst_condition = std::move(right);
    } else if (left->variable == expand.dst_variable && right->variable == expand.src_variable) {
        src_condition = std::move(right);
        dst_condition = std::move(left);
    } else {
        return {};
    }

    const catalog::Catalog* catalog = memo.getCatalog();
    if (catalog == nullptr)
        return {};

    LabelId src_label = INVALID_LABEL_ID;
    const auto& scan_expr = memo.getExpr(child_expr.child_groups[0]);
    if (const auto* scan = std::get_if<binder::BoundLabelScanOp>(&scan_expr.op); scan && scan->label_ids.size() == 1)
        src_label = scan->label_ids.front();
    const LabelId dst_label = expand.dst_label_ids.size() == 1 ? expand.dst_label_ids.front() : INVALID_LABEL_ID;
    if (src_label == INVALID_LABEL_ID || dst_label == INVALID_LABEL_ID)
        return {};

    const uint32_t src_index = src_condition->op == cypher::BinaryOperator::EQ
                                   ? findUsableIndex(*catalog, src_label, src_condition->property_name)
                                   : 0;
    const uint32_t dst_index = dst_condition->op == cypher::BinaryOperator::EQ
                                   ? findUsableIndex(*catalog, dst_label, dst_condition->property_name)
                                   : 0;
    const uint32_t effective_src = src_index != 0 ? src_index : expand.src_filter_index_id;
    const uint32_t effective_dst = dst_index != 0 ? dst_index : expand.dst_filter_index_id;
    if (effective_src == 0 || effective_dst == 0)
        return {};

    auto pruned = std::get<std::unique_ptr<binder::BoundVarLenExpandOp>>(cloneBoundLogicalOperator(child_expr.op));
    if (!pruned)
        return {};
    if (src_index != 0) {
        pruned->src_filter_index_id = src_index;
        pruned->src_filter_value = src_condition->value;
    }
    if (dst_index != 0) {
        pruned->dst_filter_index_id = dst_index;
        pruned->dst_filter_value = dst_condition->value;
    }

    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(pruned),
                                                 std::vector<GroupId>{child_expr.child_groups[0]}));
    return result;
}

} // namespace optimizer
} // namespace eugraph
