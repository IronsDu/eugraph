#include <gtest/gtest.h>

#include "bolt/bolt_value_mapping.hpp"
#include "bolt/packstream/decoder.hpp"

using namespace eugraph;
using namespace eugraph::bolt;
using PS = packstream::PackStreamValueStorage;
using PSMap = std::unordered_map<std::string, packstream::PackStreamValueStorage>;
using PSList = std::vector<packstream::PackStreamValueStorage>;

class BoltValueMappingTest : public ::testing::Test {
protected:
    std::unordered_map<LabelId, LabelDef> label_defs_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs_;

    void SetUp() override {
        // Set up a Person label with name and age properties
        LabelDef personDef;
        personDef.id = 1;
        personDef.name = "Person";
        personDef.properties = {
            PropertyDef{0, "name", PropertyType::STRING, false, std::nullopt},
            PropertyDef{1, "age", PropertyType::INT64, false, std::nullopt},
        };
        label_defs_[1] = std::move(personDef);

        // Set up a KNOWS edge label
        EdgeLabelDef knowsDef;
        knowsDef.id = 10;
        knowsDef.name = "KNOWS";
        knowsDef.properties = {
            PropertyDef{0, "since", PropertyType::INT64, false, std::nullopt},
        };
        edge_label_defs_[10] = std::move(knowsDef);
    }
};

// ==================== Null ====================

TEST_F(BoltValueMappingTest, ConvertNull) {
    Value val{std::monostate{}};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

// ==================== Boolean ====================

TEST_F(BoltValueMappingTest, ConvertBoolTrue) {
    Value val{true};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);
    ASSERT_TRUE(std::holds_alternative<bool>(result));
    EXPECT_EQ(std::get<bool>(result), true);
}

TEST_F(BoltValueMappingTest, ConvertBoolFalse) {
    Value val{false};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);
    ASSERT_TRUE(std::holds_alternative<bool>(result));
    EXPECT_EQ(std::get<bool>(result), false);
}

// ==================== Int64 ====================

TEST_F(BoltValueMappingTest, ConvertInt64) {
    Value val{int64_t{42}};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);
    ASSERT_TRUE(std::holds_alternative<int64_t>(result));
    EXPECT_EQ(std::get<int64_t>(result), 42);
}

// ==================== Double ====================

TEST_F(BoltValueMappingTest, ConvertDouble) {
    Value val{3.14};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);
    ASSERT_TRUE(std::holds_alternative<double>(result));
    EXPECT_DOUBLE_EQ(std::get<double>(result), 3.14);
}

// ==================== String ====================

TEST_F(BoltValueMappingTest, ConvertString) {
    Value val{std::string{"hello"}};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    EXPECT_EQ(std::get<std::string>(result), "hello");
}

// ==================== VertexValue → NODE ====================

TEST_F(BoltValueMappingTest, ConvertVertexToNode) {
    VertexValue v;
    v.id = 100;
    v.labels = LabelIdSet{1}; // Person

    // Add properties under label 1
    Properties props(2);
    props[0] = PropertyValue{std::string{"Alice"}};
    props[1] = PropertyValue{int64_t{30}};
    v.properties[1] = std::move(props);

    Value val{std::move(v)};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);

    ASSERT_TRUE(std::holds_alternative<packstream::PackStreamStruct>(result));
    auto& node = std::get<packstream::PackStreamStruct>(result);
    EXPECT_EQ(node.tag, tags::NODE);
    ASSERT_EQ(node.fields.size(), 3u);

    // Field 0: node id
    EXPECT_EQ(std::get<int64_t>(node.fields[0].value), 100);

    // Field 1: label names list
    auto& labels = std::get<PSList>(node.fields[1].value);
    ASSERT_EQ(labels.size(), 1u);
    EXPECT_EQ(std::get<std::string>(labels[0].value), "Person");

    // Field 2: properties dict
    auto& props_dict = std::get<PSMap>(node.fields[2].value);
    EXPECT_EQ(std::get<std::string>(props_dict.at("name").value), "Alice");
    EXPECT_EQ(std::get<int64_t>(props_dict.at("age").value), 30);
}

