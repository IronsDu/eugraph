#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>

namespace eugraph {

// Encodes and decodes keys for WiredTiger multi-table storage.
// All multi-byte integers use big-endian encoding.
// No prefix bytes — table identity is determined by the WiredTiger table name.
class KeyCodec {
public:
    // ==================== Label Reverse Index (table:label_reverse) ====================
    // Key: {vertex_id:uint64 BE}{label_id:uint16 BE}
    static std::string encodeLabelReverseKey(VertexId vertex_id, LabelId label_id);
    static std::pair<VertexId, LabelId> decodeLabelReverseKey(std::string_view key);
    static std::string encodeLabelReversePrefix(VertexId vertex_id);

    // ==================== Label Forward Index (table:label_fwd_{id}) ====================
    // Key: {vertex_id:uint64 BE}
    static std::string encodeLabelForwardKey(VertexId vertex_id);
    static VertexId decodeLabelForwardKey(std::string_view key);

    // ==================== Primary Key Forward Index (table:pk_forward) ====================
    // Key: {pk_value:raw}
    static std::string encodePkForwardKey(std::string_view pk_value);

    // ==================== Primary Key Reverse Index (table:pk_reverse) ====================
    // Key: {vertex_id:uint64 BE}
    static std::string encodePkReverseKey(VertexId vertex_id);
    static VertexId decodePkReverseKey(std::string_view key);

    // ==================== Vertex Property Storage (table:vprop_{id}) ====================
    // Key: {vertex_id:uint64 BE}{prop_id:uint16 BE}
    static std::string encodeVPropKey(VertexId vertex_id, uint16_t prop_id);
    static std::pair<VertexId, uint16_t> decodeVPropKey(std::string_view key);
    static std::string encodeVPropPrefix(VertexId vertex_id);

    // ==================== Edge Index (table:edge_index) ====================
    // Key: {vertex_id:uint64 BE}{direction:uint8}{edge_label_id:uint16 BE}{neighbor_id:uint64 BE}{seq:uint64 BE}
    struct EdgeIndexKey {
        VertexId vertex_id;
        Direction direction;
        EdgeLabelId edge_label_id;
        VertexId neighbor_id;
        uint64_t seq;
    };

    static std::string encodeEdgeIndexKey(const EdgeIndexKey& key);
    static EdgeIndexKey decodeEdgeIndexKey(std::string_view key);

    static std::string encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction);
    static std::string encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction, EdgeLabelId label_id);
    static std::string encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction, EdgeLabelId label_id,
                                             VertexId neighbor_id);

    // ==================== Edge Type Index (table:etype_{id}) ====================
    // Key: {src_vertex_id:uint64 BE}{dst_vertex_id:uint64 BE}{seq:uint64 BE}
    struct EdgeTypeIndexKey {
        VertexId src_vertex_id;
        VertexId dst_vertex_id;
        uint64_t seq;
    };

    static std::string encodeEdgeTypeIndexKey(const EdgeTypeIndexKey& key);
    static EdgeTypeIndexKey decodeEdgeTypeIndexKey(std::string_view key);

    static std::string encodeEdgeTypeIndexPrefix();
    static std::string encodeEdgeTypeIndexPrefix(VertexId src_id);
    static std::string encodeEdgeTypeIndexPrefix(VertexId src_id, VertexId dst_id);

    // ==================== Edge Property Storage (table:eprop_{id}) ====================
    // Key: {edge_id:uint64 BE}{prop_id:uint16 BE}
    static std::string encodeEPropKey(EdgeId edge_id, uint16_t prop_id);
    static std::pair<EdgeId, uint16_t> decodeEPropKey(std::string_view key);
    static std::string encodeEPropPrefix(EdgeId edge_id);

private:
    static void encodeU64BE(std::string& buf, uint64_t val);
    static void encodeU16BE(std::string& buf, uint16_t val);
    static void encodeU8(std::string& buf, uint8_t val);

    static uint64_t decodeU64BE(std::string_view data, size_t offset);
    static uint16_t decodeU16BE(std::string_view data, size_t offset);
};

} // namespace eugraph
