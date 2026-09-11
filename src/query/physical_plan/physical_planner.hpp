#pragma once

#include "query/dataset/row.hpp"
#include "query/optimizer/column_rewrite.hpp"
#include "query/physical_plan/physical_operator.hpp"
#include "query/physical_plan/slot_layout.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace eugraph {
namespace optimizer {
struct ChosenPlan;
struct PEPlan;
} // namespace optimizer

namespace function {
class FunctionRegistry;
} // namespace function

namespace compute {

class ExpandPhysicalOp;

/// Context passed through physical planning: maps label/edge-label names to IDs,
/// tracks variable schemas, and provides storage access.
struct ExpandAllowedFilterContext {
    uint32_t index_id = 0;
    EdgeLabelId edge_label = INVALID_EDGE_LABEL_ID;
    std::string dst_var;
};

struct PlanContext {
    const std::unordered_map<std::string, LabelId>& label_name_to_id;
    std::unordered_map<std::string, EdgeLabelId>& edge_label_name_to_id;
    std::unordered_map<LabelId, LabelDef>& label_defs;
    std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs;
    function::EvalContext eval_ctx;
    /// Per-slot requirements collected from downstream operators.
    /// Drives ProjectionExtractPhysicalOp spec generation.
    optimizer::PlanRequirements requirements;
    /// Per-canonical-slot extraction plan produced by buildExtractionInfo
    /// (the Decide phase). dispatchProjectionExtract consumes this directly
    /// so all PE-related decisions — slot allocation, Construct emit, prop
    /// loads — live in one place.
    optimizer::PEPlans extraction_info;
    /// Maps variable name → globally-unique SlotId. Populated by
    /// optimizer::allocateAllSlots at the start of planBound (covers all
    /// variable names in the bound tree, including aliases).
    optimizer::NameSlotMap var_slots;
    /// Scope-aware binding records seeded from Binder::scoped_bindings.
    optimizer::ScopedSlotMap scoped_var_slots;
    /// Query-time label presentation order per variable name.
    std::unordered_map<std::string, std::vector<LabelId>> label_order_by_name;
    /// Alias slot → canonical slot map. Built by collectAliasSlotMap.
    optimizer::AliasSlotMap alias_map;
    /// Slot allocator for ProjectionExtract-appended columns.
    binder::SlotAllocator slot_allocator;
    /// Fresh Expand bindings (name → planner slot) collected by
    /// buildFreshExpandMap. dispatchProjectionExtract uses this to also
    /// emit Construct columns for names that canonicalForName would
    /// conflate with an alias chain (§6.2).
    optimizer::NameSlotMap fresh_expands;
    /// Candidate label / edge-label scan restriction derived from read-only
    /// IS NOT NULL property filters. Only labels that actually define the
    /// property need to be scanned; rows for all other labels evaluate to
    /// false under the bind-time schema.
    struct StaticPruneHint {
        std::vector<LabelId> vertex_labels;
        std::vector<EdgeLabelId> edge_labels;
    };
    std::unordered_map<std::string, StaticPruneHint> static_prune_hints = {};
    /// Built-in function catalog, used by dbms.functions() procedure rows.
    const function::FunctionRegistry* func_registry = nullptr;

    /// When set, the next Expand for dst_var is configured as an
    /// allowed-destination index filter (used by Filter(CrossProduct)
    /// `x.prop IN left.list` planning).
    std::optional<ExpandAllowedFilterContext> expand_allowed_filter;
    ExpandPhysicalOp* filtered_expand = nullptr;

