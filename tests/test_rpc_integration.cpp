#include <gtest/gtest.h>

#include "compute_service/executor/query_executor.hpp"
#include "metadata_service/metadata_service.hpp"
#include "server/eugraph_handler.hpp"
#include "storage/graph_store_impl.hpp"
#include "gen-cpp2/eugraph_types.h"

#include <filesystem>
#include <folly/coro/BlockingWait.h>

using namespace eugraph;
using namespace eugraph::thrift;
using namespace folly::coro;

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_rpc_test_" + std::to_string(getpid());
}

// Helper to create a PropertyDefThrift
PropertyDefThrift makePropDef(const std::string& name, eugraph::thrift::PropertyType type) {
    PropertyDefThrift pd;
    pd.name() = name;
    pd.type() = type;
    pd.is_required() = false;
    return pd;
}

class RpcIntegrationTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<GraphStoreImpl> store_;
    std::unique_ptr<MetadataServiceImpl> meta_;
    std::unique_ptr<compute::QueryExecutor> executor_;
    std::unique_ptr<server::EuGraphHandler> handler_;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);

        store_ = std::make_unique<GraphStoreImpl>();
        ASSERT_TRUE(store_->open(db_path_));

        meta_ = std::make_unique<MetadataServiceImpl>();
        auto opened = blockingWait(meta_->open(*store_));
        ASSERT_TRUE(opened);

        executor_ = std::make_unique<compute::QueryExecutor>(*store_, *meta_, compute::QueryExecutor::Config{});
        handler_ = std::make_unique<server::EuGraphHandler>(*store_, *meta_, *executor_);
    }

    void TearDown() override {
        handler_.reset();
        executor_.reset();
        blockingWait(meta_->close());
        store_->close();
        std::filesystem::remove_all(db_path_);
    }

    QueryResult execCypher(const std::string& query) {
        QueryResult result;
        handler_->sync_executeCypher(result, std::make_unique<std::string>(query));
        return result;
    }

    LabelInfo createLabel(const std::string& name, std::vector<PropertyDefThrift> props = {}) {
        LabelInfo info;
        handler_->sync_createLabel(info, std::make_unique<std::string>(name),
                                    std::make_unique<std::vector<PropertyDefThrift>>(std::move(props)));
        return info;
    }

    EdgeLabelInfo createEdgeLabel(const std::string& name, std::vector<PropertyDefThrift> props = {}) {
        EdgeLabelInfo info;
        handler_->sync_createEdgeLabel(info, std::make_unique<std::string>(name),
                                        std::make_unique<std::vector<PropertyDefThrift>>(std::move(props)));
        return info;
    }
};

// ==================== DDL Tests ====================

TEST_F(RpcIntegrationTest, CreateAndListLabels) {
    auto info = createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING), makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    EXPECT_EQ(info.id().value(), 1);
    EXPECT_EQ(info.name().value(), "Person");

    std::vector<LabelInfo> labels;
    handler_->sync_listLabels(labels);
    EXPECT_EQ(labels.size(), 1);
}

TEST_F(RpcIntegrationTest, CreateAndListEdgeLabels) {
    auto info = createEdgeLabel("KNOWS");
    EXPECT_EQ(info.id().value(), 1);
    EXPECT_EQ(info.name().value(), "KNOWS");

    std::vector<EdgeLabelInfo> labels;
    handler_->sync_listEdgeLabels(labels);
    EXPECT_EQ(labels.size(), 1);
}

// ==================== Vertex CRUD ====================

TEST_F(RpcIntegrationTest, CreateVerticesWithPropertiesAndQuery) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING), makePropDef("age", eugraph::thrift::PropertyType::INT64)});

    auto r1 = execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})");
    EXPECT_TRUE(r1.error().value().empty()) << "CREATE error: " << r1.error().value();

    auto r2 = execCypher("MATCH (n:Person) RETURN n.name, n.age");
    EXPECT_TRUE(r2.error().value().empty()) << "MATCH error: " << r2.error().value();
    ASSERT_EQ(r2.rows()->size(), 1);

    const auto& row = r2.rows()->at(0);
    ASSERT_EQ(row.values()->size(), 2);
    EXPECT_EQ(row.values()->at(0).getType(), ResultValue::Type::string_val);
    EXPECT_EQ(row.values()->at(0).get_string_val(), "Alice");
    EXPECT_EQ(row.values()->at(1).getType(), ResultValue::Type::int_val);
    EXPECT_EQ(row.values()->at(1).get_int_val(), 30);
}

// ==================== Adjacency Queries ====================

TEST_F(RpcIntegrationTest, CreateEdgeInSingleStatementAndExpand) {
    // Create label and edge label with properties
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING), makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    createEdgeLabel("KNOWS");

    // Create alice->bob in one statement
    auto r1 = execCypher("CREATE (alice:Person {name: \"Alice\"})-[:KNOWS]->(bob:Person {name: \"Bob\"})");
    EXPECT_TRUE(r1.error().value().empty()) << "CREATE edge: " << r1.error().value();

    // Verify 2 persons exist
    auto r2 = execCypher("MATCH (n:Person) RETURN n.name");
    ASSERT_EQ(r2.rows()->size(), 2) << "Should have 2 Person vertices";

    // Expand: find alice's neighbors
    auto r3 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(b) RETURN b.name");
    EXPECT_TRUE(r3.error().value().empty()) << "Expand error: " << r3.error().value();
    ASSERT_EQ(r3.rows()->size(), 1) << "Alice should have 1 KNOWS neighbor";
    EXPECT_EQ(r3.rows()->at(0).values()->at(0).get_string_val(), "Bob");
}

TEST_F(RpcIntegrationTest, FullAdjacencyScenario) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING), makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    createEdgeLabel("KNOWS");

    // Create all vertices and edges in single CREATE statements
    execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})-[:KNOWS]->(bob:Person {name: \"Bob\", age: 25})");
    execCypher("CREATE (bob:Person {name: \"Bob\", age: 25})-[:KNOWS]->(carol:Person {name: \"Carol\", age: 28})");
    execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})-[:KNOWS]->(carol:Person {name: \"Carol\", age: 28})");

    // Verify all persons
    auto r1 = execCypher("MATCH (n:Person) RETURN n.name");
    EXPECT_TRUE(r1.error().value().empty()) << "Scan error: " << r1.error().value();
    // Note: creates duplicate persons (alice, bob, carol created multiple times)
    // This is expected because each CREATE statement is independent
    EXPECT_GE(r1.rows()->size(), 3);

    // Alice's friends (expand from the first alice created)
    auto r2 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(f) RETURN f.name");
    EXPECT_TRUE(r2.error().value().empty()) << "Alice expand error: " << r2.error().value();
    EXPECT_GE(r2.rows()->size(), 1) << "Alice should have at least 1 friend";

    // All relationships
    auto r3 = execCypher("MATCH (a:Person)-[:KNOWS]->(b:Person) RETURN a.name, b.name");
    EXPECT_TRUE(r3.error().value().empty()) << "All edges error: " << r3.error().value();
    EXPECT_GE(r3.rows()->size(), 1) << "Should have at least 1 edge";
}

} // anonymous namespace
