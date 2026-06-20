#pragma once

#include "query/optimizer/chosen_plan.hpp"
#include "query/optimizer/cost_model.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace optimizer {

/// Rewrite pass that updates BoundPropertyRef nodes in a BoundLogicalOperator
/// tree to BoundColumnRef nodes referencing flat columns emitted by
/// PropertyExtract operators. This is needed because standalone
/// PropertyExtractPhysicalOp APPENDS new columns (the binder assigned
/// indices before optimization; without this pass, downstream operators
/// reference stale indices).
///
/// Usage: call after materializeChosen, before planBoundOperator.
///   auto extraction_info = collectExtractionInfo(chosen);
///   auto materialized = materializeChosen(chosen);
///   rewriteColumnIndices(materialized, extraction_info);

struct PropertyExtractionInfo {
    /// Base column index of the source variable (before extraction).
    uint32_t base_col = 0;
    /// Per-property mapping: (label_id, prop_id) → new column index offset.
    /// Offset is 1 (skip source column) + cumulative props before this one.
    /// The absolute column index is base_col + offset.
    std::vector<std::pair<LabelId, uint16_t>> prop_order;
};

/// Collect property extraction info from a ChosenPlan tree.
/// Key: variable name. Value: base column index + ordered property list.
/// The column index is set by the caller (requires schema knowledge).
std::unordered_map<std::string, PropertyExtractionInfo> collectExtractionInfo(const ChosenPlan& chosen,
                                                                              uint32_t base_col);

/// Collect extraction info from a BoundLogicalOperator tree (RBO path).
/// Walks scan/expand operators and records per-variable property lists.
std::unordered_map<std::string, PropertyExtractionInfo>
collectExtractionFromPlan(const binder::BoundLogicalOperator& root, uint32_t base_col);

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
void updateExtractionBaseCols(const binder::BoundLogicalOperator& op,
                              std::unordered_map<std::string, PropertyExtractionInfo>& info);

/// Walk the plan and find variables that need full objects (RETURN n, RETURN r).
/// These variables must NOT use standalone PropertyExtract — they need VertexValue.
std::set<std::string> findWholeObjectVariables(const binder::BoundLogicalOperator& op);

} // namespace optimizer
} // namespace eugraph
