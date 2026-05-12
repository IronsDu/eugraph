#pragma once

#include "query/executor/row.hpp"
#include "query/physical_plan/physical_operator.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace eugraph {
namespace compute {

/// Context passed through physical planning: maps label/edge-label names to IDs,
/// tracks variable schemas, and provides storage access.
struct PlanContext {
    const std::unordered_map<std::string, LabelId>& label_name_to_id;
    const std::unordered_map<std::string, EdgeLabelId>& edge_label_name_to_id;
    std::unordered_map<LabelId, LabelDef>& label_defs;
    std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs;
    std::unordered_map<std::string, VertexId> variable_vertex_ids; // for CREATE: assigned IDs
    std::unordered_map<std::string, EdgeId> variable_edge_ids;     // for CREATE: assigned IDs
    VertexId next_vertex_id = 1;
    EdgeId next_edge_id = 1;
};

/// Result of planning an operator: the physical operator + its output schema + types.
struct PlanOperatorResult {
    std::unique_ptr<PhysicalOperator> op;
    Schema output_schema;
    std::vector<binder::BoundType> output_types;
};

/// Converts a BoundLogicalPlan into a tree of PhysicalOperator.
class PhysicalPlanner {
public:
    /// Plan from a BoundLogicalPlan (Binder output).
    std::variant<std::unique_ptr<PhysicalOperator>, std::string>
    planBound(binder::BoundLogicalPlan& bound_plan, IAsyncGraphDataStore& store, PlanContext& ctx);

private:
    std::variant<PlanOperatorResult, std::string> planBoundOperator(binder::BoundLogicalOperator& op,
                                                                    IAsyncGraphDataStore& store, PlanContext& ctx,
                                                                    Schema input_schema,
                                                                    const std::vector<binder::BoundType>& input_types);

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
