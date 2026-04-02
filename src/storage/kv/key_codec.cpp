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

// ==================== Label Reverse Index (L|) ====================

std::string KeyCodec::encodeLabelReverseKey(VertexId vertex_id, LabelId label_id) {
    std::string key;
    key.reserve(1 + 8 + 2);
    key.push_back(static_cast<char>(PREFIX_LABEL_REVERSE));
    encodeU64BE(key, vertex_id);
    encodeU16BE(key, label_id);
    return key;
}

std::pair<VertexId, LabelId> KeyCodec::decodeLabelReverseKey(std::string_view key) {
    VertexId vertex_id = decodeU64BE(key, 1);
    LabelId label_id = decodeU16BE(key, 9);
    return {vertex_id, label_id};
}

std::string KeyCodec::encodeLabelReversePrefix(VertexId vertex_id) {
    std::string key;
    key.reserve(1 + 8);
    key.push_back(static_cast<char>(PREFIX_LABEL_REVERSE));
    encodeU64BE(key, vertex_id);
    return key;
}

// ==================== Label Forward Index (I|) ====================

std::string KeyCodec::encodeLabelForwardKey(LabelId label_id, VertexId vertex_id) {
    std::string key;
    key.reserve(1 + 2 + 8);
    key.push_back(static_cast<char>(PREFIX_LABEL_FORWARD));
    encodeU16BE(key, label_id);
    encodeU64BE(key, vertex_id);
    return key;
}

std::pair<LabelId, VertexId> KeyCodec::decodeLabelForwardKey(std::string_view key) {
    LabelId label_id = decodeU16BE(key, 1);
    VertexId vertex_id = decodeU64BE(key, 3);
    return {label_id, vertex_id};
}

std::string KeyCodec::encodeLabelForwardPrefix(LabelId label_id) {
    std::string key;
    key.reserve(1 + 2);
    key.push_back(static_cast<char>(PREFIX_LABEL_FORWARD));
    encodeU16BE(key, label_id);
    return key;
}

// ==================== Primary Key Forward Index (P|) ====================

std::string KeyCodec::encodePkForwardKey(std::string_view pk_value) {
    std::string key;
    key.reserve(1 + pk_value.size());
    key.push_back(static_cast<char>(PREFIX_PK_FORWARD));
    key.append(pk_value);
    return key;
}

// ==================== Primary Key Reverse Index (R|) ====================

std::string KeyCodec::encodePkReverseKey(VertexId vertex_id) {
    std::string key;
    key.reserve(1 + 8);
    key.push_back(static_cast<char>(PREFIX_PK_REVERSE));
    encodeU64BE(key, vertex_id);
    return key;
}

// ==================== Vertex Property Storage (X|) ====================

std::string KeyCodec::encodePropertyKey(LabelId label_id, VertexId vertex_id, uint16_t prop_id) {
    std::string key;
    key.reserve(1 + 2 + 8 + 2);
    key.push_back(static_cast<char>(PREFIX_PROPERTY));
    encodeU16BE(key, label_id);
    encodeU64BE(key, vertex_id);
    encodeU16BE(key, prop_id);
    return key;
}

std::tuple<LabelId, VertexId, uint16_t> KeyCodec::decodePropertyKey(std::string_view key) {
    LabelId label_id = decodeU16BE(key, 1);
    VertexId vertex_id = decodeU64BE(key, 3);
    uint16_t prop_id = decodeU16BE(key, 11);
    return {label_id, vertex_id, prop_id};
}

std::string KeyCodec::encodePropertyPrefix(LabelId label_id, VertexId vertex_id) {
    std::string key;
    key.reserve(1 + 2 + 8);
    key.push_back(static_cast<char>(PREFIX_PROPERTY));
    encodeU16BE(key, label_id);
    encodeU64BE(key, vertex_id);
    return key;
}

// ==================== Edge Index (E|) ====================

std::string KeyCodec::encodeEdgeIndexKey(const EdgeIndexKey& k) {
    std::string key;
    key.reserve(1 + 8 + 1 + 2 + 8 + 8);
    key.push_back(static_cast<char>(PREFIX_EDGE_INDEX));
    encodeU64BE(key, k.vertex_id);
    key.push_back(static_cast<char>(k.direction));
    encodeU16BE(key, k.edge_label_id);
    encodeU64BE(key, k.neighbor_id);
    encodeU64BE(key, k.seq);
    return key;
}

