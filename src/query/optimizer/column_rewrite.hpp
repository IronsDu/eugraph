#pragma once

#include "query/optimizer/chosen_plan.hpp"
#include "query/optimizer/cost_model.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace optimizer {

/// Rewrite pass that updates BoundPropertyRef nodes in a BoundLogicalOperator
/// tree to BoundColumnRef nodes referencing flat columns emitted by
/// ProjectionExtractPhysicalOp. This is needed because the extract operator
/// APPENDS new columns (the binder assigned indices before optimization;
/// without this pass, downstream operators reference stale indices).
///
/// Usage: call after collectPlanRequirements, before planBoundOperator.
///   auto reqs = collectPlanRequirements(root);
///   auto info = buildExtractionInfo(root, reqs);
///   rewriteColumnIndices(root, info);

struct PropertyExtractionInfo {
    /// Base column index of the source variable (before extraction).
    uint32_t base_col = 0;
    /// Per-property mapping: (label_id, prop_id) → new column index offset.
    /// Offset is 1 (skip source column) + cumulative props before this one.
    /// The absolute column index is base_col + offset.
    std::vector<std::pair<LabelId, uint16_t>> prop_order;
    /// Per-property mapping for edge properties: (edge_label_id, prop_id).
    /// Edge props are output right after vertex props (before labels/type columns).
    /// Absolute column index = base_col + 1 + prop_order.size() + edge_index.
    std::vector<std::pair<EdgeLabelId, uint16_t>> edge_prop_order;
};

/// Per-variable requirement collected by scanning downstream operators.
/// Determines which ColumnSpec list ProjectionExtractPhysicalOp must emit.
struct VariableRequirement {
    /// RETURN n / SET n.x / REMOVE n.x / BoundDynamicPropertyRef on vertex:
    /// extract must construct a full VertexValue column (covering the original
    /// VertexRef column in-place).
    bool need_whole_vertex = false;
    /// RETURN r / BoundDynamicPropertyRef on edge: extract must construct a
    /// full EdgeValue column in-place.
    bool need_whole_edge = false;
    /// labels(n) / n::Label: extract emits a List<String> column.
    bool need_vertex_labels = false;
    /// type(r): extract emits a String column with the edge type name.
    bool need_edge_type = false;
    /// (label_id, prop_id) list for vertex property access. De-duplicated.
    std::vector<std::pair<LabelId, uint16_t>> vertex_props;
    /// (edge_label_id, prop_id) list for edge property access. De-duplicated.
    std::vector<std::pair<EdgeLabelId, uint16_t>> edge_props;

    bool empty() const {
        return !need_whole_vertex && !need_whole_edge && !need_vertex_labels && !need_edge_type &&
               vertex_props.empty() && edge_props.empty();
    }
};

using PlanRequirements = std::unordered_map<std::string, VariableRequirement>;

/// Collect property extraction info from a ChosenPlan tree.
/// Key: variable name. Value: base column index + ordered property list.
/// The column index is set by the caller (requires schema knowledge).
std::unordered_map<std::string, PropertyExtractionInfo> collectExtractionInfo(const ChosenPlan& chosen,
                                                                              uint32_t base_col);

/// Collect extraction info from a BoundLogicalOperator tree (RBO path).
/// Walks scan/expand operators and records per-variable property lists.
std::unordered_map<std::string, PropertyExtractionInfo>
collectExtractionFromPlan(const binder::BoundLogicalOperator& root, uint32_t base_col);

/// Walk the entire plan and collect per-variable requirements from all
/// downstream consumers (Project / Filter / Sort / Aggregate / Set / Remove /
/// Delete / Merge). This is the top-level requirement collector used by
/// planBound before any extraction decisions are made.
PlanRequirements collectPlanRequirements(const binder::BoundLogicalOperator& root);

/// Convert PlanRequirements into PropertyExtractionInfo suitable for the
/// existing rewriteColumnIndices pass. Only vertex_props are encoded for now;
/// need_whole_vertex / need_vertex_labels / need_edge_type do not require
/// BoundPropertyRef rewriting (they are handled by extract's construct/labels
/// columns whose indices overlap with the source variable column).
std::unordered_map<std::string, PropertyExtractionInfo> buildExtractionInfo(const binder::BoundLogicalOperator& root,
                                                                            const PlanRequirements& reqs);

/// Walk a BoundExpression and rewrite BoundPropertyRef → BoundColumnRef
/// using the given extraction info. Returns true if any rewrite occurred.
bool rewriteExpression(binder::BoundExpression& expr,
                       const std::unordered_map<std::string, PropertyExtractionInfo>& info);

/// Walk a BoundLogicalOperator tree and rewrite all BoundExpressions
/// in Filter/Project/Sort/Aggregate operators.
void rewriteColumnIndices(binder::BoundLogicalOperator& op,
                          const std::unordered_map<std::string, PropertyExtractionInfo>& info);

/// After materializing the logical tree, update base_col in extraction_info
/// to match the actual column indices. This fixes the base_col for non-root
/// variables (e.g. edge variable at column 1 in Expand output).
/// reqs is the full requirement map (including need_whole_*/labels/type) so
/// the column counter can account for ALL columns ProjectionExtract appends.
void updateExtractionBaseCols(const binder::BoundLogicalOperator& op,
                              std::unordered_map<std::string, PropertyExtractionInfo>& info,
                              const PlanRequirements& reqs);

/// Walk the plan and find variables that need full objects (RETURN n, RETURN r).
/// These variables must NOT use standalone PropertyExtract — they need VertexValue.
std::set<std::string> findWholeObjectVariables(const binder::BoundLogicalOperator& op);

} // namespace optimizer
} // namespace eugraph
