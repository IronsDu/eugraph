#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eugraph {

class IndexKeyCodec {
public:
    static std::string encodeSortableValue(const PropertyValue& value);
    static std::string encodeSortableValues(const std::vector<PropertyValue>& values);
    static std::string encodeIndexKey(const PropertyValue& value, uint64_t entity_id);
    static std::string encodeIndexKey(const std::vector<PropertyValue>& values, uint64_t entity_id);
    static uint64_t decodeEntityId(std::string_view key);
    static std::string encodeEqualityPrefix(const PropertyValue& value);
    static std::string encodeEqualityPrefix(const std::vector<PropertyValue>& values);
    /// String-prefix encoding used by `STARTS WITH`: the encoded value has no
    /// string terminator, so it remains a proper prefix of longer strings.
    static std::string encodePrefix(const PropertyValue& value);
    /// Encode exact components before `prefix_col` and a raw prefix for that
    /// component. `prefix_col` must be < values.size().
    static std::string encodePrefix(const std::vector<PropertyValue>& values, size_t prefix_col);
};

} // namespace eugraph
