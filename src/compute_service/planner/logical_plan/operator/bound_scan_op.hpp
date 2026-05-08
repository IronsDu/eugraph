#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundScanOp {
    std::string variable;
    uint32_t column_index;
    std::vector<uint16_t> prop_ids;
};

} // namespace binder
} // namespace eugraph
