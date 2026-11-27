#pragma once

#include "query/optimizer/framework/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

/// Logical equivalence: `Filter(r.p IS NOT NULL)` directly above an Expand can
/// narrow the Expand's edge labels to the labels that declare `p`.
///
/// Sound because edges are strong-only (there is no weak/anon edge property
/// space): an edge of another type can never carry `p`, so it can never pass
/// `IS NOT NULL`. Vertex properties do NOT get this treatment: the weak space
/// allows values outside the declaring label, so a vertex scan must not be
/// pruned by schema alone.
///
/// One job only: consume a single `IS NOT NULL` edge-property predicate when it
/// is the whole Filter. Mixed predicates are left to other rules.
class PruneExpandEdgeLabelsRule : public OptRule {
public:
    std::string name() const override {
        return "PruneExpandEdgeLabels";
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

} // namespace optimizer
} // namespace eugraph
