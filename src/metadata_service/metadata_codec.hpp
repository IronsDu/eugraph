#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace eugraph {

/// Binary serialization/deserialization for metadata types.
///
/// Used to persist LabelDef, EdgeLabelDef, PropertyDef, and PropertyValue
/// into the M| keyspace of the metadata table.
class MetadataCodec {
public:
    // ==================== LabelDef ====================

    static std::string encodeLabelDef(const LabelDef& def);
    static LabelDef decodeLabelDef(std::string_view data);

    // ==================== EdgeLabelDef ====================

    static std::string encodeEdgeLabelDef(const EdgeLabelDef& def);
    static EdgeLabelDef decodeEdgeLabelDef(std::string_view data);

    // ==================== ID counters ====================

    /// Encode next_ids: {next_vertex_id}|{next_edge_id}|{next_label_id}|{next_edge_label_id}
    static std::string encodeNextIds(VertexId next_vid, EdgeId next_eid, LabelId next_lid, EdgeLabelId next_elid);
    static void decodeNextIds(std::string_view data, VertexId& next_vid, EdgeId& next_eid, LabelId& next_lid,
                              EdgeLabelId& next_elid);

    // ==================== PropertyValue ====================

    static void encodePropertyValue(std::string& buf, const PropertyValue& value);
    static PropertyValue decodePropertyValue(std::string_view data, size_t& offset);

private:
    // ==================== PropertyDef ====================

    static void encodePropertyDef(std::string& buf, const PropertyDef& prop);
    static PropertyDef decodePropertyDef(std::string_view data, size_t& offset);

    // ==================== Primitive helpers ====================

    static void encodeU64(std::string& buf, uint64_t val);
    static uint64_t decodeU64(std::string_view data, size_t& offset);

    static void encodeU16(std::string& buf, uint16_t val);
    static uint16_t decodeU16(std::string_view data, size_t& offset);

    static void encodeU8(std::string& buf, uint8_t val);
    static uint8_t decodeU8(std::string_view data, size_t& offset);

    static void encodeString(std::string& buf, const std::string& str);
    static std::string decodeString(std::string_view data, size_t& offset);
};

} // namespace eugraph
