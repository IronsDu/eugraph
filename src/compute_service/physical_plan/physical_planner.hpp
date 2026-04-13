#pragma once

#include "compute_service/executor/async_graph_store.hpp"
#include "compute_service/executor/io_scheduler.hpp"
#include "compute_service/logical_plan/logical_plan.hpp"
#include "compute_service/physical_plan/physical_operator.hpp"
#include "storage/graph_store.hpp"

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
    std::unordered_map<std::string, LabelId> label_name_to_id;
    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
    std::unordered_map<std::string, VertexId> variable_vertex_ids; // for CREATE: assigned IDs
    std::unordered_map<std::string, EdgeId> variable_edge_ids;     // for CREATE: assigned IDs
    VertexId next_vertex_id = 1;
    EdgeId next_edge_id = 1;
};

/// Converts a LogicalPlan into a tree of PhysicalOperator.
class PhysicalPlanner {
public:
    /// Plan a logical plan into physical operators.
    /// Returns the physical operator tree root on success, or an error string.
    std::variant<std::unique_ptr<PhysicalOperator>, std::string> plan(LogicalPlan& logical_plan,
                                                                       AsyncGraphStore& store,
                                                                       PlanContext& ctx);

private:
    std::variant<std::unique_ptr<PhysicalOperator>, std::string> planOperator(
        LogicalOperator& op, AsyncGraphStore& store, PlanContext& ctx,
        Schema input_schema);
};

} // namespace compute
} // namespace eugraph
