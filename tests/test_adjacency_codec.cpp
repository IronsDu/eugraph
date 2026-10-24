#include <gtest/gtest.h>

#include "storage/kv/adjacency_codec.hpp"

using namespace eugraph;

TEST(AdjacencyCodecTest, RoundTripComplete) {
    OutEdgeCache cache;
    cache.state = AdjacencyCacheState::COMPLETE;
    cache.edges.push_back({7, 11, 100, 1000});
    cache.edges.push_back({8, 12, 101, 1001});
    auto blob = AdjacencyCodec::encode(cache);
    EXPECT_EQ(blob.size(), 1 + 2 * AdjacencyCodec::kEntrySize);
    auto decoded = AdjacencyCodec::decode(blob);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->state, AdjacencyCacheState::COMPLETE);
    EXPECT_EQ(decoded->edges, cache.edges);
}

TEST(AdjacencyCodecTest, RoundTripOverflowAndEmpty) {
    OutEdgeCache cache;
    cache.state = AdjacencyCacheState::OVERFLOW;
    auto blob = AdjacencyCodec::encode(cache);
    EXPECT_EQ(blob.size(), AdjacencyCodec::kHeaderSize);
    auto decoded = AdjacencyCodec::decode(blob);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->state, AdjacencyCacheState::OVERFLOW);
    EXPECT_TRUE(decoded->edges.empty());
}

TEST(AdjacencyCodecTest, CountComesFromValueLength) {
    OutEdgeCache cache;
    cache.state = AdjacencyCacheState::COMPLETE;
    for (uint64_t i = 0; i < 4; ++i)
        cache.edges.push_back({1, 100 + i, i, i + 1});
    auto blob = AdjacencyCodec::encode(cache);
    EXPECT_EQ((blob.size() - AdjacencyCodec::kHeaderSize) / AdjacencyCodec::kEntrySize, 4u);
    auto decoded = AdjacencyCodec::decode(blob);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->edges.size(), 4u);
    EXPECT_EQ(decoded->edges[3].neighbor_id, 103u);
}

TEST(AdjacencyCodecTest, RejectsMalformedBlob) {
    EXPECT_FALSE(AdjacencyCodec::decode("").has_value());
    EXPECT_FALSE(AdjacencyCodec::decode(std::string(1, '\x02')).has_value());
    EXPECT_FALSE(AdjacencyCodec::decode(std::string(AdjacencyCodec::kHeaderSize + AdjacencyCodec::kEntrySize - 1, '\0'))
                     .has_value());
}
