#pragma once

#include "query/optimizer/framework/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

/// Logical access-path alternative: Filter(LabelScan) where every predicate is
/// consumed by an index range. Produces a LabelScan carrying an index
/// descriptor; ImplLabelScan / ImplIndexScan compete on cost.
class GenerateIndexScanRule : public OptRule {
public:
    std::string name() const override {
        return "GenerateIndexScan";
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

} // namespace optimizer
} // namespace eugraph
