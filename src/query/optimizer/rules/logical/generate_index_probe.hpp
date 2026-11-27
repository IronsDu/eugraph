#pragma once

#include "query/optimizer/framework/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

/// R1 — logical shape rule: `Filter(range on Expand.dst.prop) -> Expand(src, dst)`
/// produces an `IndexProbe` leaf in its own group plus a `ProbeJoin` candidate
/// whose anchor is the Expand's complete child group.
///
/// The rule is deliberately shape-only:
/// - it never reads the catalog, so it never chooses an index (R2 does);
/// - it never costs a plan or picks a winner (the cascade framework does);
/// - it never re-derives the anchor: the whole left pipeline comes from the
///   existing child group.
class GenerateIndexProbeRule : public OptRule {
public:
    std::string name() const override {
        return "GenerateIndexProbe";
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

} // namespace optimizer
} // namespace eugraph
