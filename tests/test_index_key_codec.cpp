#include <gtest/gtest.h>

#include "common/types/graph_types.hpp"
#include "storage/kv/index_key_codec.hpp"
#include "storage/meta/meta_codec.hpp"

using namespace eugraph;

// ==================== IndexKeyCodec ====================

TEST(IndexKeyCodec, Int64SortOrder) {
    auto neg100 = IndexKeyCodec::encodeSortableValue(int64_t(-100));
    auto neg1 = IndexKeyCodec::encodeSortableValue(int64_t(-1));
    auto zero = IndexKeyCodec::encodeSortableValue(int64_t(0));
    auto one = IndexKeyCodec::encodeSortableValue(int64_t(1));
    auto hundred = IndexKeyCodec::encodeSortableValue(int64_t(100));

    EXPECT_LT(neg100, neg1);
    EXPECT_LT(neg1, zero);
    EXPECT_LT(zero, one);
    EXPECT_LT(one, hundred);
}

TEST(IndexKeyCodec, DoubleSortOrder) {
    auto neg1 = IndexKeyCodec::encodeSortableValue(-1.0);
    auto zero = IndexKeyCodec::encodeSortableValue(0.0);
    auto half = IndexKeyCodec::encodeSortableValue(0.5);
    auto one = IndexKeyCodec::encodeSortableValue(1.0);
    auto big = IndexKeyCodec::encodeSortableValue(1000.0);

    EXPECT_LT(neg1, zero);
    EXPECT_LT(zero, half);
    EXPECT_LT(half, one);
    EXPECT_LT(one, big);
}

TEST(IndexKeyCodec, StringSortOrder) {
    auto a = IndexKeyCodec::encodeSortableValue(std::string("a"));
    auto b = IndexKeyCodec::encodeSortableValue(std::string("b"));
    auto abc = IndexKeyCodec::encodeSortableValue(std::string("abc"));
    auto abcd = IndexKeyCodec::encodeSortableValue(std::string("abcd"));

    EXPECT_LT(a, b);
    EXPECT_LT(abc, abcd);
    EXPECT_LT(a, abc);
}

TEST(IndexKeyCodec, NullSortsFirst) {
    auto null_val = IndexKeyCodec::encodeSortableValue(std::monostate{});
    auto neg = IndexKeyCodec::encodeSortableValue(int64_t(-100));
    auto s = IndexKeyCodec::encodeSortableValue(std::string(""));

    EXPECT_LT(null_val, neg);
    EXPECT_LT(null_val, s);
}

TEST(IndexKeyCodec, EntityIdRoundTrip) {
    PropertyValue val = int64_t(42);
    for (uint64_t id : {1ULL, 100ULL, 0xFFFFFFFFFFFFFFFFULL, 123456789ULL}) {
        auto key = IndexKeyCodec::encodeIndexKey(val, id);
        auto decoded = IndexKeyCodec::decodeEntityId(key);
        EXPECT_EQ(decoded, id);
    }
}

TEST(IndexKeyCodec, IndexKeyOrdering) {
    auto key1 = IndexKeyCodec::encodeIndexKey(int64_t(10), 1);
    auto key2 = IndexKeyCodec::encodeIndexKey(int64_t(20), 1);
    auto key3 = IndexKeyCodec::encodeIndexKey(int64_t(10), 2);

    // value first, then entity_id
    EXPECT_LT(key1, key2); // 10 < 20
    EXPECT_LT(key1, key3); // same value, id 1 < 2
    EXPECT_LT(key3, key2); // value 10 < 20 overrides id
}

TEST(IndexKeyCodec, EqualityPrefix) {
    auto val = int64_t(42);
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(val);
    auto key = IndexKeyCodec::encodeIndexKey(val, 999);

    // key starts with prefix
    EXPECT_EQ(key.substr(0, prefix.size()), prefix);
}

// ==================== MetadataCodec IndexDef ====================

TEST(MetadataCodec, IndexDefRoundTrip) {
    LabelDef::IndexDef idx;
    idx.name = "idx_person_age";
    idx.prop_ids = {3, 5};
    idx.unique = true;
    idx.state = IndexState::WRITE_ONLY;

    std::string buf;
    MetadataCodec::encodeIndexDef(buf, idx);

    size_t offset = 0;
    auto decoded = MetadataCodec::decodeIndexDef(buf, offset);

    EXPECT_EQ(decoded.name, idx.name);
    EXPECT_EQ(decoded.prop_ids, idx.prop_ids);
    EXPECT_EQ(decoded.unique, idx.unique);
    EXPECT_EQ(decoded.state, idx.state);
}

TEST(MetadataCodec, LabelDefWithIndexes) {
    LabelDef def;
    def.id = 1;
    def.name = "Person";
    def.properties.push_back({0, "name", PropertyType::STRING, false, std::nullopt});
    def.properties.push_back({1, "age", PropertyType::INT64, false, std::nullopt});

    LabelDef::IndexDef idx1;
    idx1.name = "idx_person_age";
    idx1.prop_ids = {1};
    idx1.unique = false;
    idx1.state = IndexState::PUBLIC;
    def.indexes.push_back(idx1);

    LabelDef::IndexDef idx2;
    idx2.name = "idx_person_name";
    idx2.prop_ids = {0};
    idx2.unique = true;
    idx2.state = IndexState::WRITE_ONLY;
    def.indexes.push_back(idx2);

    auto encoded = MetadataCodec::encodeLabelDef(def);
    auto decoded = MetadataCodec::decodeLabelDef(encoded);

    EXPECT_EQ(decoded.id, def.id);
    EXPECT_EQ(decoded.name, def.name);
    EXPECT_EQ(decoded.properties.size(), 2u);
    EXPECT_EQ(decoded.indexes.size(), 2u);
    EXPECT_EQ(decoded.indexes[0].name, "idx_person_age");
    EXPECT_EQ(decoded.indexes[0].state, IndexState::PUBLIC);
    EXPECT_EQ(decoded.indexes[1].name, "idx_person_name");
    EXPECT_EQ(decoded.indexes[1].unique, true);
    EXPECT_EQ(decoded.indexes[1].state, IndexState::WRITE_ONLY);
}

TEST(MetadataCodec, EdgeLabelDefWithIndexes) {
    EdgeLabelDef def;
    def.id = 1;
    def.name = "KNOWS";
    def.properties.push_back({0, "weight", PropertyType::DOUBLE, false, std::nullopt});
    def.directed = true;

    LabelDef::IndexDef idx;
    idx.name = "idx_knows_weight";
    idx.prop_ids = {0};
    idx.unique = false;
    idx.state = IndexState::PUBLIC;
    def.indexes.push_back(idx);

    auto encoded = MetadataCodec::encodeEdgeLabelDef(def);
    auto decoded = MetadataCodec::decodeEdgeLabelDef(encoded);

    EXPECT_EQ(decoded.id, def.id);
    EXPECT_EQ(decoded.name, def.name);
    EXPECT_EQ(decoded.directed, true);
    EXPECT_EQ(decoded.indexes.size(), 1u);
    EXPECT_EQ(decoded.indexes[0].name, "idx_knows_weight");
    EXPECT_EQ(decoded.indexes[0].state, IndexState::PUBLIC);
}
