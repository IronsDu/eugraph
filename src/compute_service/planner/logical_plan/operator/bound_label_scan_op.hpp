#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundLabelScanOp {
    std::string variable;
    uint32_t column_index;
    LabelId label_id;
    std::string label_name;
    std::vector<uint16_t> prop_ids;
};

} // namespace binder
} // namespace eugraph
