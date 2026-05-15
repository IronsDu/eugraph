#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eugraph::tck {

// Mirrors the TCK side-effect metrics
struct SideEffects {
    int64_t nodes = 0;
    int64_t relationships = 0;
    int64_t properties = 0;
    int64_t labels = 0;

    bool isZero() const {
        return nodes == 0 && relationships == 0 && properties == 0 && labels == 0;
    }
};

// Internal row representation for result comparison
struct TckCell {
    std::string value; // TCK-formatted string (e.g., "(:Label {prop: 'val'})")
};

struct TckRow {
    std::vector<TckCell> cells;
};

// Tracks graph state before/after a query to compute side effects
struct GraphSnapshot {
    int64_t nodeCount = 0;
    int64_t edgeCount = 0;
    int64_t labelCount = 0;
    int64_t propertyCount = 0;
};

} // namespace eugraph::tck
