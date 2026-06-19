#include "query/optimizer/opt_rule.hpp"

#include "query/planner/bound_logical_plan.hpp"

namespace eugraph {
namespace optimizer {

OptNodeType nodeTypeFromVariantIndex(size_t index) {
    static constexpr OptNodeType mapping[] = {
        OptNodeType::Singleton,        // 0
        OptNodeType::CorrelatedSource, // 1
        OptNodeType::Scan,             // 2
        OptNodeType::LabelScan,        // 3
        OptNodeType::Expand,           // 4
        OptNodeType::Filter,           // 5
        OptNodeType::Project,          // 6
        OptNodeType::Aggregate,        // 7
        OptNodeType::Sort,             // 8
        OptNodeType::Skip,             // 9
        OptNodeType::Limit,            // 10
        OptNodeType::Distinct,         // 11
        OptNodeType::CreateNode,       // 12
        OptNodeType::CreateEdge,       // 13
        OptNodeType::Set,              // 14
        OptNodeType::Remove,           // 15
        OptNodeType::Delete,           // 16
        OptNodeType::PathBuild,        // 17
        OptNodeType::VarLenExpand,     // 18
        OptNodeType::BinaryJoin,       // 19
        OptNodeType::LeftJoin,         // 20
        OptNodeType::SemiJoin,         // 21
        OptNodeType::Unwind,           // 22
        OptNodeType::Union,            // 23
        OptNodeType::Merge             // 24
    };
    return mapping[index];
}

bool OptRule::topMatch(const GroupExpr& expr) const {
    const PatternNode& p = pattern();
    // Always compare root type. The "empty children = wildcard" semantics
    // applies only when a pattern node is used as a CHILD of another pattern
    // (handled in matchNode); a top-level pattern must match its declared
    // operator type. Previously this returned true for any leaf pattern
    // (e.g. {CorrelatedSource, {}}), which caused every leaf-source impl
    // rule to fire on every expression and pollute groups with wrong-tag
    // physical expressions.
    OptNodeType actual = nodeTypeFromVariantIndex(expr.op.index());
    return actual == p.type;
}

bool OptRule::fullMatch(const GroupExpr& expr, Memo& memo) const {
    return matchNode(pattern(), expr, memo);
}

bool OptRule::matchNode(const PatternNode& pattern, const GroupExpr& expr, Memo& memo) const {
    // Root operator must match
    OptNodeType actual = nodeTypeFromVariantIndex(expr.op.index());
    if (actual != pattern.type)
        return false;

    // If pattern is a leaf (no children), match any subtree (wildcard)
    if (pattern.children.empty())
        return true;

    // Pattern has children — must match child groups
    if (expr.child_groups.size() < pattern.children.size())
        return false;

    // For each pattern child, check if the corresponding child group
    // contains at least one expression matching the pattern child.
    // (Columbia: BINDERY creates group binderys for each input)
    for (size_t i = 0; i < pattern.children.size(); ++i) {
        const auto& child_pattern = pattern.children[i];
        GroupId child_gid = expr.child_groups[i];
        Group& child_group = memo.getGroup(child_gid);

        bool found_match = false;
        for (ExprId child_eid : child_group.logical_exprs) {
            GroupExpr& child_expr = memo.getExpr(child_eid);
            if (matchNode(child_pattern, child_expr, memo)) {
                found_match = true;
                break;
            }
        }
        if (!found_match)
            return false;
    }

    return true;
}

} // namespace optimizer
} // namespace eugraph
