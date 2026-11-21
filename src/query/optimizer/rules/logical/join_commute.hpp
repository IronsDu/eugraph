#pragma once
#include "query/optimizer/framework/opt_rule.hpp"
namespace eugraph {
namespace optimizer {
/// Cross/Inner join 可交换。只对“无写算子、无 pattern comprehension”的只读子树
/// 产生候选；这些子树尚存在列顺序/别名假设，先固定边界，后续再放开。
class JoinCommuteRule : public OptRule {
public:
    std::string name() const override {
        return "JoinCommute";
    }
    PatternNode pattern() const override;
    bool condition(GroupExpr& expr, Memo& memo) const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};
} // namespace optimizer
} // namespace eugraph
