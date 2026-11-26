#include "query/optimizer/logical_optimizer.hpp"
#include "common/config/env_vars.hpp"

#include "query/optimizer/analysis/index_condition.hpp"
#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/analysis/requirement_collector.hpp"
#include "query/optimizer/framework/cbo.hpp"
#include "query/optimizer/normalization/plan_normalizations.hpp"
#include "query/optimizer/rules/logical/edge_uniqueness_pushdown.hpp"
#include "query/optimizer/rules/logical/filter_pushdown.hpp"
#include "query/optimizer/rules/logical/join_associate.hpp"
#include "query/optimizer/rules/logical/join_commute.hpp"
#include "query/optimizer/rules/logical/path_count_fusion.hpp"
#include "query/optimizer/rules/physical/impl_rules.hpp"

#include "query/planner/bound_logical_plan.hpp"
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <functional>
#include <unordered_set>

namespace eugraph {
namespace optimizer {

namespace {

} // namespace

namespace {

struct IndexScanChoice {
    uint32_t index_id = 0;
    std::vector<uint16_t> prop_ids;
    std::vector<PropertyValue> eq;
    std::optional<PropertyValue> lower;
    std::optional<PropertyValue> upper;
    bool lower_inclusive = false;
    bool upper_inclusive = false;
};

std::optional<IndexScanChoice> chooseIndexScan(const LabelDef& label, const std::vector<IndexableCondition>& conds,
                                               const std::unordered_set<std::string>& anon_props) {
    for (const auto& idx : label.indexes) {
        if (idx.index_id == 0 || idx.state != IndexState::PUBLIC || idx.encoding_version != 2 || idx.accessors.empty())
            continue;
        IndexScanChoice out;
        out.index_id = idx.index_id;
        out.prop_ids = idx.prop_ids;
        size_t used = 0;
        bool ok = true;
        for (size_t i = 0; i < idx.accessors.size(); ++i) {
            const auto& acc = idx.accessors[i];
            const IndexableCondition* cond = nullptr;
            for (size_t c = used; c < conds.size(); ++c)
                if (conds[c].property_name == acc.property_name) {
                    cond = &conds[c];
                    break;
                }
            if (!cond) {
                ok = false;
                break;
            }
            if (acc.is_strong) {
                if (cond->is_strong ? cond->label_id != acc.source_label_id
                                    : (cond->declared_label_count != 1 || cond->label_id != acc.source_label_id ||
                                       anon_props.contains(acc.property_name))) {
                    ok = false;
                    break;
                }
            } else if (cond->is_strong) {
                ok = false;
                break;
            }
            if (i + 1 < idx.accessors.size()) {
                if (cond->op != cypher::BinaryOperator::EQ) {
                    ok = false;
                    break;
                }
                out.eq.push_back(cond->value);
                ++used;
            } else {
                IndexRange range;
                bool has_eq = false;
                size_t consumed = used;
                for (size_t c = used; c < conds.size(); ++c) {
                    if (conds[c].property_name != acc.property_name)
                        continue;
                    const auto& cc = conds[c];
                    ++consumed;
                    if (cc.op == cypher::BinaryOperator::EQ) {
                        out.eq.push_back(cc.value);
                        has_eq = true;
                    } else if (cc.op == cypher::BinaryOperator::GT || cc.op == cypher::BinaryOperator::GTE) {
                        mergeIndexLower(range, cc.value, cc.op == cypher::BinaryOperator::GTE);
                    } else if (cc.op == cypher::BinaryOperator::LT || cc.op == cypher::BinaryOperator::LTE) {
                        mergeIndexUpper(range, cc.value, cc.op == cypher::BinaryOperator::LTE);
                    } else {
                        ok = false;
                        break;
                    }
                }
                if (!ok || consumed != conds.size())
                    ok = false;
                if (!has_eq) {
                    if (range.lower) {
                        out.lower = range.lower->value;
                        out.lower_inclusive = range.lower->inclusive;
                    }
                    if (range.upper) {
                        out.upper = range.upper->value;
                        out.upper_inclusive = range.upper->inclusive;
                    }
                }
            }
        }
        if (ok)
            return out;
    }
    return std::nullopt;
}

void applyIndexAccessPaths(binder::BoundLogicalOperator& op, const catalog::Catalog& catalog,
                           const binder::SlotSemanticMap* semantics) {
    auto* filter_ptr = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&op);
    if (filter_ptr && *filter_ptr) {
        auto* scan = std::get_if<binder::BoundLabelScanOp>(&(*filter_ptr)->child);
        if (scan && scan->label_ids.size() == 1 && !scan->index_scan_values && semantics) {
            auto label_it = catalog.allLabels().find(scan->label_ids[0]);
            if (label_it != catalog.allLabels().end()) {
                std::unordered_set<std::string> anon_props;
                for (const auto& [lid, ld] : catalog.allLabels())
                    if (ld.name == "__anon__") {
                        for (const auto& pd : ld.properties)
                            anon_props.insert(pd.name);
                        break;
                    }
                auto resolver = [&](const binder::BoundColumnRef& ref) -> std::optional<ResolvedProperty> {
                    auto it = semantics->find(ref.slot_id);
                    if (it == semantics->end() || it->second.kind != binder::SlotSemanticKind::Property)
                        return std::nullopt;
                    ResolvedProperty out;
                    out.name = it->second.property_name;
                    out.label_id =
                        it->second.property_labels.empty() ? INVALID_LABEL_ID : it->second.property_labels.front();
                    out.prop_id = it->second.property_ids.empty() ? 0 : it->second.property_ids.front();
                    out.is_strong = it->second.strong;
                    out.declared_label_count = static_cast<int>(it->second.property_labels.size());
                    return out;
                };
                std::vector<const binder::BoundExpression*> conjs;
                std::function<void(const binder::BoundExpression&)> collect = [&](const binder::BoundExpression& e) {
                    auto* b = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&e);
                    if (b && *b && (*b)->op == cypher::BinaryOperator::AND) {
                        collect((*b)->left);
                        collect((*b)->right);
                    } else {
                        conjs.push_back(&e);
                    }
                };
                collect((*filter_ptr)->predicate);
                std::vector<IndexableCondition> conds;
                bool all = true;
                for (const auto* e : conjs) {
                    auto* b = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(e);
                    if (!b || !*b) {
                        all = false;
                        break;
                    }
                    auto c = extractNormalizedIndexableCondition(**b, INVALID_LABEL_ID, resolver);
                    if (!c) {
                        all = false;
                        break;
                    }
                    conds.push_back(std::move(*c));
                }
                if (all) {
                    auto choice = chooseIndexScan(label_it->second, conds, anon_props);
                    if (choice) {
                        scan->index_scan = true;
                        scan->index_id = choice->index_id;
                        scan->index_prop_ids = std::move(choice->prop_ids);
                        scan->index_eq_values = std::move(choice->eq);
                        scan->index_lower = std::move(choice->lower);
                        scan->index_upper = std::move(choice->upper);
                        scan->index_lower_inclusive = choice->lower_inclusive;
                        scan->index_upper_inclusive = choice->upper_inclusive;
                        auto child = std::move((*filter_ptr)->child);
                        op = std::move(child);
                        return;
                    }
                }
            }
        }
    }
    optimizer::forEachLogicalChild(op, [&](binder::BoundLogicalOperator& child) {
        applyIndexAccessPaths(child, catalog, semantics);
    });
}

} // namespace

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
    normalization.add(std::make_unique<JoinConditionNormalization>());
    normalization.add(std::make_unique<ShortestPathBatchNormalization>());
    NormalizationContext normalization_ctx;
    normalization.run(plan.root, normalization_ctx);
    memo_.setCommuteSafe(normalization_ctx.commute_safe);

    // Make the catalog available to LogProp derivation. Group::getLogProp
    // picks it up via Memo::getCatalog() during OInputsTask cost computation.
    if (catalog != nullptr)
        applyIndexAccessPaths(plan.root, *catalog, slot_semantics);

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
    rules_.addRule(std::make_unique<GenerateIndexJoinRule>());
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
    rules_.addRule(std::make_unique<ImplPatternComprehensionApplyRule>());
    rules_.addRule(std::make_unique<ImplIndexJoinRule>());
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
