#pragma once

#include "query/optimizer/normalization/normalization_pass.hpp"

namespace eugraph {
namespace optimizer {

/// 给 Filter 下的 Cross/Inner Join 填等值 join 条件；保留 Filter 残差。
class JoinConditionNormalization final : public NormalizationPass {
public:
    std::string name() const override {
        return "JoinConditionNormalization";
    }
    void apply(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const override;
};

/// 选择 bounded multi-target shortestPath 的 batch 执行策略标注。
class ShortestPathBatchNormalization final : public NormalizationPass {
public:
    std::string name() const override {
        return "ShortestPathBatchNormalization";
    }
    void apply(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const override;
};

/// 把 binder 生成的 correlated 端点过滤折叠进 Expand：
///
///   Filter(id(__exists_dst_N) == id(__exists_saved_N))
///     Expand(dst=__exists_dst_N, ...)
///   =>
///   Expand(dst=__exists_saved_N, probe_bound_dst=...)
///
/// 这是语义等价的形状规范化，必须在 CBO/requirements 之前完成，让优化器对最终
/// 形状计成本；physical planner 只实例化已折叠的 Expand。
class FoldExpandEndpointFilterNormalization final : public NormalizationPass {
public:
    std::string name() const override {
        return "FoldExpandEndpointFilter";
    }
    void apply(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const override;
};

/// 计算是否允许 join commute/associate，写入 NormalizationContext。
class JoinSafetyNormalization final : public NormalizationPass {
public:
    std::string name() const override {
        return "JoinSafetyNormalization";
    }
    void apply(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const override;
};

} // namespace optimizer
} // namespace eugraph
