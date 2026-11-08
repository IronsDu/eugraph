#include "query/optimizer/analysis/property_demand.hpp"

#include "query/optimizer/analysis/expression_slots.hpp"
#include "query/optimizer/analysis/logical_plan_walk.hpp"

#include <type_traits>
#include <variant>

namespace eugraph {
namespace optimizer {

namespace {

using detail::collectExprSlots;

void collectSubtreeSlots(const binder::BoundLogicalOperator& op, std::unordered_set<binder::SlotId>& out,
                         bool& has_limit) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v)
                    collectExprSlots(v->predicate, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v)
                    for (const auto& item : v->items)
                        collectExprSlots(item.expr, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    for (const auto& k : v->group_keys)
                        collectExprSlots(k, out);
                    for (const auto& agg : v->aggregates)
                        for (const auto& a : agg.arguments)
                            collectExprSlots(a, out);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                has_limit = true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v)
                    for (const auto& item : v->items)
                        collectExprSlots(item.expr, out);
            }
        },
        op);
    forEachLogicalChild(op,
                        [&](const binder::BoundLogicalOperator& child) { collectSubtreeSlots(child, out, has_limit); });
}

} // namespace

PropertyDemand analyzePropertyDemand(const binder::BoundProjectOp& project, const binder::BoundLogicalOperator& child) {
    PropertyDemand demand;
    std::unordered_set<binder::SlotId> output;
    for (const auto& item : project.items)
        collectExprSlots(item.expr, output);
    collectSubtreeSlots(child, demand.required_below_limit, demand.has_limit);
    for (auto slot : output)
        if (!demand.required_below_limit.contains(slot))
            demand.output_only.insert(slot);
    return demand;
}

PropertyDemand analyzePropertyDemand(const binder::BoundLogicalOperator& root) {
    const auto* project = std::get_if<std::unique_ptr<binder::BoundProjectOp>>(&root);
    if (!project || !*project)
        return {};
    return analyzePropertyDemand(**project, (*project)->child);
}

} // namespace optimizer
} // namespace eugraph
