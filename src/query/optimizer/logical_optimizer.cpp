#include "query/optimizer/logical_optimizer.hpp"

#include "query/optimizer/cbo.hpp"
#include "query/optimizer/rules/filter_pushdown.hpp"
#include "query/optimizer/rules/impl/impl_rules.hpp"

#include "query/planner/bound_logical_plan.hpp"
#include <spdlog/spdlog.h>

namespace eugraph {
namespace optimizer {

void LogicalOptimizer::optimize(binder::BoundLogicalPlan& plan, const catalog::Catalog* catalog) {
    initRules();

    // Make the catalog available to LogProp derivation. Group::getLogProp
    // picks it up via Memo::getCatalog() during OInputsTask cost computation.
    memo_.setCatalog(catalog);

    auto root_gid = memo_.copyIn(plan.root);

    // CBO: create root context with "any" physical property and infinite cost bound.
    Context root_ctx(PhysProp{}, Cost::infinity());
    int root_ctx_id = memo_.addContext(std::move(root_ctx));

    TaskQueue queue;
    queue.push(std::make_unique<OGroupTask>(root_gid, root_ctx_id, /*last=*/true));

    constexpr int kMaxIterations = 100000;
    int iterations = 0;
    while (!queue.empty()) {
        auto task = queue.pop();
        task->perform(memo_, rules_, queue);
        if (++iterations >= kMaxIterations) {
            spdlog::warn("[optimizer] Reached max iterations ({}) — stopping early", kMaxIterations);
            break;
        }
    }

    // CBO Phase 4: extract chosen physical plan. Null if any group lacks a winner.
    plan.chosen = memo_.extractChosen(root_gid, PhysProp{});

    // Always populate plan.root as RBO fallback.
    plan.root = memo_.copyOut(root_gid, PhysProp{});
}

void LogicalOptimizer::initRules() {
    // Transformation rules
    rules_.addRule(std::make_unique<FilterPushdownRule>());

    // Implementation rules (Phase 4) — logical → physical
    rules_.addRule(std::make_unique<ImplSingletonRule>());
    rules_.addRule(std::make_unique<ImplCorrelatedSourceRule>());
    rules_.addRule(std::make_unique<ImplScanRule>());
    rules_.addRule(std::make_unique<ImplLabelScanRule>());
    rules_.addRule(std::make_unique<ImplFilterRule>());
    rules_.addRule(std::make_unique<ImplProjectRule>());
    rules_.addRule(std::make_unique<ImplExpandRule>());
    rules_.addRule(std::make_unique<ImplVarLenExpandRule>());
    rules_.addRule(std::make_unique<ImplSortRule>());
    rules_.addRule(std::make_unique<ImplLimitRule>());
    rules_.addRule(std::make_unique<ImplSkipRule>());
    rules_.addRule(std::make_unique<ImplDistinctRule>());
    rules_.addRule(std::make_unique<ImplAggregateRule>());
    rules_.addRule(std::make_unique<ImplPathBuildRule>());
    rules_.addRule(std::make_unique<ImplUnwindRule>());
    rules_.addRule(std::make_unique<ImplBinaryJoinRule>());
    rules_.addRule(std::make_unique<ImplLeftJoinRule>());
    rules_.addRule(std::make_unique<ImplSemiJoinRule>());
    rules_.addRule(std::make_unique<ImplUnionRule>());
}

} // namespace optimizer
} // namespace eugraph
