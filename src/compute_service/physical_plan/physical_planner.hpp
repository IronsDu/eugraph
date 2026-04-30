#pragma once

#include "compute_service/executor/row.hpp"
#include "compute_service/logical_plan/logical_plan.hpp"
#include "compute_service/physical_plan/physical_operator.hpp"
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
    const std::unordered_map<std::string, LabelId>* label_name_to_id = nullptr; // non-owning
    const std::unordered_map<std::string, EdgeLabelId>* edge_label_name_to_id = nullptr; // non-owning
    std::unordered_map<LabelId, LabelDef>* label_defs = nullptr; // non-owning, points to owner's map
    std::unordered_map<EdgeLabelId, EdgeLabelDef>* edge_label_defs = nullptr;
    std::unordered_map<std::string, VertexId> variable_vertex_ids; // for CREATE: assigned IDs
    std::unordered_map<std::string, EdgeId> variable_edge_ids;     // for CREATE: assigned IDs
    VertexId next_vertex_id = 1;
    EdgeId next_edge_id = 1;
};

/// Result of planning an operator: the physical operator + its output schema.
struct PlanOperatorResult {
    std::unique_ptr<PhysicalOperator> op;
    Schema output_schema;
};

/// Converts a LogicalPlan into a tree of PhysicalOperator.
class PhysicalPlanner {
public:
    /// Plan a logical plan into physical operators.
    /// Returns the physical operator tree root on success, or an error string.
    std::variant<std::unique_ptr<PhysicalOperator>, std::string> plan(LogicalPlan& logical_plan,
                                                                      IAsyncGraphDataStore& store, PlanContext& ctx);

private:
    std::variant<PlanOperatorResult, std::string> planOperator(LogicalOperator& op, IAsyncGraphDataStore& store,
                                                               PlanContext& ctx, Schema input_schema);

    std::variant<PlanOperatorResult, std::string> planAllNodeScan(AllNodeScanOp& op, IAsyncGraphDataStore& store,
                                                                  PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planLabelScan(LabelScanOp& op, IAsyncGraphDataStore& store,
                                                                PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planExpand(ExpandOp& op, IAsyncGraphDataStore& store,
                                                             PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planFilter(FilterOp& op, IAsyncGraphDataStore& store,
                                                             PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planProject(ProjectOp& op, IAsyncGraphDataStore& store,
                                                              PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planLimit(LimitOp& op, IAsyncGraphDataStore& store, PlanContext& ctx,
                                                            Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planSkip(SkipOp& op, IAsyncGraphDataStore& store, PlanContext& ctx,
                                                           Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planSort(SortOp& op, IAsyncGraphDataStore& store, PlanContext& ctx,
                                                           Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planDistinct(DistinctOp& op, IAsyncGraphDataStore& store,
                                                               PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planAggregate(AggregateOp& op, IAsyncGraphDataStore& store,
                                                                PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planCreateNode(CreateNodeOp& op, IAsyncGraphDataStore& store,
                                                                 PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planCreateEdge(CreateEdgeOp& op, IAsyncGraphDataStore& store,
                                                                 PlanContext& ctx, Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planSet(SetOp& op, IAsyncGraphDataStore& store, PlanContext& ctx,
                                                          Schema input_schema);
    std::variant<PlanOperatorResult, std::string> planRemove(RemoveOp& op, IAsyncGraphDataStore& store,
                                                             PlanContext& ctx, Schema input_schema);

    std::optional<PlanOperatorResult> tryIndexScan(LabelScanOp& scan_op, cypher::BinaryOp& binop, LabelId label_id,
                                                   const LabelDef& label_def, IAsyncGraphDataStore& store,
                                                   PlanContext& ctx);
};

} // namespace compute
} // namespace eugraph
