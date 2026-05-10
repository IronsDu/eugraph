#pragma once

#include "compute_service/optimizer/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

class FilterPushdownRule : public OptRule {
public:
    std::string name() const override {
        return "FilterPushdown";
    }

    PatternNode pattern() const override {
        // Match Filter(Leaf) — a Filter with one child
        return PatternNode{OptNodeType::Filter, {{}}};
    }

    bool condition(GroupExpr& expr, Memo& memo) const override;

    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;

    int promise() const override {
        return 2;
    }
};

bool isPenetrable(OptNodeType type);

} // namespace optimizer
} // namespace eugraph
