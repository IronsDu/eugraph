#include <gtest/gtest.h>

#include "storage/kv/key_codec.hpp"
#include "common/types/constants.hpp"

using namespace eugraph;

class KeyCodecTest : public ::testing::Test {};

// ==================== Label Reverse Index (L|) ====================

TEST_F(KeyCodecTest, EncodeDecodeLabelReverseKey) {
    auto key = KeyCodec::encodeLabelReverseKey(1, 100);

    // Check prefix byte
    EXPECT_EQ(static_cast<uint8_t>(key[0]), PREFIX_LABEL_REVERSE);

    // Check total length: 1 + 8 + 2 = 11
    EXPECT_EQ(key.size(), 11u);

    // Decode and verify
    auto [vertex_id, label_id] = KeyCodec::decodeLabelReverseKey(key);
    EXPECT_EQ(vertex_id, 1u);
    EXPECT_EQ(label_id, 100u);
}

TEST_F(KeyCodecTest, LabelReverseKeyLargeValues) {
    auto key = KeyCodec::encodeLabelReverseKey(0xFFFFFFFFFFFFFFFF, 0xFFFF);
    auto [vertex_id, label_id] = KeyCodec::decodeLabelReverseKey(key);
    EXPECT_EQ(vertex_id, 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(label_id, 0xFFFF);
}

TEST_F(KeyCodecTest, LabelReversePrefix) {
    auto prefix = KeyCodec::encodeLabelReversePrefix(42);
    EXPECT_EQ(prefix.size(), 9u); // 1 + 8
    EXPECT_EQ(static_cast<uint8_t>(prefix[0]), PREFIX_LABEL_REVERSE);

    // Prefix should be a prefix of the full key
    auto full_key = KeyCodec::encodeLabelReverseKey(42, 5);
    EXPECT_EQ(full_key.substr(0, 9), prefix);
}

// ==================== Label Forward Index (I|) ====================

TEST_F(KeyCodecTest, EncodeDecodeLabelForwardKey) {
    auto key = KeyCodec::encodeLabelForwardKey(3, 999);

    EXPECT_EQ(static_cast<uint8_t>(key[0]), PREFIX_LABEL_FORWARD);
    EXPECT_EQ(key.size(), 11u); // 1 + 2 + 8

    auto [label_id, vertex_id] = KeyCodec::decodeLabelForwardKey(key);
    EXPECT_EQ(label_id, 3u);
    EXPECT_EQ(vertex_id, 999u);
}

TEST_F(KeyCodecTest, LabelForwardPrefix) {
    auto prefix = KeyCodec::encodeLabelForwardPrefix(7);
    EXPECT_EQ(prefix.size(), 3u); // 1 + 2

    auto full_key = KeyCodec::encodeLabelForwardKey(7, 100);
    EXPECT_EQ(full_key.substr(0, 3), prefix);
}

// ==================== Primary Key Forward Index (P|) ====================

TEST_F(KeyCodecTest, EncodePkForwardKey) {
    auto key = KeyCodec::encodePkForwardKey("alice@example.com");
    EXPECT_EQ(static_cast<uint8_t>(key[0]), PREFIX_PK_FORWARD);
    EXPECT_EQ(key.substr(1), "alice@example.com");
}

TEST_F(KeyCodecTest, EncodePkForwardKeyEmpty) {
    auto key = KeyCodec::encodePkForwardKey("");
    EXPECT_EQ(key.size(), 1u);
    EXPECT_EQ(static_cast<uint8_t>(key[0]), PREFIX_PK_FORWARD);
}

// ==================== Primary Key Reverse Index (R|) ====================

TEST_F(KeyCodecTest, EncodePkReverseKey) {
    auto key = KeyCodec::encodePkReverseKey(12345);
    EXPECT_EQ(static_cast<uint8_t>(key[0]), PREFIX_PK_REVERSE);
    EXPECT_EQ(key.size(), 9u); // 1 + 8
}

// ==================== Vertex Property Storage (X|) ====================

TEST_F(KeyCodecTest, EncodeDecodePropertyKey) {
    auto key = KeyCodec::encodePropertyKey(1, 42, 5);

    EXPECT_EQ(static_cast<uint8_t>(key[0]), PREFIX_PROPERTY);
    EXPECT_EQ(key.size(), 13u); // 1 + 2 + 8 + 2

    auto [label_id, vertex_id, prop_id] = KeyCodec::decodePropertyKey(key);
    EXPECT_EQ(label_id, 1u);
    EXPECT_EQ(vertex_id, 42u);
    EXPECT_EQ(prop_id, 5u);
}

TEST_F(KeyCodecTest, PropertyPrefix) {
    auto prefix = KeyCodec::encodePropertyPrefix(2, 100);
    EXPECT_EQ(prefix.size(), 11u); // 1 + 2 + 8

    auto full_key = KeyCodec::encodePropertyKey(2, 100, 3);
    EXPECT_EQ(full_key.substr(0, 11), prefix);
}

// ==================== Edge Index (E|) ====================

TEST_F(KeyCodecTest, EncodeDecodeEdgeIndexKey) {
    KeyCodec::EdgeIndexKey input{1, Direction::OUT, 2, 3, 100};

    auto key = KeyCodec::encodeEdgeIndexKey(input);
    EXPECT_EQ(static_cast<uint8_t>(key[0]), PREFIX_EDGE_INDEX);
    EXPECT_EQ(key.size(), 28u); // 1 + 8 + 1 + 2 + 8 + 8

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
    EXPECT_EQ(prefix.size(), 10u); // 1 + 8 + 1

    auto full_key = KeyCodec::encodeEdgeIndexKey({1, Direction::OUT, 2, 3, 0});
    EXPECT_EQ(full_key.substr(0, 10), prefix);
}

TEST_F(KeyCodecTest, EdgeIndexPrefixWithLabel) {
    auto prefix = KeyCodec::encodeEdgeIndexPrefix(1, Direction::OUT, 5);
    EXPECT_EQ(prefix.size(), 12u); // 1 + 8 + 1 + 2
}

TEST_F(KeyCodecTest, EdgeIndexPrefixWithNeighbor) {
    auto prefix = KeyCodec::encodeEdgeIndexPrefix(1, Direction::OUT, 5, 10);
    EXPECT_EQ(prefix.size(), 20u); // 1 + 8 + 1 + 2 + 8
}

// ==================== Edge Property Storage (D|) ====================

TEST_F(KeyCodecTest, EncodeDecodeEdgePropertyKey) {
    auto key = KeyCodec::encodeEdgePropertyKey(1, 100, 3);

    EXPECT_EQ(static_cast<uint8_t>(key[0]), PREFIX_EDGE_PROPERTY);
    EXPECT_EQ(key.size(), 13u); // 1 + 2 + 8 + 2

    auto [label_id, edge_id, prop_id] = KeyCodec::decodeEdgePropertyKey(key);
    EXPECT_EQ(label_id, 1u);
    EXPECT_EQ(edge_id, 100u);
    EXPECT_EQ(prop_id, 3u);
}

TEST_F(KeyCodecTest, EdgePropertyPrefix) {
    auto prefix = KeyCodec::encodeEdgePropertyPrefix(2, 500);
    EXPECT_EQ(prefix.size(), 11u); // 1 + 2 + 8

    auto full_key = KeyCodec::encodeEdgePropertyKey(2, 500, 1);
    EXPECT_EQ(full_key.substr(0, 11), prefix);
}

// ==================== Key Ordering / Sorting ====================

TEST_F(KeyCodecTest, DifferentPrefixesSortCorrectly) {
    // Verify that different prefixes don't collide
    auto l_key = KeyCodec::encodeLabelReversePrefix(1);
    auto i_key = KeyCodec::encodeLabelForwardPrefix(1);
    auto p_key = KeyCodec::encodePkForwardKey("test");
    auto r_key = KeyCodec::encodePkReverseKey(1);
    auto x_key = KeyCodec::encodePropertyPrefix(1, 1);
    auto e_key = KeyCodec::encodeEdgeIndexPrefix(1, Direction::OUT);
    auto d_key = KeyCodec::encodeEdgePropertyPrefix(1, 1);

    // All prefixes should be different
    EXPECT_NE(l_key[0], i_key[0]);
    EXPECT_NE(p_key[0], r_key[0]);
    EXPECT_NE(x_key[0], e_key[0]);
    EXPECT_NE(x_key[0], d_key[0]);
}

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
