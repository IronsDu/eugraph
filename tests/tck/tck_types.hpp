#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eugraph::tck {

// Mirrors the TCK side-effect metrics
struct SideEffects {
    int64_t nodes = 0;
    int64_t relationships = 0;
    int64_t properties = 0;
    int64_t labels = 0;
    // Separate add/remove for DELETE+MERGE interaction
    int64_t removed_nodes = 0;
    int64_t removed_relationships = 0;
    int64_t removed_properties = 0;
    int64_t removed_labels = 0;

    bool isZero() const {
        return nodes == 0 && relationships == 0 && properties == 0 && labels == 0 && removed_nodes == 0 &&
               removed_relationships == 0 && removed_properties == 0 && removed_labels == 0;
    }
};

// Internal row representation for result comparison
struct TckCell {
    std::string value; // TCK-formatted string (e.g., "(:Label {prop: 'val'})")
};

struct TckRow {
    std::vector<TckCell> cells;
};

// Property key: (element_id, prop_name). Value: serialized form (via formatResultValue).
using PropertyMap = std::map<std::pair<int64_t, std::string>, std::string>;

// Tracks graph state before/after a query to compute side effects
struct GraphSnapshot {
    int64_t nodeCount = 0;
    int64_t edgeCount = 0;
    int64_t propertyCount = 0;
    // Distinct label names for correct +labels/-labels counting
    std::unordered_set<std::string> labelNames;
    // ID sets for add/remove detection
    std::unordered_set<int64_t> nodeIds;
    std::unordered_set<int64_t> edgeIds;
    // Per-property state for accurate add/remove/modify counting.
    // A modification (same key, different value) counts as BOTH +1 and -1
    // under TCK semantics.
    PropertyMap vertexProps;
    PropertyMap edgeProps;
};

} // namespace eugraph::tck
