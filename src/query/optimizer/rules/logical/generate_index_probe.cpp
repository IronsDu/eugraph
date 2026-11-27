#include "query/optimizer/rules/logical/generate_index_probe.hpp"

#include "query/optimizer/analysis/index_condition.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_op.hpp"

#include <functional>

namespace eugraph {
namespace optimizer {

namespace {

/// The parts of an Expand that a destination probe needs. Values are copied out
/// of the child group *before* the rule mutates the memo (copyIn may grow the
/// group vector, invalidating references).
struct ExploreStep {
    std::string source_variable;
    std::string target_variable;
    std::string edge_variable;
    std::vector<EdgeLabelId> edge_label_ids;
    cypher::RelationshipDirection direction = cypher::RelationshipDirection::UNDIRECTED;
    std::vector<LabelId> target_label_ids;
    GroupId anchor_group = INVALID_GROUP_ID;
    bool found = false;
};

/// Locate the single Expand expression of `gid`. Returns false when the group
/// has no Expand, or several Expand alternatives with different anchor groups:
/// a shape rule must not pick one of them arbitrarily.
bool findExpandStep(Memo& memo, GroupId gid, ExploreStep& out) {
    Group& group = memo.getGroup(gid);
    bool found = false;
    for (ExprId eid : group.logical_exprs) {
        const auto& op = memo.getExpr(eid).op;
        const auto* expand = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&op);
        if (!expand || !*expand)
            continue;
        const GroupExpr& expand_expr = memo.getExpr(eid);
        if (expand_expr.child_groups.size() != 1)
            return false;
        if (found) {
            // Several Expand alternatives: only proceed when they agree on the
            // anchor group, otherwise the ProbeJoin would silently pick one.
            if (out.anchor_group != expand_expr.child_groups[0])
                return false;
            continue;
        }
        out.source_variable = (*expand)->src_variable;
        out.target_variable = (*expand)->dst_variable;
        out.edge_variable = (*expand)->edge_variable;
        out.edge_label_ids = (*expand)->edge_label_ids;
        out.direction = (*expand)->direction;
        out.target_label_ids = (*expand)->dst_label_ids;
        out.anchor_group = expand_expr.child_groups[0];
        found = true;
    }
    out.found = found;
    return found;
}

/// Split a predicate into its top-level AND conjuncts.
void splitConjuncts(const binder::BoundExpression& pred, std::vector<const binder::BoundExpression*>& out) {
    const auto* binop = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&pred);
    if (binop && *binop && (*binop)->op == cypher::BinaryOperator::AND) {
        splitConjuncts((*binop)->left, out);
        splitConjuncts((*binop)->right, out);
        return;
    }
    out.push_back(&pred);
}

} // namespace

PatternNode GenerateIndexProbeRule::pattern() const {
    return {OptNodeType::Filter, {{}}};
}

std::vector<std::unique_ptr<GroupExpr>> GenerateIndexProbeRule::substitute(GroupExpr& expr, Memo& memo) const {
    const auto* filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!filter || !*filter || expr.child_groups.empty())
        return {};

    // Same conservative boundary as join commute: write plans (and shared-anchor
    // pattern comprehensions) have not been audited for extra join alternatives
    // / output-slot layout, and they frequently fall back to RBO (no CBO
    // winner), so an access-path candidate must not be injected there.
    if (!memo.commuteSafe())
        return {};

    ExploreStep step;
    if (!findExpandStep(memo, expr.child_groups[0], step) || step.found == false)
        return {};
    if (step.source_variable.empty() || step.target_variable.empty() || step.edge_label_ids.empty())
        return {};

    // Every top-level conjunct must be an indexable comparison on the very same
    // property of the Expand's destination variable. Anything else would be a
    // dropped predicate, so refuse to build the probe.
    std::vector<const binder::BoundExpression*> conjuncts;
    splitConjuncts((*filter)->predicate, conjuncts);
    if (conjuncts.empty())
        return {};

    auto resolver = slotSemanticColumnResolver(memo.getSlotSemantics());
    std::string property_name;
    std::optional<PropertyValue> equality;
    IndexRange range;
    for (const auto* conjunct : conjuncts) {
        const auto* binop = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(conjunct);
        if (!binop || !*binop)
            return {};
        auto cond = extractNormalizedIndexableCondition(**binop, INVALID_LABEL_ID, resolver);
        if (!cond || cond->variable != step.target_variable || cond->property_name.empty())
            return {};
        if (property_name.empty())
            property_name = cond->property_name;
        else if (property_name != cond->property_name)
            return {};
        switch (cond->op) {
        case cypher::BinaryOperator::EQ:
            if (equality)
                return {}; // several equalities on one property: not a range probe
            equality = cond->value;
            break;
        case cypher::BinaryOperator::GT:
            mergeIndexLower(range, cond->value, false);
            break;
        case cypher::BinaryOperator::GTE:
            mergeIndexLower(range, cond->value, true);
            break;
        case cypher::BinaryOperator::LT:
            mergeIndexUpper(range, cond->value, false);
            break;
        case cypher::BinaryOperator::LTE:
            mergeIndexUpper(range, cond->value, true);
            break;
        default:
            return {};
        }
    }
    if (property_name.empty())
        return {};
    // An equality already implies every range on the same property; mixing them
    // is not representable by the descriptor, so reject instead of guessing.
    if (equality && (range.lower || range.upper))
        return {};

    binder::BoundIndexProbeOp probe;
    probe.source_variable = step.source_variable;
    probe.target_variable = step.target_variable;
    probe.edge_variable = step.edge_variable;
    probe.edge_label_ids = step.edge_label_ids;
    probe.direction = step.direction;
    probe.target_label_ids = step.target_label_ids;
    probe.index_property_name = property_name;
    probe.equality = equality;
    if (range.lower) {
        probe.lower = range.lower->value;
        probe.lower_inclusive = range.lower->inclusive;
    }
    if (range.upper) {
        probe.upper = range.upper->value;
        probe.upper_inclusive = range.upper->inclusive;
    }
    // R1 is shape-only: the index, label and property id are resolved by a
    // separate rule on the probe group.

    auto probe_join = std::make_unique<binder::BoundIndexProbeJoinOp>();
    probe_join->probe = probe; // transport mirror for copyIn/copyOut

    binder::BoundLogicalOperator probe_tree = std::make_unique<binder::BoundIndexProbeOp>(std::move(probe));
    GroupId probe_group = memo.copyIn(probe_tree);

    std::vector<GroupId> children{step.anchor_group, probe_group};
    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(
        std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(probe_join), std::move(children)));
    return result;
}

} // namespace optimizer
} // namespace eugraph
