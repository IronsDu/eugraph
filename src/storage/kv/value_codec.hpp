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
//   0x08: DateTimeValue          - 1 byte kind + all date/time fields as fixed-width BE
//   0x09: TimeValue              - 1 byte kind + time fields as fixed-width BE
//   0x0A: DurationValue          - 4 × int64 fields as fixed-width BE
//   0x0B: vector<DateTimeValue>  - 4 bytes count + N*DateTimeValue(no tag)
//   0x0C: vector<TimeValue>      - 4 bytes count + N*TimeValue(no tag)
//   0x0D: vector<DurationValue>  - 4 bytes count + N*DurationValue(no tag)

class ValueCodec {
public:
    static std::string encode(const PropertyValue& value);
    static PropertyValue decode(std::string_view data);

    // Encode a uint64_t value directly (for vertex_id, edge_id values in index records)
    static std::string encodeU64(uint64_t val);
    static uint64_t decodeU64(std::string_view data);

    // Encode/decode edge adjacency info stored in edge index values
    // Format: {src_id:u64BE}{dst_id:u64BE}{seq:u64BE}{label_id:u16BE} (26 bytes)
    static std::string encodeEdgeAdjacency(VertexId src_id, VertexId dst_id, uint64_t seq, EdgeLabelId label_id);
    static void decodeEdgeAdjacency(std::string_view data, VertexId& src_id, VertexId& dst_id, uint64_t& seq,
                                    EdgeLabelId& label_id);

private:
    static constexpr uint8_t TAG_NULL = 0x00;
    static constexpr uint8_t TAG_BOOL = 0x01;
    static constexpr uint8_t TAG_INT64 = 0x02;
    static constexpr uint8_t TAG_DOUBLE = 0x03;
    static constexpr uint8_t TAG_STRING = 0x04;
    static constexpr uint8_t TAG_INT64_ARRAY = 0x05;
    static constexpr uint8_t TAG_DOUBLE_ARRAY = 0x06;
    static constexpr uint8_t TAG_STRING_ARRAY = 0x07;
    static constexpr uint8_t TAG_DATETIME = 0x08;
    static constexpr uint8_t TAG_TIME = 0x09;
    static constexpr uint8_t TAG_DURATION = 0x0A;
    static constexpr uint8_t TAG_DATETIME_ARRAY = 0x0B;
    static constexpr uint8_t TAG_TIME_ARRAY = 0x0C;
    static constexpr uint8_t TAG_DURATION_ARRAY = 0x0D;
};

} // namespace eugraph
