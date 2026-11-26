#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundLabelScanOp {
    std::string variable;
    uint32_t column_index;
    std::vector<LabelId> label_ids;
    std::vector<std::string> label_names;
    std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids;
    // Set by the logical reorder rule: emit vertices from an index scan over
    // runtime-provided values instead of a full label scan.
    bool index_scan_values = false;
    uint32_t index_id = 0;
    // Optimizer-selected ordinary index access path. Set by the access-path
    // pass on a direct Filter(LabelScan); physical lowering only instantiates it.
    bool index_scan = false;
    std::vector<uint16_t> index_prop_ids;
    std::vector<PropertyValue> index_eq_values;
    std::optional<PropertyValue> index_lower;
    std::optional<PropertyValue> index_upper;
    bool index_lower_inclusive = false;
    bool index_upper_inclusive = false;
};

} // namespace binder
} // namespace eugraph
