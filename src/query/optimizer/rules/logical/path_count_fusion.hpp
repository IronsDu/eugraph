#pragma once

#include "query/optimizer/framework/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

/// Rewrite a mandatory fixed-length OUT path ending in a non-DISTINCT count
/// into a logical BoundPathCountOp. The physical implementation then counts
/// the final edge choices from each N-1-hop prefix row.
class PathCountFusionRule : public OptRule {
public:
    std::string name() const override {
        return "PathCountFusion";
    }

    PatternNode pattern() const override {
        return PatternNode{OptNodeType::Aggregate, {{}}};
    }

    bool condition(GroupExpr& expr, Memo& memo) const override;

    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;

    int promise() const override {
        return 4;
    }
};

} // namespace optimizer
} // namespace eugraph
