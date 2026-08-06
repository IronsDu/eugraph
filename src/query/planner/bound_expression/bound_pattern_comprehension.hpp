#pragma once

#include "query/parser/ast.hpp"

#include "query/planner/bound_type.hpp"
#include "query/planner/slot_id.hpp"

#include <string>

namespace eugraph {
namespace binder {

/// Placeholder bound expression for a PatternComprehension AST node
/// (`[(n)-->(m) | m]`, `size([(n)-->() | 1])`, etc.).
///
/// After the binder hoists each PatternComprehension into a
/// BoundPatternComprehensionApplyOp (which pre-computes a list column per
/// outer row), the original AST node is rewritten to this placeholder
/// carrying the output slot. column_rewrite.cpp then replaces the
/// placeholder with a BoundColumnRef pointing at the same slot, so the
/// evaluator never sees BoundPatternComprehension directly.
struct BoundPatternComprehension {
    /// Slot id of the precomputed list column produced by the corresponding
    /// BoundPatternComprehensionApplyOp. INVALID_SLOT_ID until the hoisting
    /// pass in bind_return/bind_with allocates one.
    SlotId output_slot = INVALID_SLOT_ID;
    /// Output column name (e.g. "__pc_1") — used by column_rewrite to look up
    /// the resolved column index.
    std::string output_name;
    /// Result type: LIST(element_type).
    BoundType result_type = BoundType::Any();
    /// Non-owning pointer back to the AST node. The hoisting pass reads
    /// `patterns`, `where_pred`, `projection` from here to build the
    /// correlated sub-plan. The AST outlives the binder (owned by parser).
    const cypher::PatternComprehension* ast = nullptr;
};

} // namespace binder
} // namespace eugraph
