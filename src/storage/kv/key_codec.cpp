#include "storage/kv/key_codec.hpp"

namespace eugraph {

void KeyCodec::encodeU64BE(std::string& buf, uint64_t val) {
    for (int i = 7; i >= 0; --i) {
        buf.push_back(static_cast<char>((val >> (i * 8)) & 0xFF));
    }
}

void KeyCodec::encodeU16BE(std::string& buf, uint16_t val) {
    buf.push_back(static_cast<char>((val >> 8) & 0xFF));
    buf.push_back(static_cast<char>(val & 0xFF));
}

void KeyCodec::encodeU8(std::string& buf, uint8_t val) {
    buf.push_back(static_cast<char>(val));
}

uint64_t KeyCodec::decodeU64BE(std::string_view data, size_t offset) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | static_cast<uint8_t>(data[offset + i]);
    }
    return val;
}

uint16_t KeyCodec::decodeU16BE(std::string_view data, size_t offset) {
    return (static_cast<uint8_t>(data[offset]) << 8) | static_cast<uint8_t>(data[offset + 1]);
}

// ==================== Label Reverse Index ====================

std::string KeyCodec::encodeLabelReverseKey(VertexId vertex_id, LabelId label_id) {
    std::string key;
    key.reserve(8 + 2);
    encodeU64BE(key, vertex_id);
    encodeU16BE(key, label_id);
    return key;
}

std::pair<VertexId, LabelId> KeyCodec::decodeLabelReverseKey(std::string_view key) {
    VertexId vertex_id = decodeU64BE(key, 0);
    LabelId label_id = decodeU16BE(key, 8);
    return {vertex_id, label_id};
}

std::string KeyCodec::encodeLabelReversePrefix(VertexId vertex_id) {
    std::string key;
    key.reserve(8);
    encodeU64BE(key, vertex_id);
    return key;
}

// ==================== Label Forward Index ====================

std::string KeyCodec::encodeLabelForwardKey(VertexId vertex_id) {
    std::string key;
    key.reserve(8);
    encodeU64BE(key, vertex_id);
    return key;
}

VertexId KeyCodec::decodeLabelForwardKey(std::string_view key) {
    return decodeU64BE(key, 0);
}

// ==================== Primary Key Forward Index ====================

std::string KeyCodec::encodePkForwardKey(std::string_view pk_value) {
    return std::string(pk_value);
}

// ==================== Primary Key Reverse Index ====================

std::string KeyCodec::encodePkReverseKey(VertexId vertex_id) {
    std::string key;
    key.reserve(8);
    encodeU64BE(key, vertex_id);
    return key;
}

VertexId KeyCodec::decodePkReverseKey(std::string_view key) {
    return decodeU64BE(key, 0);
}

// ==================== Vertex Property Storage ====================

std::string KeyCodec::encodeVPropKey(VertexId vertex_id, uint16_t prop_id) {
    std::string key;
    key.reserve(8 + 2);
    encodeU64BE(key, vertex_id);
    encodeU16BE(key, prop_id);
    return key;
}

std::pair<VertexId, uint16_t> KeyCodec::decodeVPropKey(std::string_view key) {
    VertexId vertex_id = decodeU64BE(key, 0);
    uint16_t prop_id = decodeU16BE(key, 8);
    return {vertex_id, prop_id};
}

std::string KeyCodec::encodeVPropPrefix(VertexId vertex_id) {
    std::string key;
    key.reserve(8);
    encodeU64BE(key, vertex_id);
    return key;
}

// ==================== Edge Index ====================

std::string KeyCodec::encodeEdgeIndexKey(const EdgeIndexKey& k) {
    std::string key;
    key.reserve(8 + 1 + 2 + 8 + 8);
    encodeU64BE(key, k.vertex_id);
    encodeU8(key, static_cast<uint8_t>(k.direction));
    encodeU16BE(key, k.edge_label_id);
    encodeU64BE(key, k.neighbor_id);
    encodeU64BE(key, k.seq);
    return key;
}

KeyCodec::EdgeIndexKey KeyCodec::decodeEdgeIndexKey(std::string_view key) {
    EdgeIndexKey k;
    size_t off = 0;
    k.vertex_id = decodeU64BE(key, off);
    off += 8;
    k.direction = static_cast<Direction>(static_cast<uint8_t>(key[off]));
    off += 1;
    k.edge_label_id = decodeU16BE(key, off);
    off += 2;
    k.neighbor_id = decodeU64BE(key, off);
    off += 8;
    k.seq = decodeU64BE(key, off);
    return k;
}

std::string KeyCodec::encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction) {
    std::string key;
    key.reserve(8 + 1);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(direction));
    return key;
}

std::string KeyCodec::encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction, EdgeLabelId label_id) {
    std::string key;
    key.reserve(8 + 1 + 2);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(direction));
    encodeU16BE(key, label_id);
    return key;
}

std::string KeyCodec::encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction, EdgeLabelId label_id,
                                            VertexId neighbor_id) {
    std::string key;
    key.reserve(8 + 1 + 2 + 8);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(direction));
    encodeU16BE(key, label_id);
    encodeU64BE(key, neighbor_id);
    return key;
}

// ==================== Edge Type Index ====================

std::string KeyCodec::encodeEdgeTypeIndexKey(const EdgeTypeIndexKey& k) {
    std::string key;
    key.reserve(8 + 8 + 8);
    encodeU64BE(key, k.src_vertex_id);
    encodeU64BE(key, k.dst_vertex_id);
    encodeU64BE(key, k.seq);
    return key;
}

KeyCodec::EdgeTypeIndexKey KeyCodec::decodeEdgeTypeIndexKey(std::string_view key) {
    EdgeTypeIndexKey k;
    k.src_vertex_id = decodeU64BE(key, 0);
    k.dst_vertex_id = decodeU64BE(key, 8);
    k.seq = decodeU64BE(key, 16);
    return k;
}

std::string KeyCodec::encodeEdgeTypeIndexPrefix() {
    return {};
}

std::string KeyCodec::encodeEdgeTypeIndexPrefix(VertexId src_id) {
    std::string key;
    key.reserve(8);
    encodeU64BE(key, src_id);
    return key;
}

std::string KeyCodec::encodeEdgeTypeIndexPrefix(VertexId src_id, VertexId dst_id) {
    std::string key;
    key.reserve(8 + 8);
    encodeU64BE(key, src_id);
    encodeU64BE(key, dst_id);
    return key;
}

// ==================== Edge Property Storage ====================

std::string KeyCodec::encodeEPropKey(EdgeId edge_id, uint16_t prop_id) {
    std::string key;
    key.reserve(8 + 2);
    encodeU64BE(key, edge_id);
    encodeU16BE(key, prop_id);
    return key;
}

std::pair<EdgeId, uint16_t> KeyCodec::decodeEPropKey(std::string_view key) {
    EdgeId edge_id = decodeU64BE(key, 0);
    uint16_t prop_id = decodeU16BE(key, 8);
    return {edge_id, prop_id};
}

std::string KeyCodec::encodeEPropPrefix(EdgeId edge_id) {
    std::string key;
    key.reserve(8);
    encodeU64BE(key, edge_id);
    return key;
}

} // namespace eugraph