TEST_F(BoltValueMappingTest, ConvertVertexWithAnonLabel) {
    VertexValue v;
    v.id = 200;
    v.labels = LabelIdSet{INVALID_LABEL_ID}; // anonymous label

    Value val{std::move(v)};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);

    ASSERT_TRUE(std::holds_alternative<packstream::PackStreamStruct>(result));
    auto& node = std::get<packstream::PackStreamStruct>(result);
    EXPECT_EQ(node.tag, tags::NODE);

    // Label list should be empty (anonymous label filtered out)
    auto& labels = std::get<PSList>(node.fields[1].value);
    EXPECT_TRUE(labels.empty());
}

// ==================== EdgeValue → RELATIONSHIP ====================

TEST_F(BoltValueMappingTest, ConvertEdgeToRelationship) {
    EdgeValue e;
    e.id = 500;
    e.src_id = 100;
    e.dst_id = 200;
    e.label_id = 10; // KNOWS
    Properties props(1);
    props[0] = PropertyValue{int64_t{2020}};
    e.properties = std::move(props);

    Value val{std::move(e)};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);

    ASSERT_TRUE(std::holds_alternative<packstream::PackStreamStruct>(result));
    auto& rel = std::get<packstream::PackStreamStruct>(result);
    EXPECT_EQ(rel.tag, tags::RELATIONSHIP);
    ASSERT_EQ(rel.fields.size(), 5u);

    EXPECT_EQ(std::get<int64_t>(rel.fields[0].value), 500);         // id
    EXPECT_EQ(std::get<int64_t>(rel.fields[1].value), 100);         // src
    EXPECT_EQ(std::get<int64_t>(rel.fields[2].value), 200);         // dst
    EXPECT_EQ(std::get<std::string>(rel.fields[3].value), "KNOWS"); // type

    auto& rprops = std::get<PSMap>(rel.fields[4].value);
    EXPECT_EQ(std::get<int64_t>(rprops.at("since").value), 2020);
}

// ==================== ListValue ====================

TEST_F(BoltValueMappingTest, ConvertList) {
    ListValue lv;
    lv.elements.push_back({Value{int64_t{1}}});
    lv.elements.push_back({Value{int64_t{2}}});

    Value val{std::move(lv)};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);

    ASSERT_TRUE(std::holds_alternative<PSList>(result));
    auto& list = std::get<PSList>(result);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(list[0].value), 1);
    EXPECT_EQ(std::get<int64_t>(list[1].value), 2);
}

// ==================== MapValue ====================

TEST_F(BoltValueMappingTest, ConvertMap) {
    MapValue mv;
    mv.entries.push_back({"key1", {Value{std::string{"val1"}}}});
    mv.entries.push_back({"key2", {Value{int64_t{100}}}});

    Value val{std::move(mv)};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);

    ASSERT_TRUE(std::holds_alternative<PSMap>(result));
    auto& dict = std::get<PSMap>(result);
    ASSERT_EQ(dict.size(), 2u);
    EXPECT_EQ(std::get<std::string>(dict.at("key1").value), "val1");
    EXPECT_EQ(std::get<int64_t>(dict.at("key2").value), 100);
}

// ==================== boltParamToValue ====================

