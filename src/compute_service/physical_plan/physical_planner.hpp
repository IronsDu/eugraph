#pragma once

#include "compute_service/binder/bound_logical_plan_fwd.hpp"
#include "compute_service/binder/bound_type.hpp"
#include "compute_service/catalog/catalog.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/function/function_registry.hpp"
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

/// Converts a LogicalPlan into a tree of PhysicalOperator.
class PhysicalPlanner {
public:
    /// Plan from a BoundLogicalPlan (Binder output).
    std::variant<std::unique_ptr<PhysicalOperator>, std::string>
    planBound(binder::BoundLogicalPlan& bound_plan, IAsyncGraphDataStore& store, PlanContext& ctx);

    /// Plan from a Legacy LogicalPlan (LogicalPlanBuilder output).
    std::variant<std::unique_ptr<PhysicalOperator>, std::string> plan(LogicalPlan& logical_plan,
                                                                      IAsyncGraphDataStore& store, PlanContext& ctx,
                                                                      const catalog::Catalog& catalog,
                                                                      const function::FunctionRegistry& func_registry);

private:
    // ── Bound plan dispatch ──
    std::variant<PlanOperatorResult, std::string> planBoundOperator(binder::BoundLogicalOperator& op,
                                                                    IAsyncGraphDataStore& store, PlanContext& ctx,
                                                                    Schema input_schema,
                                                                    const std::vector<binder::BoundType>& input_types);

    // ── Legacy plan dispatch ──
    std::variant<PlanOperatorResult, std::string> planOperator(LogicalOperator& op, IAsyncGraphDataStore& store,
                                                               PlanContext& ctx, Schema input_schema,
                                                               const std::vector<binder::BoundType>& input_types);

    std::variant<PlanOperatorResult, std::string> planAllNodeScan(AllNodeScanOp& op, IAsyncGraphDataStore& store,
                                                                  PlanContext& ctx, Schema input_schema,
                                                                  const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planLabelScan(LabelScanOp& op, IAsyncGraphDataStore& store,
                                                                PlanContext& ctx, Schema input_schema,
                                                                const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planExpand(ExpandOp& op, IAsyncGraphDataStore& store,
                                                             PlanContext& ctx, Schema input_schema,
                                                             const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planFilter(FilterOp& op, IAsyncGraphDataStore& store,
                                                             PlanContext& ctx, Schema input_schema,
                                                             const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planProject(ProjectOp& op, IAsyncGraphDataStore& store,
                                                              PlanContext& ctx, Schema input_schema,
                                                              const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planLimit(LimitOp& op, IAsyncGraphDataStore& store, PlanContext& ctx,
                                                            Schema input_schema,
                                                            const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planSkip(SkipOp& op, IAsyncGraphDataStore& store, PlanContext& ctx,
                                                           Schema input_schema,
                                                           const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planSort(SortOp& op, IAsyncGraphDataStore& store, PlanContext& ctx,
                                                           Schema input_schema,
                                                           const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planDistinct(DistinctOp& op, IAsyncGraphDataStore& store,
                                                               PlanContext& ctx, Schema input_schema,
                                                               const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planAggregate(AggregateOp& op, IAsyncGraphDataStore& store,
                                                                PlanContext& ctx, Schema input_schema,
                                                                const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planCreateNode(CreateNodeOp& op, IAsyncGraphDataStore& store,
                                                                 PlanContext& ctx, Schema input_schema,
                                                                 const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planCreateEdge(CreateEdgeOp& op, IAsyncGraphDataStore& store,
                                                                 PlanContext& ctx, Schema input_schema,
                                                                 const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planSet(SetOp& op, IAsyncGraphDataStore& store, PlanContext& ctx,
                                                          Schema input_schema,
                                                          const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planRemove(RemoveOp& op, IAsyncGraphDataStore& store,
                                                             PlanContext& ctx, Schema input_schema,
                                                             const std::vector<binder::BoundType>& input_types);
    std::variant<PlanOperatorResult, std::string> planPathBuild(PathBuildOp& op, IAsyncGraphDataStore& store,
                                                                PlanContext& ctx, Schema input_schema,
                                                                const std::vector<binder::BoundType>& input_types);

    /// Validate that PropertyAccess on LabelCastExpr (n::Label.prop) references
    /// a label and property that actually exist. Returns error string on failure.
    std::string validateExpression(const cypher::Expression& expr, const PlanContext& ctx) const;

    std::optional<PlanOperatorResult> tryIndexScan(LabelScanOp& scan_op,
                                                   const std::vector<const cypher::BinaryOp*>& conditions,
                                                   LabelId label_id, const LabelDef& label_def,
                                                   IAsyncGraphDataStore& store, PlanContext& ctx);

    std::optional<PlanOperatorResult> tryEdgeIndexScan(ExpandOp& expand_op,
                                                       const std::vector<const cypher::BinaryOp*>& conditions,
                                                       EdgeLabelId label_id, const EdgeLabelDef& edge_label_def,
                                                       IAsyncGraphDataStore& store, PlanContext& ctx);

    // Set by plan() before dispatching — used by planFilter/planProject for expression binding
    const catalog::Catalog* catalog_ = nullptr;
    const function::FunctionRegistry* func_registry_ = nullptr;
};

} // namespace compute
} // namespace eugraph
