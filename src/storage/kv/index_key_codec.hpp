#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace eugraph {

class IndexKeyCodec {
public:
    static std::string encodeSortableValue(const PropertyValue& value);
    static std::string encodeIndexKey(const PropertyValue& value, uint64_t entity_id);
    static uint64_t decodeEntityId(std::string_view key);
    static std::string encodeEqualityPrefix(const PropertyValue& value);
};

} // namespace eugraph
