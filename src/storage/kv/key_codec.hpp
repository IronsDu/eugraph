#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>

namespace eugraph {

// Encodes and decodes KV keys for all graph storage prefixes.
// All multi-byte integers use big-endian encoding.
class KeyCodec {
public:
    // ==================== Label Reverse Index (L|) ====================
    // Key: L|{vertex_id:uint64 BE}|{label_id:uint16 BE}
    static std::string encodeLabelReverseKey(VertexId vertex_id, LabelId label_id);
    static std::pair<VertexId, LabelId> decodeLabelReverseKey(std::string_view key);

    // Prefix scan: L|{vertex_id} -> get all labels of a vertex
    static std::string encodeLabelReversePrefix(VertexId vertex_id);

    // ==================== Label Forward Index (I|) ====================
    // Key: I|{label_id:uint16 BE}|{vertex_id:uint64 BE}
    static std::string encodeLabelForwardKey(LabelId label_id, VertexId vertex_id);
    static std::pair<LabelId, VertexId> decodeLabelForwardKey(std::string_view key);

    // Prefix scan: I|{label_id} -> get all vertices with this label
    static std::string encodeLabelForwardPrefix(LabelId label_id);

    // ==================== Primary Key Forward Index (P|) ====================
    // Key: P|{pk_value:encoded}
    static std::string encodePkForwardKey(std::string_view pk_value);

    // ==================== Primary Key Reverse Index (R|) ====================
    // Key: R|{vertex_id:uint64 BE}
    static std::string encodePkReverseKey(VertexId vertex_id);

    // ==================== Vertex Property Storage (X|) ====================
    // Key: X|{label_id:uint16 BE}|{vertex_id:uint64 BE}|{prop_id:uint16 BE}
    static std::string encodePropertyKey(LabelId label_id, VertexId vertex_id, uint16_t prop_id);
    static std::tuple<LabelId, VertexId, uint16_t> decodePropertyKey(std::string_view key);

    // Prefix scan: X|{label_id}|{vertex_id} -> get all properties
    static std::string encodePropertyPrefix(LabelId label_id, VertexId vertex_id);

    // ==================== Edge Index (E|) ====================
    // Key: E|{vertex_id:uint64 BE}|{direction:uint8}|{edge_label_id:uint16 BE}|{neighbor_id:uint64 BE}|{seq:uint64 BE}
    struct EdgeIndexKey {
        VertexId vertex_id;
        Direction direction;
        EdgeLabelId edge_label_id;
        VertexId neighbor_id;
        uint64_t seq;
    };

    static std::string encodeEdgeIndexKey(const EdgeIndexKey& key);
    static EdgeIndexKey decodeEdgeIndexKey(std::string_view key);

    // Prefix scans
    static std::string encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction);
    static std::string encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction, EdgeLabelId label_id);
    static std::string encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction, EdgeLabelId label_id,
                                             VertexId neighbor_id);

    // ==================== Edge Type Index (G|) ====================
    // Key: G|{edge_label_id:uint16 BE}|{src_vertex_id:uint64 BE}|{dst_vertex_id:uint64 BE}|{seq:uint64 BE}
    struct EdgeTypeIndexKey {
        EdgeLabelId edge_label_id;
        VertexId src_vertex_id;
        VertexId dst_vertex_id;
        uint64_t seq;
    };

    static std::string encodeEdgeTypeIndexKey(const EdgeTypeIndexKey& key);
    static EdgeTypeIndexKey decodeEdgeTypeIndexKey(std::string_view key);

    // Prefix scans
    static std::string encodeEdgeTypeIndexPrefix(EdgeLabelId label_id);
    static std::string encodeEdgeTypeIndexPrefix(EdgeLabelId label_id, VertexId src_id);
    static std::string encodeEdgeTypeIndexPrefix(EdgeLabelId label_id, VertexId src_id, VertexId dst_id);

    // ==================== Edge Property Storage (D|) ====================
    // Key: D|{edge_label_id:uint16 BE}|{edge_id:uint64 BE}|{prop_id:uint16 BE}
    static std::string encodeEdgePropertyKey(EdgeLabelId label_id, EdgeId edge_id, uint16_t prop_id);
    static std::tuple<EdgeLabelId, EdgeId, uint16_t> decodeEdgePropertyKey(std::string_view key);

    // Prefix scan: D|{edge_label_id}|{edge_id} -> get all properties of an edge
    static std::string encodeEdgePropertyPrefix(EdgeLabelId label_id, EdgeId edge_id);

private:
    static void encodeU64BE(std::string& buf, uint64_t val);
    static void encodeU16BE(std::string& buf, uint16_t val);

    static uint64_t decodeU64BE(std::string_view data, size_t offset);
    static uint16_t decodeU16BE(std::string_view data, size_t offset);
};

} // namespace eugraph