TEST_F(BoltValueMappingTest, ParamNullToValue) {
    packstream::Value pv{std::monostate{}};
    auto result = boltParamToValue(pv);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST_F(BoltValueMappingTest, ParamBoolToValue) {
    packstream::Value pv{true};
    auto result = boltParamToValue(pv);
    ASSERT_TRUE(std::holds_alternative<bool>(result));
    EXPECT_EQ(std::get<bool>(result), true);
}

TEST_F(BoltValueMappingTest, ParamIntToValue) {
    packstream::Value pv{int64_t{42}};
    auto result = boltParamToValue(pv);
    ASSERT_TRUE(std::holds_alternative<int64_t>(result));
    EXPECT_EQ(std::get<int64_t>(result), 42);
}

TEST_F(BoltValueMappingTest, ParamDoubleToValue) {
    packstream::Value pv{3.14};
    auto result = boltParamToValue(pv);
    ASSERT_TRUE(std::holds_alternative<double>(result));
    EXPECT_DOUBLE_EQ(std::get<double>(result), 3.14);
}

TEST_F(BoltValueMappingTest, ParamStringToValue) {
    packstream::Value pv{std::string{"test"}};
    auto result = boltParamToValue(pv);
    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    EXPECT_EQ(std::get<std::string>(result), "test");
}

TEST_F(BoltValueMappingTest, ParamListToValue) {
    PSList list;
    list.push_back(PS{int64_t{1}});
    list.push_back(PS{int64_t{2}});
    packstream::Value pv{std::move(list)};
    auto result = boltParamToValue(pv);
    ASSERT_TRUE(std::holds_alternative<ListValue>(result));
    auto& lv = std::get<ListValue>(result);
    ASSERT_EQ(lv.elements.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(lv.elements[0].value), 1);
    EXPECT_EQ(std::get<int64_t>(lv.elements[1].value), 2);
}

TEST_F(BoltValueMappingTest, ParamDictToValue) {
    PSMap dict;
    dict["x"] = PS{int64_t{10}};
    dict["y"] = PS{std::string{"hello"}};
    packstream::Value pv{std::move(dict)};
    auto result = boltParamToValue(pv);
    ASSERT_TRUE(std::holds_alternative<MapValue>(result));
    auto& mv = std::get<MapValue>(result);
    ASSERT_EQ(mv.entries.size(), 2u);
    // Entries are in insertion order
    bool found_x = false, found_y = false;
    for (auto& [key, elem] : mv.entries) {
        if (key == "x") {
            EXPECT_EQ(std::get<int64_t>(elem.value), 10);
            found_x = true;
        }
        if (key == "y") {
            EXPECT_EQ(std::get<std::string>(elem.value), "hello");
            found_y = true;
        }
    }
    EXPECT_TRUE(found_x);
    EXPECT_TRUE(found_y);
}

// ==================== PathValue → PATH ====================

TEST_F(BoltValueMappingTest, ConvertPath) {
    PathValue pv;
    // Path: node 100 → edge 500 → node 200
    VertexValue v1;
    v1.id = 100;
    pv.elements.push_back({Value{std::move(v1)}});

    EdgeValue e1;
    e1.id = 500;
    e1.src_id = 100;
    e1.dst_id = 200;
    e1.label_id = 10;
    pv.elements.push_back({Value{std::move(e1)}});

    VertexValue v2;
    v2.id = 200;
    pv.elements.push_back({Value{std::move(v2)}});

    Value val{std::move(pv)};
    auto result = valueToBolt(val, label_defs_, edge_label_defs_);

    ASSERT_TRUE(std::holds_alternative<packstream::PackStreamStruct>(result));
    auto& path = std::get<packstream::PackStreamStruct>(result);
    EXPECT_EQ(path.tag, tags::PATH);
    ASSERT_EQ(path.fields.size(), 3u);

    // nodes list: 2 nodes
    auto& nodes = std::get<PSList>(path.fields[0].value);
    ASSERT_EQ(nodes.size(), 2u);

    // relationships list: 1 edge
    auto& rels = std::get<PSList>(path.fields[1].value);
    ASSERT_EQ(rels.size(), 1u);

    // sequence: [0, 0] means node[0], rel[0], node[1] should be [0N, 0R, 1N]
    auto& seq = std::get<PSList>(path.fields[2].value);
    ASSERT_EQ(seq.size(), 3u);
    EXPECT_EQ(std::get<int64_t>(seq[0].value), 0); // first node (index into nodes)
    EXPECT_EQ(std::get<int64_t>(seq[1].value), 0); // first rel (index into rels)
    EXPECT_EQ(std::get<int64_t>(seq[2].value), 1); // second node
}
