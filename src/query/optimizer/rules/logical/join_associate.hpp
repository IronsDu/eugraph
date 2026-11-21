#pragma once
#include "query/optimizer/framework/opt_rule.hpp"
namespace eugraph {
namespace optimizer {
/// 只读 Cross/Inner join 结合律：Join(Join(A,B),C) -> Join(A,Join(B,C))。
/// 安全边界与 JoinCommute 相同（写计划/pattern comprehension 不触发）。
class JoinAssociateRule : public OptRule {
public:
    std::string name() const override {
        return "JoinAssociate";
    }
    PatternNode pattern() const override;
    bool condition(GroupExpr& expr, Memo& memo) const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};
} // namespace optimizer
} // namespace eugraph
