#include <gtest/gtest.h>

#include "storage/kv/key_codec.hpp"

using namespace eugraph;

class KeyCodecTest : public ::testing::Test {};

// ==================== Vertex Data: labels (table:vertex_data) ====================

TEST_F(KeyCodecTest, EncodeDecodeVertexLabelKey) {
    auto key = KeyCodec::encodeVertexLabelKey(1, 100);
    EXPECT_EQ(key.size(), 8u + 1u + 2u);
    auto [vertex_id, label_id] = KeyCodec::decodeVertexLabelKey(key);
    EXPECT_EQ(vertex_id, 1u);
    EXPECT_EQ(label_id, 100u);
}

TEST_F(KeyCodecTest, VertexLabelKeyLargeValues) {
    auto key = KeyCodec::encodeVertexLabelKey(0xFFFFFFFFFFFFFFFF, 0xFFFF);
    auto [vertex_id, label_id] = KeyCodec::decodeVertexLabelKey(key);
    EXPECT_EQ(vertex_id, 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(label_id, 0xFFFF);
}

TEST_F(KeyCodecTest, VertexLabelPrefix) {
    auto prefix = KeyCodec::encodeVertexLabelPrefix(42);
    EXPECT_EQ(prefix.size(), 8u + 1u);
    auto full_key = KeyCodec::encodeVertexLabelKey(42, 5);
    EXPECT_EQ(full_key.substr(0, prefix.size()), prefix);
}

// ==================== Label Forward Index (table:label_fwd_{id}) ====================

TEST_F(KeyCodecTest, EncodeDecodeLabelForwardKey) {
    auto key = KeyCodec::encodeLabelForwardKey(999);

    // No prefix, no label_id: just 8 bytes
    EXPECT_EQ(key.size(), 8u);

    auto vertex_id = KeyCodec::decodeLabelForwardKey(key);
    EXPECT_EQ(vertex_id, 999u);
}

TEST_F(KeyCodecTest, LabelForwardKeySortOrder) {
    auto key1 = KeyCodec::encodeLabelForwardKey(100);
    auto key2 = KeyCodec::encodeLabelForwardKey(200);
    EXPECT_LT(key1, key2);
}

// ==================== Vertex Data: properties (table:vertex_data) ====================

TEST_F(KeyCodecTest, EncodeDecodeVertexPropertyKey) {
    auto key = KeyCodec::encodeVertexPropertyKey(42, 7, 5);
    EXPECT_EQ(key.size(), 8u + 1u + 2u + 2u);
    auto decoded = KeyCodec::decodeVertexPropertyKey(key);
    EXPECT_EQ(decoded.vertex_id, 42u);
    EXPECT_EQ(decoded.label_id, 7u);
    EXPECT_EQ(decoded.prop_id, 5u);
}

TEST_F(KeyCodecTest, VertexPropertyPrefixes) {
    auto all = KeyCodec::encodeVertexPropertyPrefix(100);
    EXPECT_EQ(all.size(), 8u + 1u);
    auto by_label = KeyCodec::encodeVertexPropertyPrefix(100, 3);
    EXPECT_EQ(by_label.size(), 8u + 1u + 2u);
    auto full = KeyCodec::encodeVertexPropertyKey(100, 3, 9);
    EXPECT_EQ(full.substr(0, all.size()), all);
    EXPECT_EQ(full.substr(0, by_label.size()), by_label);
}

TEST_F(KeyCodecTest, VertexAdjacencyKeyAndPrefix) {
    auto key = KeyCodec::encodeVertexAdjacencyKey(42);
    EXPECT_EQ(key.size(), 8u + 1u);
    auto prefix = KeyCodec::encodeVertexDataPrefix(42);
    EXPECT_EQ(key.substr(0, prefix.size()), prefix);
}

// ==================== Edge Index (table:edge_index) ====================

TEST_F(KeyCodecTest, EncodeDecodeEdgeIndexKey) {
    KeyCodec::EdgeIndexKey input{1, Direction::OUT, 2, 3, 100};

    auto key = KeyCodec::encodeEdgeIndexKey(input);
    // No prefix: 8 + 1 + 2 + 8 + 8 = 27 bytes
    EXPECT_EQ(key.size(), 27u);

    auto decoded = KeyCodec::decodeEdgeIndexKey(key);
    EXPECT_EQ(decoded.vertex_id, 1u);
    EXPECT_EQ(decoded.direction, Direction::OUT);
    EXPECT_EQ(decoded.edge_label_id, 2u);
    EXPECT_EQ(decoded.neighbor_id, 3u);
    EXPECT_EQ(decoded.seq, 100u);
}

TEST_F(KeyCodecTest, EdgeIndexKeyDirection) {
    KeyCodec::EdgeIndexKey out_key{1, Direction::OUT, 2, 3, 0};
    KeyCodec::EdgeIndexKey in_key{1, Direction::IN, 2, 3, 0};

    auto encoded_out = KeyCodec::encodeEdgeIndexKey(out_key);
    auto encoded_in = KeyCodec::encodeEdgeIndexKey(in_key);

    // OUT direction key should sort before IN direction key (0x00 < 0x01)
    EXPECT_LT(encoded_out, encoded_in);
}

TEST_F(KeyCodecTest, EdgeIndexPrefixVertexDirection) {
    auto prefix = KeyCodec::encodeEdgeIndexPrefix(1, Direction::OUT);
    EXPECT_EQ(prefix.size(), 9u); // 8 + 1

    auto full_key = KeyCodec::encodeEdgeIndexKey({1, Direction::OUT, 2, 3, 0});
    EXPECT_EQ(full_key.substr(0, 9), prefix);
}

TEST_F(KeyCodecTest, EdgeIndexPrefixWithLabel) {
    auto prefix = KeyCodec::encodeEdgeIndexPrefix(1, Direction::OUT, 5);
    EXPECT_EQ(prefix.size(), 11u); // 8 + 1 + 2
}

TEST_F(KeyCodecTest, EdgeIndexPrefixWithNeighbor) {
    auto prefix = KeyCodec::encodeEdgeIndexPrefix(1, Direction::OUT, 5, 10);
    EXPECT_EQ(prefix.size(), 19u); // 8 + 1 + 2 + 8
}

// ==================== Edge Type Index (table:etype_{id}) ====================

TEST_F(KeyCodecTest, EncodeDecodeEdgeTypeIndexKey) {
    KeyCodec::EdgeTypeIndexKey input{10, 20, 5};

    auto key = KeyCodec::encodeEdgeTypeIndexKey(input);
    // No prefix, no edge_label_id: 8 + 8 + 8 = 24 bytes
    EXPECT_EQ(key.size(), 24u);

    auto decoded = KeyCodec::decodeEdgeTypeIndexKey(key);
    EXPECT_EQ(decoded.src_vertex_id, 10u);
    EXPECT_EQ(decoded.dst_vertex_id, 20u);
    EXPECT_EQ(decoded.seq, 5u);
}

TEST_F(KeyCodecTest, EdgeTypeIndexPrefixEmpty) {
    auto prefix = KeyCodec::encodeEdgeTypeIndexPrefix();
    EXPECT_EQ(prefix.size(), 0u);
}

TEST_F(KeyCodecTest, EdgeTypeIndexPrefixBySrc) {
    auto prefix = KeyCodec::encodeEdgeTypeIndexPrefix(10);
    EXPECT_EQ(prefix.size(), 8u);

    auto full_key = KeyCodec::encodeEdgeTypeIndexKey({10, 20, 0});
    EXPECT_EQ(full_key.substr(0, 8), prefix);

    auto other_key = KeyCodec::encodeEdgeTypeIndexKey({99, 20, 0});
    EXPECT_NE(other_key.substr(0, 8), prefix);
}

TEST_F(KeyCodecTest, EdgeTypeIndexPrefixBySrcDst) {
    auto prefix = KeyCodec::encodeEdgeTypeIndexPrefix(10, 20);
    EXPECT_EQ(prefix.size(), 16u);

    auto full_key = KeyCodec::encodeEdgeTypeIndexKey({10, 20, 0});
    EXPECT_EQ(full_key.substr(0, 16), prefix);
}

TEST_F(KeyCodecTest, EdgeTypeIndexKeysSortBySrc) {
    auto key1 = KeyCodec::encodeEdgeTypeIndexKey({10, 20, 0});
    auto key2 = KeyCodec::encodeEdgeTypeIndexKey({20, 10, 0});
    EXPECT_LT(key1, key2);
}

TEST_F(KeyCodecTest, EdgeTypeIndexKeysSortByDst) {
    auto key1 = KeyCodec::encodeEdgeTypeIndexKey({10, 20, 0});
    auto key2 = KeyCodec::encodeEdgeTypeIndexKey({10, 30, 0});
    EXPECT_LT(key1, key2);
}

// ==================== Edge Property Storage (table:eprop_{id}) ====================

TEST_F(KeyCodecTest, EncodeDecodeEPropKey) {
    auto key = KeyCodec::encodeEPropKey(100, 3);

    // No prefix, no edge_label_id: 8 + 2 = 10 bytes
    EXPECT_EQ(key.size(), 10u);

    auto [edge_id, prop_id] = KeyCodec::decodeEPropKey(key);
    EXPECT_EQ(edge_id, 100u);
    EXPECT_EQ(prop_id, 3u);
}

TEST_F(KeyCodecTest, EPropPrefix) {
    auto prefix = KeyCodec::encodeEPropPrefix(500);
    EXPECT_EQ(prefix.size(), 8u);

    auto full_key = KeyCodec::encodeEPropKey(500, 1);
    EXPECT_EQ(full_key.substr(0, 8), prefix);
}

// ==================== Key Ordering / Sorting ====================

TEST_F(KeyCodecTest, EdgeIndexKeysSortByVertex) {
    auto key1 = KeyCodec::encodeEdgeIndexKey({1, Direction::OUT, 1, 1, 0});
    auto key2 = KeyCodec::encodeEdgeIndexKey({2, Direction::OUT, 1, 1, 0});
    EXPECT_LT(key1, key2);
}

TEST_F(KeyCodecTest, EdgeIndexKeysSortByNeighbor) {
    auto key1 = KeyCodec::encodeEdgeIndexKey({1, Direction::OUT, 1, 10, 0});
    auto key2 = KeyCodec::encodeEdgeIndexKey({1, Direction::OUT, 1, 20, 0});
    EXPECT_LT(key1, key2);
}
