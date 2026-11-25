#pragma once

#include "common/types/graph_types.hpp"
#include "query/physical_plan/explain/explain_symbols.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/slot_semantics.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eugraph {

struct GraphSchema;

namespace compute {

class PhysicalOperator;

/// User-facing EXPLAIN context. It carries only schema/formatting context; it
/// must not be used for execution decisions.
struct ExplainContext {
    const GraphSchema* schema = nullptr;
    /// Show internal slots/column names that are normally hidden from users.
    bool verbose = false;
    /// CBO / RBO; copied to the root node by the builder.
    std::string plan_source;
    /// Binder-era slot semantics; used to render property columns without
    /// relying on physical-plan shape heuristics.
    const binder::SlotSemanticMap* slot_semantics = nullptr;
    /// Binder/user + PE-internal symbols, resolved by SlotId.
    const ExplainSymbolMap* symbols = nullptr;
};

struct ExplainNode {
    std::string operator_name;
    std::vector<std::string> details;
    std::vector<std::string> identifiers;
    std::optional<double> estimated_rows;
    std::optional<double> cost;
    std::string plan_source;
    std::vector<std::unique_ptr<ExplainNode>> children;
};

class ExplainBuilder {
public:
    static ExplainNode build(const PhysicalOperator& root, const ExplainContext& ctx);
};

/// Neo4j-like text tree: one operator per line, details in parentheses, children
/// rendered with tree connectors. `outputs` are only shown in verbose mode.
std::string formatExplainTree(const ExplainNode& root, const ExplainContext& ctx);

/// Render a bound expression for Details. Falls back to a stable expression
/// kind if a node has no user-facing form.
std::string formatExplainExpression(const binder::BoundExpression& expr, const ExplainContext& ctx);

/// Schema-aware name helpers; fall back to explicit IDs instead of hiding them.
/// Strip internal `__` conventions for user-facing EXPLAIN output. The
/// mapping is presentation-only and never changes operator/schema names.
std::string explainVariableName(std::string_view name);
bool explainIsInternalAlias(std::string_view name);
std::string explainFunctionName(std::string_view name);

std::string explainLabelName(LabelId id, const ExplainContext& ctx);
std::string explainEdgeLabelName(EdgeLabelId id, const ExplainContext& ctx);
std::string explainPropertyName(LabelId label_id, uint16_t prop_id, const ExplainContext& ctx);
std::string explainEdgePropertyName(EdgeLabelId label_id, uint16_t prop_id, const ExplainContext& ctx);

std::string explainLabelList(const std::vector<LabelId>& ids, const ExplainContext& ctx);
std::string explainEdgeLabelList(const std::vector<EdgeLabelId>& ids, const ExplainContext& ctx);
std::string explainPropertyList(LabelId label_id, const std::vector<uint16_t>& prop_ids, const ExplainContext& ctx);
std::string formatExplainPropertyValue(const PropertyValue& value);

} // namespace compute
} // namespace eugraph
