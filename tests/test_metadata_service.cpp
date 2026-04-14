#include <gtest/gtest.h>

#include "metadata_service/metadata_service.hpp"
#include "metadata_service/metadata_codec.hpp"
#include "storage/graph_store_impl.hpp"

#include <filesystem>
#include <folly/coro/BlockingWait.h>

using namespace eugraph;
using namespace folly::coro;

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_meta_test_" + std::to_string(getpid());
}

// Helper to make PropertyDef concisely
PropertyDef prop(const std::string& name, PropertyType type, bool required = false,
                 std::optional<PropertyValue> default_val = std::nullopt) {
    return PropertyDef{0, name, type, required, std::move(default_val)};
}

class MetadataServiceTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<GraphStoreImpl> store_;
    std::unique_ptr<MetadataServiceImpl> meta_;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);

        store_ = std::make_unique<GraphStoreImpl>();
        ASSERT_TRUE(store_->open(db_path_));

        meta_ = std::make_unique<MetadataServiceImpl>();
        auto result = blockingWait(meta_->open(*store_));
        ASSERT_TRUE(result);
    }

    void TearDown() override {
        blockingWait(meta_->close());
        store_->close();
        std::filesystem::remove_all(db_path_);
    }
};

} // anonymous namespace

// ==================== Label CRUD ====================

TEST_F(MetadataServiceTest, CreateAndGetLabel) {
    auto label_id = blockingWait(meta_->createLabel("Person", {
        prop("name", PropertyType::STRING, true),
        prop("age", PropertyType::INT64, false)
    }));
    EXPECT_NE(label_id, INVALID_LABEL_ID);

    auto result = blockingWait(meta_->getLabelId("Person"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, label_id);

    auto name = blockingWait(meta_->getLabelName(label_id));
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "Person");
}

TEST_F(MetadataServiceTest, CreateLabelDuplicateFails) {
    auto id1 = blockingWait(meta_->createLabel("Person"));
    EXPECT_NE(id1, INVALID_LABEL_ID);

    auto id2 = blockingWait(meta_->createLabel("Person"));
    EXPECT_EQ(id2, INVALID_LABEL_ID);
}

TEST_F(MetadataServiceTest, GetLabelNonexistent) {
    auto result = blockingWait(meta_->getLabelId("Nonexistent"));
    EXPECT_FALSE(result.has_value());

    auto name = blockingWait(meta_->getLabelName(9999));
    EXPECT_FALSE(name.has_value());
}

TEST_F(MetadataServiceTest, GetLabelDef) {
    auto label_id = blockingWait(meta_->createLabel("Person", {
        prop("name", PropertyType::STRING, true),
        prop("age", PropertyType::INT64, false)
    }));

    auto def = blockingWait(meta_->getLabelDef("Person"));
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->id, label_id);
    EXPECT_EQ(def->name, "Person");
    ASSERT_EQ(def->properties.size(), 2u);
    EXPECT_EQ(def->properties[0].id, 1);
    EXPECT_EQ(def->properties[0].name, "name");
    EXPECT_EQ(def->properties[0].type, PropertyType::STRING);
    EXPECT_TRUE(def->properties[0].required);
    EXPECT_EQ(def->properties[1].id, 2);
    EXPECT_EQ(def->properties[1].name, "age");
    EXPECT_EQ(def->properties[1].type, PropertyType::INT64);
    EXPECT_FALSE(def->properties[1].required);

    auto def_by_id = blockingWait(meta_->getLabelDefById(label_id));
    ASSERT_TRUE(def_by_id.has_value());
    EXPECT_EQ(def_by_id->name, "Person");
}

TEST_F(MetadataServiceTest, ListLabels) {
    blockingWait(meta_->createLabel("Person"));
    blockingWait(meta_->createLabel("City"));
    blockingWait(meta_->createLabel("Company"));

    auto labels = blockingWait(meta_->listLabels());
    EXPECT_EQ(labels.size(), 3u);

    std::set<std::string> names;
    for (const auto& l : labels)
        names.insert(l.name);
    EXPECT_TRUE(names.count("Person"));
    EXPECT_TRUE(names.count("City"));
    EXPECT_TRUE(names.count("Company"));
}

// ==================== EdgeLabel CRUD ====================

TEST_F(MetadataServiceTest, CreateAndGetEdgeLabel) {
    auto elabel_id = blockingWait(meta_->createEdgeLabel("KNOWS", {
        prop("since", PropertyType::INT64, false)
    }));
    EXPECT_NE(elabel_id, INVALID_EDGE_LABEL_ID);

    auto result = blockingWait(meta_->getEdgeLabelId("KNOWS"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, elabel_id);

    auto name = blockingWait(meta_->getEdgeLabelName(elabel_id));
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "KNOWS");
}

TEST_F(MetadataServiceTest, CreateEdgeLabelDuplicateFails) {
    auto id1 = blockingWait(meta_->createEdgeLabel("KNOWS"));
    EXPECT_NE(id1, INVALID_EDGE_LABEL_ID);

    auto id2 = blockingWait(meta_->createEdgeLabel("KNOWS"));
    EXPECT_EQ(id2, INVALID_EDGE_LABEL_ID);
}