KeyCodec::EdgeIndexKey KeyCodec::decodeEdgeIndexKey(std::string_view key) {
    EdgeIndexKey k;
    size_t off = 1;
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
    key.reserve(1 + 8 + 1);
    key.push_back(static_cast<char>(PREFIX_EDGE_INDEX));
    encodeU64BE(key, vertex_id);
    key.push_back(static_cast<char>(direction));
    return key;
}

std::string KeyCodec::encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction, EdgeLabelId label_id) {
    std::string key;
    key.reserve(1 + 8 + 1 + 2);
    key.push_back(static_cast<char>(PREFIX_EDGE_INDEX));
    encodeU64BE(key, vertex_id);
    key.push_back(static_cast<char>(direction));
    encodeU16BE(key, label_id);
    return key;
}

std::string KeyCodec::encodeEdgeIndexPrefix(VertexId vertex_id, Direction direction, EdgeLabelId label_id,
                                            VertexId neighbor_id) {
    std::string key;
    key.reserve(1 + 8 + 1 + 2 + 8);
    key.push_back(static_cast<char>(PREFIX_EDGE_INDEX));
    encodeU64BE(key, vertex_id);
    key.push_back(static_cast<char>(direction));
    encodeU16BE(key, label_id);
    encodeU64BE(key, neighbor_id);
    return key;
}

// ==================== Edge Type Index (G|) ====================

std::string KeyCodec::encodeEdgeTypeIndexKey(const EdgeTypeIndexKey& k) {
    std::string key;
    key.reserve(1 + 2 + 8 + 8 + 8);
    key.push_back(static_cast<char>(PREFIX_EDGE_TYPE_INDEX));
    encodeU16BE(key, k.edge_label_id);
    encodeU64BE(key, k.src_vertex_id);
    encodeU64BE(key, k.dst_vertex_id);
    encodeU64BE(key, k.seq);
    return key;
}

KeyCodec::EdgeTypeIndexKey KeyCodec::decodeEdgeTypeIndexKey(std::string_view key) {
    EdgeTypeIndexKey k;
    k.edge_label_id = decodeU16BE(key, 1);
    k.src_vertex_id = decodeU64BE(key, 3);
    k.dst_vertex_id = decodeU64BE(key, 11);
    k.seq = decodeU64BE(key, 19);
    return k;
}

std::string KeyCodec::encodeEdgeTypeIndexPrefix(EdgeLabelId label_id) {
    std::string key;
    key.reserve(1 + 2);
    key.push_back(static_cast<char>(PREFIX_EDGE_TYPE_INDEX));
    encodeU16BE(key, label_id);
    return key;
}

std::string KeyCodec::encodeEdgeTypeIndexPrefix(EdgeLabelId label_id, VertexId src_id) {
    std::string key;
    key.reserve(1 + 2 + 8);
    key.push_back(static_cast<char>(PREFIX_EDGE_TYPE_INDEX));
    encodeU16BE(key, label_id);
    encodeU64BE(key, src_id);
    return key;
}

std::string KeyCodec::encodeEdgeTypeIndexPrefix(EdgeLabelId label_id, VertexId src_id, VertexId dst_id) {
    std::string key;
    key.reserve(1 + 2 + 8 + 8);
    key.push_back(static_cast<char>(PREFIX_EDGE_TYPE_INDEX));
    encodeU16BE(key, label_id);
    encodeU64BE(key, src_id);
    encodeU64BE(key, dst_id);
    return key;
}

// ==================== Edge Property Storage (D|) ====================

std::string KeyCodec::encodeEdgePropertyKey(EdgeLabelId label_id, EdgeId edge_id, uint16_t prop_id) {
    std::string key;
    key.reserve(1 + 2 + 8 + 2);
    key.push_back(static_cast<char>(PREFIX_EDGE_PROPERTY));
    encodeU16BE(key, label_id);
    encodeU64BE(key, edge_id);
    encodeU16BE(key, prop_id);
    return key;
}

std::tuple<EdgeLabelId, EdgeId, uint16_t> KeyCodec::decodeEdgePropertyKey(std::string_view key) {
    EdgeLabelId label_id = decodeU16BE(key, 1);
    EdgeId edge_id = decodeU64BE(key, 3);
    uint16_t prop_id = decodeU16BE(key, 11);
    return {label_id, edge_id, prop_id};
}

std::string KeyCodec::encodeEdgePropertyPrefix(EdgeLabelId label_id, EdgeId edge_id) {
    std::string key;
    key.reserve(1 + 2 + 8);
    key.push_back(static_cast<char>(PREFIX_EDGE_PROPERTY));
    encodeU16BE(key, label_id);
    encodeU64BE(key, edge_id);
    return key;
}

} // namespace eugraph
