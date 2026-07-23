#include <gtest/gtest.h>

#include "common/types/graph_types.hpp"
#include "program/server/eugraph_handler.hpp"
#include "query/dataset/row.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/graph_manager.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

#include <filesystem>
#include <string>

using namespace eugraph;
using namespace eugraph::server;

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_vertex_ser_test_" + std::to_string(getpid());
}

PropertyDef makePropDef(uint16_t id, const std::string& name, PropertyType type) {
    PropertyDef pd;
    pd.id = id;
    pd.name = name;
    pd.type = type;
    return pd;
}

class VertexSerializationTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<GraphManager> graph_manager_;
    std::unique_ptr<EuGraphHandler> handler_;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);

        graph_manager_ = std::make_unique<GraphManager>();
        ASSERT_TRUE(graph_manager_->init(db_path_, 1, 1));
        handler_ = std::make_unique<EuGraphHandler>(*graph_manager_);
    }

    void TearDown() override {
        handler_.reset();
        graph_manager_->shutdown();
        graph_manager_.reset();
        std::filesystem::remove_all(db_path_);
    }
};

TEST_F(VertexSerializationTest, OutputsIdNotVid) {
    VertexValue v;
    v.id = 42;
    v.labels = LabelIdSet{1};
    v.properties[1] = Properties{std::string("Alice")};

    LabelDef label_def;
    label_def.id = 1;
    label_def.name = "Person";
    label_def.properties.push_back(makePropDef(0, "name", PropertyType::STRING));

    std::unordered_map<LabelId, LabelDef> label_defs = {{1, label_def}};
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;

    auto result = handler_->valueToThrift(Value{v}, label_defs, edge_label_defs);
    ASSERT_EQ(result.getType(), thrift::ResultValue::Type::vertex_json);

    const std::string& json = result.get_vertex_json();
    EXPECT_NE(json.find("\"id\":42"), std::string::npos) << "JSON should contain \"id\":42, got: " << json;
    EXPECT_EQ(json.find("\"_vid\":"), std::string::npos) << "JSON should NOT contain _vid, got: " << json;
}

TEST_F(VertexSerializationTest, IncludesLabelName) {
    VertexValue v;
    v.id = 1;
    v.labels = LabelIdSet{2};
    v.properties[2] = Properties{std::string("Bob")};

    LabelDef label_def;
    label_def.id = 2;
    label_def.name = "User";
    label_def.properties.push_back(makePropDef(0, "name", PropertyType::STRING));

    std::unordered_map<LabelId, LabelDef> label_defs = {{2, label_def}};
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;

    auto result = handler_->valueToThrift(Value{v}, label_defs, edge_label_defs);
    ASSERT_EQ(result.getType(), thrift::ResultValue::Type::vertex_json);

    const std::string& json = result.get_vertex_json();
    EXPECT_NE(json.find("\"labels\":[\"User\"]"), std::string::npos) << "JSON should contain label name, got: " << json;
}

TEST_F(VertexSerializationTest, SerializesProperties) {
    VertexValue v;
    v.id = 7;
    v.labels = LabelIdSet{1};
    v.properties[1] = Properties{std::string("ABCDEF"), int64_t(30), double(3.14)};

    LabelDef label_def;
    label_def.id = 1;
    label_def.name = "TheLabel";
    label_def.properties.push_back(makePropDef(0, "name", PropertyType::STRING));
    label_def.properties.push_back(makePropDef(1, "age", PropertyType::INT64));
    label_def.properties.push_back(makePropDef(2, "score", PropertyType::DOUBLE));

    std::unordered_map<LabelId, LabelDef> label_defs = {{1, label_def}};
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;

    auto result = handler_->valueToThrift(Value{v}, label_defs, edge_label_defs);
    ASSERT_EQ(result.getType(), thrift::ResultValue::Type::vertex_json);

    const std::string& json = result.get_vertex_json();
    // Properties are serialized as ordered [key, value] pairs (not a JSON object)
    // to preserve insertion order — see eugraph_handler.cpp:411-436.
    EXPECT_NE(json.find("[\"name\",\"ABCDEF\"]"), std::string::npos) << "Should contain name property, got: " << json;
    EXPECT_NE(json.find("[\"age\",30]"), std::string::npos) << "Should contain age property, got: " << json;
}

TEST_F(VertexSerializationTest, VertexWithoutLabels) {
    VertexValue v;
    v.id = 99;

    std::unordered_map<LabelId, LabelDef> label_defs;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;

    auto result = handler_->valueToThrift(Value{v}, label_defs, edge_label_defs);
    ASSERT_EQ(result.getType(), thrift::ResultValue::Type::vertex_json);

    const std::string& json = result.get_vertex_json();
    EXPECT_NE(json.find("\"id\":99"), std::string::npos) << "Should contain id, got: " << json;
    EXPECT_EQ(json.find("\"label\":"), std::string::npos) << "Should NOT contain label when none set, got: " << json;
    EXPECT_EQ(json.find("\"_vid\":"), std::string::npos) << "Should NOT contain _vid, got: " << json;
}

} // namespace
