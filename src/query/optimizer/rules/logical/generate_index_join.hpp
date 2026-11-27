#pragma once

#include "query/optimizer/framework/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

/// Logical access-path candidate: `Filter(dst.prop cmp literal)` over a chain of
/// Expand/Project/Filter/Distinct steps becomes a `BoundIndexJoinOp` whose child
/// group is the chain's source subtree. The physical lowering probes the
/// destination index and walks the edge chain back to the source (see
/// PhysicalPlanner::planIndexJoin).
///
/// This rule is a candidate generator, not a planner: the tag/cost/winner are
/// decided by ImplIndexJoinRule + the cost model. It still carries two
/// deliberate pruning heuristics from the pre-CBO era (bare-scan source with a
/// large estimate, and `build_left`), which are candidates for moving into the
/// cost model when the IndexJoin tag gets descriptor-driven costing.
class GenerateIndexJoinRule : public OptRule {
public:
    std::string name() const override {
        return "GenerateIndexJoin";
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

} // namespace optimizer
} // namespace eugraph
