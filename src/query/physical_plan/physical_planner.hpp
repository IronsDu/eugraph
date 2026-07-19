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

namespace compute {

/// Context passed through physical planning: maps label/edge-label names to IDs,
/// tracks variable schemas, and provides storage access.
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
    /// Alias slot → canonical slot map. Built by collectAliasSlotMap.
    optimizer::AliasSlotMap alias_map;
    /// Slot allocator for ProjectionExtract-appended columns.
    binder::SlotAllocator slot_allocator;
    /// Fresh Expand bindings (name → planner slot) collected by
    /// buildFreshExpandMap. dispatchProjectionExtract uses this to also
    /// emit Construct columns for names that canonicalForName would
    /// conflate with an alias chain (§6.2).
    optimizer::NameSlotMap fresh_expands;

    /// Read-only resolver over (var_slots, alias_map). All read-only query
    /// paths should go through this — direct map access is reserved for the
    /// lookup-or-allocate pattern in makeSlotLayout / Project per-item fallback
    /// and for the allocateAllSlots / lowerAliasPassthrough writers.
    optimizer::SlotResolver resolver() const {
        return optimizer::SlotResolver(var_slots, alias_map);
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
};

} // namespace compute
} // namespace eugraph
