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

// ==================== Vertex Existence ====================

std::string KeyCodec::encodeVertexExistenceKey(VertexId vertex_id) {
    std::string key;
    key.reserve(8);
    encodeU64BE(key, vertex_id);
    return key;
}

VertexId KeyCodec::decodeVertexExistenceKey(std::string_view key) {
    return decodeU64BE(key, 0);
}

// ==================== vertex_data ====================

std::string KeyCodec::encodeVertexDataLabelKey(VertexId vertex_id, LabelId label_id) {
    std::string key;
    key.reserve(8 + 1 + 2);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(VertexDataKind::LABEL));
    encodeU16BE(key, label_id);
    return key;
}

std::pair<VertexId, LabelId> KeyCodec::decodeVertexDataLabelKey(std::string_view key) {
    return {decodeU64BE(key, 0), decodeU16BE(key, 9)};
}

std::string KeyCodec::encodeVertexDataLabelPrefix(VertexId vertex_id) {
    std::string key;
    key.reserve(8 + 1);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(VertexDataKind::LABEL));
    return key;
}

std::string KeyCodec::encodeVertexDataPropKey(VertexId vertex_id, LabelId label_id, uint16_t prop_id) {
    std::string key;
    key.reserve(8 + 1 + 2 + 2);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(VertexDataKind::PROPERTY));
    encodeU16BE(key, label_id);
    encodeU16BE(key, prop_id);
    return key;
}

std::tuple<VertexId, LabelId, uint16_t> KeyCodec::decodeVertexDataPropKey(std::string_view key) {
    return {decodeU64BE(key, 0), decodeU16BE(key, 9), decodeU16BE(key, 11)};
}

std::string KeyCodec::encodeVertexDataPropPrefix(VertexId vertex_id) {
    std::string key;
    key.reserve(8 + 1);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(VertexDataKind::PROPERTY));
    return key;
}

std::string KeyCodec::encodeVertexDataPropPrefix(VertexId vertex_id, LabelId label_id) {
    std::string key;
    key.reserve(8 + 1 + 2);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(VertexDataKind::PROPERTY));
    encodeU16BE(key, label_id);
    return key;
}

std::string KeyCodec::encodeVertexDataVertexPrefix(VertexId vertex_id) {
    std::string key;
    key.reserve(8);
    encodeU64BE(key, vertex_id);
    return key;
}

KeyCodec::VertexDataKind KeyCodec::vertexDataKind(std::string_view key) {
    return static_cast<VertexDataKind>(static_cast<uint8_t>(key[8]));
}

std::string KeyCodec::encodeVertexDataAdjStateKey(VertexId vertex_id, Direction direction) {
    std::string key;
    key.reserve(8 + 1);
    encodeU64BE(key, vertex_id);
    encodeU8(key, static_cast<uint8_t>(direction == Direction::IN ? VertexDataKind::IN_ADJACENCY
                                                                  : VertexDataKind::OUT_ADJACENCY));
    return key;
}

std::string KeyCodec::encodeVertexDataAdjPrefix(VertexId vertex_id, Direction direction) {
    return encodeVertexDataAdjStateKey(vertex_id, direction);
}

std::string KeyCodec::encodeVertexDataAdjEdgeKey(const VertexDataAdjEdgeKey& k) {
    std::string key;
    key.reserve(8 + 1 + 2 + 8 + 8);
    encodeU64BE(key, k.vertex_id);
    encodeU8(key, static_cast<uint8_t>(k.direction == Direction::IN ? VertexDataKind::IN_ADJACENCY
                                                                    : VertexDataKind::OUT_ADJACENCY));
    encodeU16BE(key, k.edge_label_id);
    encodeU64BE(key, k.neighbor_id);
    encodeU64BE(key, k.seq);
    return key;
}

KeyCodec::VertexDataAdjEdgeKey KeyCodec::decodeVertexDataAdjEdgeKey(std::string_view key) {
    VertexDataAdjEdgeKey k;
    k.vertex_id = decodeU64BE(key, 0);
    k.direction = vertexDataKind(key) == VertexDataKind::IN_ADJACENCY ? Direction::IN : Direction::OUT;
    k.edge_label_id = decodeU16BE(key, 9);
    k.neighbor_id = decodeU64BE(key, 11);
    k.seq = decodeU64BE(key, 19);
    return k;
}

bool KeyCodec::isVertexDataAdjStateKey(std::string_view key) {
    return key.size() == 9;
}

std::string KeyCodec::encodeAdjacencyState(AdjacencyCacheState state, uint32_t count) {
    std::string value;
    value.reserve(5);
    encodeU8(value, static_cast<uint8_t>(state));
    value.push_back(static_cast<char>((count >> 24) & 0xFF));
    value.push_back(static_cast<char>((count >> 16) & 0xFF));
    value.push_back(static_cast<char>((count >> 8) & 0xFF));
    value.push_back(static_cast<char>(count & 0xFF));
    return value;
}

std::pair<KeyCodec::AdjacencyCacheState, uint32_t> KeyCodec::decodeAdjacencyState(std::string_view value) {
    if (value.size() < 5)
        return {AdjacencyCacheState::OVERFLOW, 0};
    uint32_t count = (static_cast<uint32_t>(static_cast<uint8_t>(value[1])) << 24) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(value[2])) << 16) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(value[3])) << 8) |
                     static_cast<uint32_t>(static_cast<uint8_t>(value[4]));
    return {static_cast<AdjacencyCacheState>(static_cast<uint8_t>(value[0])), count};
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
