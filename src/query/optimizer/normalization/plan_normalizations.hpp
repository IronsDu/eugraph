#pragma once

#include "query/optimizer/normalization/normalization_pass.hpp"

namespace eugraph {
namespace optimizer {

/// 语义规范化：给 Filter 下的 Cross/Inner Join 填等值 join 条件（`join_conditions`），
/// 保留 Filter 残差。**不做选择**：不选 join 算法/tag、不比成本；`join_conditions` 是
/// CBO 的事实输入，算法/tag 由 Impl 规则产出、成本模型选 winner（B1/B2 契约）。
class JoinConditionNormalization final : public NormalizationPass {
public:
    std::string name() const override {
        return "JoinConditionNormalization";
    }
    void apply(binder::BoundLogicalOperator& root, NormalizationContext& ctx) const override;
};

/// 标注 correlated 端点 Expand 的执行策略（只写 probe_bound_dst，不改形状）：
///
///   Expand(dst=<子树 CorrelatedSource 提供的列>, 有向, 只读语句)
///   => probe_bound_dst = true
///
/// 形状由 binder 直接生成（P2）：链端点绑定到相关列，Expand 直接 seek 该目的端
/// （dst=bound），不再产生 `Filter(id(dst) == id(saved))` 等折叠。本 pass 只标注
/// “可把目的端邻接按批物化一次，再在内存里探源行”；算子保留运行时预算并回退到
/// 批量精确 seek。等 P3 有度数统计后，这个确定性标注升级为 Expand 的 Impl 候选
/// （PhysicalExpr 属性 + 成本），binder 与 physical lowering 都不用再动。
class AnnotateCorrelatedEndpointProbeNormalization final : public NormalizationPass {
public:
    std::string name() const override {
        return "AnnotateCorrelatedEndpointProbe";
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
