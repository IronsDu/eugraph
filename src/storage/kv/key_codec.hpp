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
    /// vertex_data record kinds. All keys are {vertex_id:u64BE}{kind:u8}...
    enum class VertexDataKind : uint8_t {
        LABEL = 0x00,
        PROPERTY = 0x01,
        OUT_ADJACENCY = 0x02,
        IN_ADJACENCY = 0x03,
    };

    // ==================== vertex_data (table:vertex_data) ====================
    // {vertex}{0x00}{label_id:u16BE} -> empty
    static std::string encodeVertexDataLabelKey(VertexId vertex_id, LabelId label_id);
    static std::pair<VertexId, LabelId> decodeVertexDataLabelKey(std::string_view key);
    static std::string encodeVertexDataLabelPrefix(VertexId vertex_id);

    // {vertex}{0x01}{label_id:u16BE}{prop_id:u16BE} -> ValueCodec value
    static std::string encodeVertexDataPropKey(VertexId vertex_id, LabelId label_id, uint16_t prop_id);
    static std::tuple<VertexId, LabelId, uint16_t> decodeVertexDataPropKey(std::string_view key);
    static std::string encodeVertexDataPropPrefix(VertexId vertex_id);                    // all props of vertex
    static std::string encodeVertexDataPropPrefix(VertexId vertex_id, LabelId label_id); // props of one label

    static std::string encodeVertexDataVertexPrefix(VertexId vertex_id); // every record of vertex
    static VertexDataKind vertexDataKind(std::string_view key);

    // ==================== Label Forward Index (table:label_fwd_{id}) ====================
    // Key: {vertex_id:uint64 BE}
    static std::string encodeLabelForwardKey(VertexId vertex_id);
    static VertexId decodeLabelForwardKey(std::string_view key);

    // ==================== Vertex Existence (table:vertex_existence) ====================
    // Key: {vertex_id:uint64 BE} — enumerates every live vertex, including unlabeled ones.
    static std::string encodeVertexExistenceKey(VertexId vertex_id);
    static VertexId decodeVertexExistenceKey(std::string_view key);

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
