#pragma once

#include "common/types/graph_types.hpp"

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

} // namespace optimizer
} // namespace eugraph
