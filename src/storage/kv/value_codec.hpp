#pragma once

#include "common/types/graph_types.hpp"

#include <string>
#include <string_view>

namespace eugraph {

// Serializes and deserializes PropertyValue to/from binary format.
//
// Binary format:
//   [1 byte type tag][variable length data]
//
// Type tags:
//   0x00: null (monostate)       - no data
//   0x01: bool                   - 1 byte (0x00=false, 0x01=true)
//   0x02: int64_t                - 8 bytes big-endian
//   0x03: double                 - 8 bytes IEEE 754 big-endian
//   0x04: string                 - 4 bytes length (BE) + data
//   0x05: vector<int64_t>        - 4 bytes count (BE) + N*8 bytes
//   0x06: vector<double>         - 4 bytes count (BE) + N*8 bytes
//   0x07: vector<string>         - 4 bytes count (BE) + N*(4 bytes len + data)

class ValueCodec {
public:
    static std::string encode(const PropertyValue& value);
    static PropertyValue decode(std::string_view data);

    // Encode a uint64_t value directly (for vertex_id, edge_id values in index records)
    static std::string encodeU64(uint64_t val);
    static uint64_t decodeU64(std::string_view data);

private:
    static constexpr uint8_t TAG_NULL = 0x00;
    static constexpr uint8_t TAG_BOOL = 0x01;
    static constexpr uint8_t TAG_INT64 = 0x02;
    static constexpr uint8_t TAG_DOUBLE = 0x03;
    static constexpr uint8_t TAG_STRING = 0x04;
    static constexpr uint8_t TAG_INT64_ARRAY = 0x05;
    static constexpr uint8_t TAG_DOUBLE_ARRAY = 0x06;
    static constexpr uint8_t TAG_STRING_ARRAY = 0x07;
};

} // namespace eugraph
