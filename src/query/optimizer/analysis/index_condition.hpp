#pragma once

#include "common/types/graph_types.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

#include <functional>

#include <optional>

namespace eugraph {
namespace optimizer {

/// One side of a storage index range. The value is the user-visible bound; the
/// inclusive flag is kept separately so storage cursors can decide the encoding
/// instead of callers shifting numeric values by ±1.
struct IndexBound {
    PropertyValue value;
    bool inclusive = false;
};

/// Indexable range predicates for one property, merged to the strongest bounds.
struct IndexRange {
    std::optional<IndexBound> lower;
    std::optional<IndexBound> upper;
};

/// Total-order comparison for the scalar property kinds accepted by indexes.
/// Returns 0 for unsupported cross-kind pairs; callers only merge same-kind
/// predicates.
int compareIndexValues(const PropertyValue& a, const PropertyValue& b);

void mergeIndexLower(IndexRange& range, const PropertyValue& value, bool inclusive);
void mergeIndexUpper(IndexRange& range, const PropertyValue& value, bool inclusive);

/// A property/value comparison extracted from a Filter expression. This is the
/// shared representation used by optimizer access-path rules and the fallback
/// physical planner during migration.
struct IndexableCondition {
    LabelId label_id = INVALID_LABEL_ID;
    uint16_t prop_id = 0;
    std::string property_name;
    cypher::BinaryOperator op = cypher::BinaryOperator::EQ;
    PropertyValue value;
    bool is_strong = false;
    /// Number of labels declaring the property (weak space excluded).
    int declared_label_count = 0;
};

/// Property semantics for a BoundColumnRef (already-lowered property column).
struct ResolvedProperty {
    std::string name;
    LabelId label_id = INVALID_LABEL_ID;
    uint16_t prop_id = 0;
    bool is_strong = false;
    int declared_label_count = 0;
};

using ColumnPropertyResolver = std::function<std::optional<ResolvedProperty>(const binder::BoundColumnRef&)>;

/// Normalize a comparison (`property op literal` or reversed `literal op
/// property`) into an IndexableCondition.
std::optional<IndexableCondition> extractIndexableCondition(const binder::BoundBinaryOp& binop, LabelId anon_label_id,
                                                            const ColumnPropertyResolver& resolve_column);
std::optional<IndexableCondition> extractNormalizedIndexableCondition(const binder::BoundBinaryOp& binop,
                                                                      LabelId anon_label_id,
                                                                      const ColumnPropertyResolver& resolve_column);

} // namespace optimizer
} // namespace eugraph
