#pragma once

#include "query/optimizer/framework/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

/// Logical equivalent for `Filter(src.p = a OR dst.q = b)` directly above a
/// VarLenExpand: attach the optimizer-selected src/dst index allow-sets to the
/// VLE descriptor (the operator emits only paths where at least one endpoint
/// matches, i.e. exactly the OR predicate, so the Filter can be consumed).
///
/// Both endpoints must resolve to an index: with only one side active the VLE's
/// OR emission degenerates to "always true", which is NOT equivalent to the
/// predicate. The physical operator keeps its own OR emission logic; the
/// planner only consumes the descriptor.
class PruneVarlenOrFiltersRule : public OptRule {
public:
    std::string name() const override {
        return "PruneVarlenOrFilters";
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

} // namespace optimizer
} // namespace eugraph
