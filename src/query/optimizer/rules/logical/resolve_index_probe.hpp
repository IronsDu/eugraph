#pragma once

#include "query/optimizer/framework/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

/// R2 — logical refinement rule: resolve an unresolved `IndexProbe` to a
/// concrete catalog index *inside the probe's own group*.
///
/// Soundness boundary: only probes whose destination variable carries explicit
/// labels (from the Expand's label set) are resolved. Scanning label L's index
/// is equivalent to the unlabeled probe only when every possible destination
/// vertex is known to be L-labeled, which is exactly what an explicit pattern
/// label states. Unlabeled destinations stay unresolved rather than guessing a
/// "covering" label (see docs/query/engine/optimizer-next-steps.md P4).
class ResolveIndexProbeRule : public OptRule {
public:
    std::string name() const override {
        return "ResolveIndexProbe";
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

} // namespace optimizer
} // namespace eugraph