    /// Read-only resolver over (var_slots, alias_map). All read-only query
    /// paths should go through this — direct map access is reserved for the
    /// lookup-or-allocate pattern in makeSlotLayout / Project per-item fallback
    /// and for the allocateAllSlots / lowerAliasPassthrough writers.
    optimizer::SlotResolver resolver() const {
        return optimizer::SlotResolver(var_slots, alias_map, &scoped_var_slots);
    }
};

/// Result of planning an operator: the physical operator + its output schema + types
/// + logical slot layout (maps SlotId → physical column_index).
struct PlanOperatorResult {
    std::unique_ptr<PhysicalOperator> op;
    Schema output_schema;
    std::vector<binder::BoundType> output_types;
    TupleSlotLayout slot_layout;
};

/// Converts a BoundLogicalPlan into a tree of PhysicalOperator.
class PhysicalPlanner {
public:
    /// Plan from a BoundLogicalPlan (Binder output).
    std::variant<std::unique_ptr<PhysicalOperator>, std::string> planBound(binder::BoundLogicalPlan& bound_plan,
                                                                           IAsyncGraphDataStore& store,
                                                                           IAsyncGraphMetaStore& meta,
                                                                           PlanContext& ctx);

    /// Plan from a CBO-chosen physical plan (Phase 4). Materializes the
    /// ChosenPlan tree into a fresh BoundLogicalOperator tree and delegates
    /// to planBoundOperator. Honors the optimizer's tag selections where
    /// they matter; for most operators the source logical operator's variant
    /// type alone determines the physical op (1:1 mapping).
    ///
    /// Falls back to planBound when `chosen` is null.
    std::variant<std::unique_ptr<PhysicalOperator>, std::string> planChosen(const optimizer::ChosenPlan& chosen,
                                                                            IAsyncGraphDataStore& store,
                                                                            IAsyncGraphMetaStore& meta,
                                                                            PlanContext& ctx);

private:
    std::variant<PlanOperatorResult, std::string>
    planBoundOperator(binder::BoundLogicalOperator& op, IAsyncGraphDataStore& store, IAsyncGraphMetaStore& meta,
                      PlanContext& ctx, Schema input_schema, const std::vector<binder::BoundType>& input_types);

    // ── Bound-plan index scan optimization ──
    std::optional<PlanOperatorResult> tryBoundIndexScan(const binder::BoundLabelScanOp& scan_op,
                                                        const std::vector<const binder::BoundBinaryOp*>& conditions,
                                                        LabelId label_id, const LabelDef& label_def,
                                                        IAsyncGraphDataStore& store, PlanContext& ctx);

    std::optional<PlanOperatorResult> tryBoundEdgeIndexScan(const binder::BoundExpandOp& expand_op,
                                                            const std::vector<const binder::BoundBinaryOp*>& conditions,
                                                            EdgeLabelId label_id, const EdgeLabelDef& edge_label_def,
                                                            IAsyncGraphDataStore& store, PlanContext& ctx);

    /// Plan Filter(CrossProduct(left,right)) with `right.x.prop IN left.list`
    /// as a ListIndexJoin: the left list is injected into right's final
    /// Expand so it probes the destination index instead of scanning edges.
    std::optional<PlanOperatorResult> tryPlanListIndexJoin(const binder::BoundFilterOp& filter,
                                                           binder::BoundBinaryJoinOp& join, IAsyncGraphDataStore& store,
                                                           IAsyncGraphMetaStore& meta, PlanContext& ctx,
                                                           Schema input_schema,
                                                           const std::vector<binder::BoundType>& input_types);

    /// Reorder a correlated Expand chain driven by a selective range
    /// predicate on the expanded destination (e.g. LDBC Q3's
    /// `(friend)<-[:HAS_CREATOR]-(message) WHERE message.creationDate ...`).
    /// Instead of scanning every message of every candidate friend, this
    /// plans the destination label's property index first and hash-joins the
    /// resulting messages back to the candidate source rows through the edge.
    std::optional<PlanOperatorResult> tryPlanFilterDestinationIndexJoin(binder::BoundFilterOp& filter,
                                                                        IAsyncGraphDataStore& store,
                                                                        IAsyncGraphMetaStore& meta, PlanContext& ctx);
};

} // namespace compute
} // namespace eugraph
