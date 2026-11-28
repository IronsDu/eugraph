#include "query/optimizer/logical_optimizer.hpp"
#include "common/config/env_vars.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/analysis/requirement_collector.hpp"
#include "query/optimizer/framework/cbo.hpp"
#include "query/optimizer/normalization/plan_normalizations.hpp"
#include "query/optimizer/rules/logical/edge_uniqueness_pushdown.hpp"
#include "query/optimizer/rules/logical/filter_pushdown.hpp"
#include "query/optimizer/rules/logical/generate_index_join.hpp"
#include "query/optimizer/rules/logical/generate_index_probe.hpp"
#include "query/optimizer/rules/logical/generate_index_scan.hpp"
#include "query/optimizer/rules/logical/join_associate.hpp"
#include "query/optimizer/rules/logical/join_commute.hpp"
#include "query/optimizer/rules/logical/path_count_fusion.hpp"
#include "query/optimizer/rules/logical/prune_expand_edge_labels.hpp"
#include "query/optimizer/rules/logical/prune_varlen_or_filters.hpp"
#include "query/optimizer/rules/logical/resolve_index_probe.hpp"
#include "query/optimizer/rules/physical/impl_rules.hpp"

#include "query/planner/bound_logical_plan.hpp"
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <functional>
#include <unordered_set>

namespace eugraph {
namespace optimizer {

namespace {} // namespace

namespace {} // namespace

void LogicalOptimizer::optimize(binder::BoundLogicalPlan& plan, const catalog::Catalog* catalog,
                                const binder::SlotSemanticMap* slot_semantics,
                                const std::unordered_map<LabelId, uint64_t>* label_vertex_counts) {
    // A LogicalOptimizer instance may be reused. Reset per-query state before
    // building a fresh search space; otherwise rules_ accumulates duplicate
    // rule indices and memo_ cross-query deduplicates unrelated plans.
    memo_ = Memo{};
    rules_ = RuleSet{};
    initRules();

    // Normalization：建立逻辑计划不变量/标注，不产生 Cascades alternative。
    // 顺序固定、与 RuleSet 分离；结果只通过 NormalizationContext 传回 optimizer。
    NormalizationPipeline normalization;
    normalization.add(std::make_unique<JoinSafetyNormalization>());
    // The binder already emits the correlated endpoint as a bound Expand
    // destination (P2); this pass only marks the batch-probe strategy on it.
    // Physical lowering reads the flag and never decides it.
    normalization.add(std::make_unique<AnnotateCorrelatedEndpointProbeNormalization>());
    normalization.add(std::make_unique<JoinConditionNormalization>());
    NormalizationContext normalization_ctx;
    normalization.run(plan.root, normalization_ctx);
    memo_.setCommuteSafe(normalization_ctx.commute_safe);

    // Make the catalog available to LogProp derivation. Group::getLogProp
    // picks it up via Memo::getCatalog() during OInputsTask cost computation.
    memo_.setCatalog(catalog);
    memo_.setSlotSemantics(slot_semantics);

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

    // Publish winners that the cascade registered without closing their group
    // (root group expanded by a rule during the optimization pass).
    memo_.finalizeWinners();

    // CBO Phase 4: extract chosen physical plan. Null if any group lacks a winner.
    plan.chosen = memo_.extractChosen(root_gid, PhysProp{});
    if (const char* force_rbo = std::getenv(config::kEnvOptimizerForceRbo); force_rbo != nullptr && *force_rbo == '1') {
        plan.chosen.reset();
    }
    if (!plan.chosen)
        spdlog::debug("[optimizer] no CBO winner; using RBO logical plan");

    // Always populate plan.root as RBO fallback. The RBO tree is not inside
    // the Enricher mechanism, so apply the binder's property demand to it
    // explicitly; the CBO chosen path gets this through PhysProp/PropertyExtract.
    plan.root = memo_.copyOut(root_gid, PhysProp{});
    // TODO experiment: no fallback property pushdown
}

void LogicalOptimizer::initRules() {
    // Transformation rules
    rules_.addRule(std::make_unique<GenerateIndexScanRule>());
    rules_.addRule(std::make_unique<GenerateIndexJoinRule>());
    // Access-path candidate group switch. Kept as an explicit fallback: setting
    // EUGRAPH_DISABLE_PROBE_RULES=1 restores the pre-ProbeJoin search space
    // (useful for plan A/B and for regressions without a rebuild).
    if (std::getenv(config::kEnvDisableProbeRules) == nullptr) {
        // R1: Filter(Expand) shape -> IndexProbe + ProbeJoin candidate.
        rules_.addRule(std::make_unique<GenerateIndexProbeRule>());
        // R2: refine the probe group's descriptor to a catalog index (shape-only
        // rules never choose indexes; the probe group is optimized independently).
        rules_.addRule(std::make_unique<ResolveIndexProbeRule>());
        // Edge-only semantic pruning: edges are strong-only, so `r.p IS NOT NULL`
        // may narrow the Expand's edge labels (vertex pruning would be unsound:
        // weak-space values exist outside the declaring label).
        rules_.addRule(std::make_unique<PruneExpandEdgeLabelsRule>());
        // VLE OR pruning: src/dst index allow-sets on the VLE descriptor.
        rules_.addRule(std::make_unique<PruneVarlenOrFiltersRule>());
    }
    rules_.addRule(std::make_unique<FilterPushdownRule>());
    rules_.addRule(std::make_unique<JoinCommuteRule>());
    rules_.addRule(std::make_unique<JoinAssociateRule>());
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
    rules_.addRule(std::make_unique<ImplIndexScanRule>());
    rules_.addRule(std::make_unique<ImplPatternComprehensionApplyRule>());
    rules_.addRule(std::make_unique<ImplIndexJoinRule>());
    if (std::getenv(config::kEnvDisableProbeRules) == nullptr) {
        rules_.addRule(std::make_unique<ImplIndexProbeRule>());
        rules_.addRule(std::make_unique<ImplIndexProbeJoinRule>());
    }
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
    rules_.addRule(std::make_unique<ImplHashJoinRule>());
    rules_.addRule(std::make_unique<ImplBinaryJoinRule>());
    rules_.addRule(std::make_unique<ImplLeftJoinRule>());
    rules_.addRule(std::make_unique<ImplSemiJoinRule>());
    rules_.addRule(std::make_unique<ImplUnionRule>());
}

} // namespace optimizer
} // namespace eugraph
