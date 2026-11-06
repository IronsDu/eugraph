#pragma once

#include "query/optimizer/framework/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

/// Push the semantic `__edge_unique(e0..en)` predicate of a fixed-length
/// relationship chain down to its prefixes.
///
/// The binder emits one global uniqueness Filter after a MATCH clause. This
/// rule recognises the fixed-chain shape and rewrites it to:
///
///   Expand(e2) -> Filter(e0,e1,e2) -> Expand(e1) -> Filter(e0,e1) -> Expand(e0)
///
/// so a reused edge is rejected before the next hop expands it. Varlen and
/// multi-pattern plans keep the global filter: their relationship set is not a
/// single fixed prefix chain.
class EdgeUniquenessPushdownRule : public OptRule {
public:
    std::string name() const override {
        return "EdgeUniquenessPushdown";
    }

    PatternNode pattern() const override {
        // The child shape is inspected in condition(): it must be a linear
        // fixed-length Expand chain, not an arbitrary subtree.
        return PatternNode{OptNodeType::Filter, {{}}};
    }

    bool condition(GroupExpr& expr, Memo& memo) const override;

    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;

    int promise() const override {
        return 3;
    }
};

} // namespace optimizer
} // namespace eugraph