TEST_F(MetadataServiceTest, GetEdgeLabelDef) {
    auto elabel_id = blockingWait(meta_->createEdgeLabel("KNOWS", {
        prop("since", PropertyType::INT64, false),
        prop("weight", PropertyType::DOUBLE, false)
    }));

    auto def = blockingWait(meta_->getEdgeLabelDef("KNOWS"));
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->id, elabel_id);
    EXPECT_EQ(def->name, "KNOWS");
    ASSERT_EQ(def->properties.size(), 2u);
    EXPECT_EQ(def->properties[0].id, 1);
    EXPECT_EQ(def->properties[0].name, "since");
    EXPECT_EQ(def->properties[1].id, 2);
    EXPECT_EQ(def->properties[1].name, "weight");

    auto def_by_id = blockingWait(meta_->getEdgeLabelDefById(elabel_id));
    ASSERT_TRUE(def_by_id.has_value());
    EXPECT_EQ(def_by_id->name, "KNOWS");
}

TEST_F(MetadataServiceTest, ListEdgeLabels) {
    blockingWait(meta_->createEdgeLabel("KNOWS"));
    blockingWait(meta_->createEdgeLabel("LIVES_IN"));

    auto labels = blockingWait(meta_->listEdgeLabels());
    EXPECT_EQ(labels.size(), 2u);
}

// ==================== ID allocation ====================

TEST_F(MetadataServiceTest, IdAllocation) {
    auto vid1 = blockingWait(meta_->nextVertexId());
    auto vid2 = blockingWait(meta_->nextVertexId());
    auto vid3 = blockingWait(meta_->nextVertexId());
    EXPECT_NE(vid1, vid2);
    EXPECT_NE(vid2, vid3);
    EXPECT_LT(vid1, vid2);
    EXPECT_LT(vid2, vid3);

    auto eid1 = blockingWait(meta_->nextEdgeId());
    auto eid2 = blockingWait(meta_->nextEdgeId());
    EXPECT_NE(eid1, eid2);
    EXPECT_LT(eid1, eid2);
}

TEST_F(MetadataServiceTest, IdAllocationMany) {
    std::set<VertexId> vids;
    for (int i = 0; i < 100; ++i) {
        auto vid = blockingWait(meta_->nextVertexId());
        EXPECT_TRUE(vids.insert(vid).second) << "Duplicate VertexId: " << vid;
    }

    std::set<EdgeId> eids;
    for (int i = 0; i < 100; ++i) {
        auto eid = blockingWait(meta_->nextEdgeId());
        EXPECT_TRUE(eids.insert(eid).second) << "Duplicate EdgeId: " << eid;
    }
}

// ==================== Persistence ====================

TEST_F(MetadataServiceTest, PersistenceAcrossRestart) {
    // Create some data
    auto person_id = blockingWait(meta_->createLabel("Person", {
        prop("name", PropertyType::STRING, true),
        prop("age", PropertyType::INT64, false)
    }));
    auto knows_id = blockingWait(meta_->createEdgeLabel("KNOWS", {
        prop("since", PropertyType::INT64, false)
    }));

    // Allocate some IDs
    auto vid = blockingWait(meta_->nextVertexId());
    auto eid = blockingWait(meta_->nextEdgeId());

    // Close
    blockingWait(meta_->close());
    store_->close();

    // Reopen
    store_ = std::make_unique<GraphStoreImpl>();
    ASSERT_TRUE(store_->open(db_path_));
    meta_ = std::make_unique<MetadataServiceImpl>();
    auto opened = blockingWait(meta_->open(*store_));
    ASSERT_TRUE(opened);

    // Verify labels
    auto result = blockingWait(meta_->getLabelId("Person"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, person_id);

    auto def = blockingWait(meta_->getLabelDef("Person"));
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->properties.size(), 2u);
    EXPECT_EQ(def->properties[0].name, "name");
    EXPECT_EQ(def->properties[1].name, "age");

    // Verify edge labels
    auto elabel = blockingWait(meta_->getEdgeLabelId("KNOWS"));
    ASSERT_TRUE(elabel.has_value());
    EXPECT_EQ(*elabel, knows_id);

    // Verify ID counters continued from where they left off
    auto vid2 = blockingWait(meta_->nextVertexId());
    EXPECT_GT(vid2, vid);
    auto eid2 = blockingWait(meta_->nextEdgeId());
    EXPECT_GT(eid2, eid);
}

// ==================== PropertyValue serialization ====================

