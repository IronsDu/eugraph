#include "query/optimizer/analysis/plan_analysis.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"

#include <type_traits>

namespace eugraph {
namespace optimizer {

namespace {

void collectLabels(const binder::BoundLogicalOperator& op, LabelDemand& demand) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v)
                    demand.required_dst_labels.insert(v->dst_label_ids.begin(), v->dst_label_ids.end());
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                demand.required_dst_labels.insert(v.label_ids.begin(), v.label_ids.end());
            }
        },
        op);
    forEachLogicalChild(op, [&](const binder::BoundLogicalOperator& child) { collectLabels(child, demand); });
}

} // namespace

PlanAnalysis analyzePlan(const binder::BoundLogicalOperator& root) {
    PlanAnalysis analysis;
    analysis.property = analyzePropertyDemand(root);
    collectLabels(root, analysis.label);
    return analysis;
}

} // namespace optimizer
} // namespace eugraph
