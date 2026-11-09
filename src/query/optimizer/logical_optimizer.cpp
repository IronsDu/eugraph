#include "query/optimizer/logical_optimizer.hpp"
#include "common/config/env_vars.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/framework/cbo.hpp"
#include "query/optimizer/logical_model/requirement_collector.hpp"
#include "query/optimizer/rules/logical/edge_uniqueness_pushdown.hpp"
#include "query/optimizer/rules/logical/filter_pushdown.hpp"
#include "query/optimizer/rules/logical/path_count_fusion.hpp"
#include "query/optimizer/rules/logical/tag_anchored_join_reorder.hpp"
#include "query/optimizer/rules/physical/impl_rules.hpp"

#include "query/planner/bound_logical_plan.hpp"
#include <spdlog/spdlog.h>

#include <cstdlib>

namespace eugraph {
namespace optimizer {

namespace {

bool hasFullObjectEnricher(const ChosenPlan& plan) {
    switch (plan.tag) {
    case PhysicalOpTag::VertexEnrich:
    case PhysicalOpTag::EdgeEnrich:
    case PhysicalOpTag::PathEnrich:
        return true;
    default:
        break;
    }
    for (const auto& child : plan.children) {
        if (child && hasFullObjectEnricher(*child))
            return true;
    }
    return false;
}

} // namespace

void LogicalOptimizer::optimize(binder::BoundLogicalPlan& plan, const catalog::Catalog* catalog,
                                const std::unordered_map<LabelId, uint64_t>* label_vertex_counts) {
    // A LogicalOptimizer instance may be reused. Reset per-query state before
    // building a fresh search space; otherwise rules_ accumulates duplicate
    // rule indices and memo_ cross-query deduplicates unrelated plans.
    memo_ = Memo{};
    rules_ = RuleSet{};
    initRules();

    // Join-direction rewrite for the LDBC c6 tag-co-occurrence shape: the
    // selective knownTag anchor should drive the pattern. Guarded by label
    // cardinality estimates; other shapes are untouched.
    if (catalog)
        rewriteTagAnchoredJoin(plan, catalog, label_vertex_counts);

    // Make the catalog available to LogProp derivation. Group::getLogProp
    // picks it up via Memo::getCatalog() during OInputsTask cost computation.
    memo_.setCatalog(catalog);

    // Structural optimizer pass (no CBO statistics): choose the bounded
    // multi-target shortestPath execution strategy before memoization.
    markShortestPathBatch(plan.root);

    auto root_gid = memo_.copyIn(plan.root);

    // CBO: root physical property carries the query's required materialisation.
    // The binder has only recorded semantic references; this is the Cascades
    // required-physical-property side of property demand.
    VarRequirements root_reqs;
    if (catalog != nullptr)
        root_reqs = collectOpRequirements(plan.root, catalog);
    PhysProp root_prop;
    root_prop.setMaterializations(std::move(root_reqs));
    Context root_ctx(std::move(root_prop), Cost::infinity());
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
    if (const char* force_rbo = std::getenv(config::kEnvOptimizerForceRbo); force_rbo != nullptr && *force_rbo == '1') {
        plan.chosen.reset();
    }
    if (!plan.chosen)
        spdlog::debug("[optimizer] no CBO winner; using RBO logical plan");

    // Full-object Enricher lowering (VertexEnrich/EdgeEnrich/PathEnrich) is
    // not yet safe for all alias/bound-variable plans. Until that lowering is
    // completed, fall back to planBound for plans whose CBO winner contains a
    // full-object enforcer. Flat PropertyExtract enforcers remain enabled.
    if (plan.chosen && hasFullObjectEnricher(*plan.chosen)) {
        spdlog::debug("[optimizer] Chosen plan contains full-object Enricher; falling back to planBound");
        plan.chosen.reset();
    }

    // Always populate plan.root as RBO fallback. The RBO tree is not inside
    // the Enricher mechanism, so apply the binder's property demand to it
    // explicitly; the CBO chosen path gets this through PhysProp/PropertyExtract.
    plan.root = memo_.copyOut(root_gid, PhysProp{});
    // TODO experiment: no fallback property pushdown
}

void LogicalOptimizer::initRules() {
    // Transformation rules
    rules_.addRule(std::make_unique<FilterPushdownRule>());
    rules_.addRule(std::make_unique<EdgeUniquenessPushdownRule>());
    rules_.addRule(std::make_unique<PathCountFusionRule>());

    for (auto& rule : custom_rules_)
        rules_.addRule(std::move(rule));
    custom_rules_.clear();

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
    rules_.addRule(std::make_unique<ImplPathCountRule>());
    rules_.addRule(std::make_unique<ImplUnwindRule>());
    rules_.addRule(std::make_unique<ImplBinaryJoinRule>());
    rules_.addRule(std::make_unique<ImplLeftJoinRule>());
    rules_.addRule(std::make_unique<ImplSemiJoinRule>());
    rules_.addRule(std::make_unique<ImplUnionRule>());
}

} // namespace optimizer
} // namespace eugraph