TEST_F(MetadataServiceTest, PropertyDefWithDefaultValue) {
    auto label_id = blockingWait(meta_->createLabel("Config", {
        prop("key", PropertyType::STRING, true),
        prop("value", PropertyType::STRING, false, PropertyValue(std::string("default"))),
        prop("count", PropertyType::INT64, false, PropertyValue(int64_t(42))),
        prop("enabled", PropertyType::BOOL, false, PropertyValue(true)),
        prop("score", PropertyType::DOUBLE, false, PropertyValue(3.14))
    }));

    auto def = blockingWait(meta_->getLabelDef("Config"));
    ASSERT_TRUE(def.has_value());
    ASSERT_EQ(def->properties.size(), 5u);

    // key — required, no default
    EXPECT_EQ(def->properties[0].name, "key");
    EXPECT_TRUE(def->properties[0].required);
    EXPECT_FALSE(def->properties[0].default_value.has_value());

    // value — string default
    EXPECT_EQ(def->properties[1].name, "value");
    ASSERT_TRUE(def->properties[1].default_value.has_value());
    EXPECT_EQ(std::get<std::string>(*def->properties[1].default_value), "default");

    // count — int64 default
    ASSERT_TRUE(def->properties[2].default_value.has_value());
    EXPECT_EQ(std::get<int64_t>(*def->properties[2].default_value), 42);

    // enabled — bool default
    ASSERT_TRUE(def->properties[3].default_value.has_value());
    EXPECT_EQ(std::get<bool>(*def->properties[3].default_value), true);

    // score — double default
    ASSERT_TRUE(def->properties[4].default_value.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*def->properties[4].default_value), 3.14);
}

// ==================== MetadataCodec unit tests ====================

TEST(MetadataCodecTest, LabelDefRoundTrip) {
    LabelDef original;
    original.id = 5;
    original.name = "TestLabel";
    original.properties = {
        PropertyDef{1, "name", PropertyType::STRING, true, std::nullopt},
        PropertyDef{2, "age", PropertyType::INT64, false, PropertyValue(int64_t(0))},
        PropertyDef{3, "tags", PropertyType::STRING_ARRAY, false, std::nullopt}
    };

    auto encoded = MetadataCodec::encodeLabelDef(original);
    auto decoded = MetadataCodec::decodeLabelDef(encoded);

    EXPECT_EQ(decoded.id, original.id);
    EXPECT_EQ(decoded.name, original.name);
    ASSERT_EQ(decoded.properties.size(), original.properties.size());
    EXPECT_EQ(decoded.properties[0].id, 1);
    EXPECT_EQ(decoded.properties[0].name, "name");
    EXPECT_EQ(decoded.properties[0].type, PropertyType::STRING);
    EXPECT_TRUE(decoded.properties[0].required);

    EXPECT_EQ(decoded.properties[1].name, "age");
    ASSERT_TRUE(decoded.properties[1].default_value.has_value());
    EXPECT_EQ(std::get<int64_t>(*decoded.properties[1].default_value), 0);

    EXPECT_EQ(decoded.properties[2].name, "tags");
    EXPECT_EQ(decoded.properties[2].type, PropertyType::STRING_ARRAY);
}

TEST(MetadataCodecTest, EdgeLabelDefRoundTrip) {
    EdgeLabelDef original;
    original.id = 10;
    original.name = "FOLLOWS";
    original.directed = true;
    original.properties = {
        PropertyDef{1, "since", PropertyType::INT64, false, std::nullopt}
    };

    auto encoded = MetadataCodec::encodeEdgeLabelDef(original);
    auto decoded = MetadataCodec::decodeEdgeLabelDef(encoded);

    EXPECT_EQ(decoded.id, original.id);
    EXPECT_EQ(decoded.name, original.name);
    EXPECT_TRUE(decoded.directed);
    ASSERT_EQ(decoded.properties.size(), 1u);
    EXPECT_EQ(decoded.properties[0].name, "since");
}

TEST(MetadataCodecTest, NextIdsRoundTrip) {
    auto encoded = MetadataCodec::encodeNextIds(100, 200, 5, 3);
    VertexId vid; EdgeId eid; LabelId lid; EdgeLabelId elid;
    MetadataCodec::decodeNextIds(encoded, vid, eid, lid, elid);
    EXPECT_EQ(vid, 100u);
    EXPECT_EQ(eid, 200u);
    EXPECT_EQ(lid, 5u);
    EXPECT_EQ(elid, 3u);
}

TEST(MetadataCodecTest, PropertyValueAllTypes) {
    auto testRoundTrip = [](const PropertyValue& original) {
        std::string buf;
        MetadataCodec::encodePropertyValue(buf, original);
        size_t offset = 0;
        auto decoded = MetadataCodec::decodePropertyValue(buf, offset);
        EXPECT_EQ(original.index(), decoded.index()) << "Type tag mismatch";
    };

    testRoundTrip(PropertyValue(std::monostate{}));
    testRoundTrip(PropertyValue(false));
    testRoundTrip(PropertyValue(true));
    testRoundTrip(PropertyValue(int64_t(12345)));
    testRoundTrip(PropertyValue(int64_t(-98765)));
    testRoundTrip(PropertyValue(3.14159));
    testRoundTrip(PropertyValue(std::string("hello world")));
    testRoundTrip(PropertyValue(std::vector<int64_t>{1, 2, 3}));
    testRoundTrip(PropertyValue(std::vector<double>{1.1, 2.2}));
    testRoundTrip(PropertyValue(std::vector<std::string>{"a", "bb", "ccc"}));
}
