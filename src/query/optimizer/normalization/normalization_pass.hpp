#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace optimizer {

/// 结果性的计划元数据。NormalizationPass 只建立不变量/标注，不产生
/// Cascades alternative，也不参与代价搜索。
struct NormalizationContext {
    /// 计划中是否存在写算子/pattern comprehension 等不适合 join reorder 的
    /// 结构；由 JoinSafetyNormalization 计算。
    bool commute_safe = true;
};

class NormalizationPass {
public:
    virtual ~NormalizationPass() = default;
    virtual std::string name() const = 0;
    virtual void apply(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const = 0;
};

/// 固定顺序的 normalization pipeline；与 RuleSet 分开，避免把“建立不变量”
/// 误建模成会生成等价 alternative 的 OptRule。
class NormalizationPipeline {
public:
    void add(std::unique_ptr<NormalizationPass> pass) {
        passes_.push_back(std::move(pass));
    }

    void run(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const {
        for (const auto& pass : passes_)
            pass->apply(root, ctx);
    }

private:
    std::vector<std::unique_ptr<NormalizationPass>> passes_;
};

} // namespace optimizer
} // namespace eugraph
