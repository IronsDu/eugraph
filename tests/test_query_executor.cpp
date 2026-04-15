#include <gtest/gtest.h>

#include "compute_service/executor/query_executor.hpp"
#include "metadata_service/metadata_service.hpp"
#include "storage/graph_store_impl.hpp"

#include <filesystem>
#include <folly/coro/BlockingWait.h>

using namespace eugraph;
using namespace eugraph::compute;
using namespace folly::coro;

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_executor_test_" + std::to_string(getpid());
}

class QueryExecutorTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<GraphStoreImpl> store_;
    std::unique_ptr<MetadataServiceImpl> meta_;
    std::unique_ptr<QueryExecutor> executor_;

    LabelId PERSON_LABEL = INVALID_LABEL_ID;
    LabelId CITY_LABEL = INVALID_LABEL_ID;
    EdgeLabelId KNOWS_LABEL = INVALID_EDGE_LABEL_ID;
    EdgeLabelId LIVES_IN_LABEL = INVALID_EDGE_LABEL_ID;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);

        store_ = std::make_unique<GraphStoreImpl>();
        ASSERT_TRUE(store_->open(db_path_));

        meta_ = std::make_unique<MetadataServiceImpl>();
        auto opened = blockingWait(meta_->open(*store_));
        ASSERT_TRUE(opened);

        // Create labels via metadata service
        PERSON_LABEL = blockingWait(meta_->createLabel("Person"));
        CITY_LABEL = blockingWait(meta_->createLabel("City"));
        KNOWS_LABEL = blockingWait(meta_->createEdgeLabel("KNOWS"));
        LIVES_IN_LABEL = blockingWait(meta_->createEdgeLabel("LIVES_IN"));

        ASSERT_NE(PERSON_LABEL, INVALID_LABEL_ID);
        ASSERT_NE(CITY_LABEL, INVALID_LABEL_ID);
        ASSERT_NE(KNOWS_LABEL, INVALID_EDGE_LABEL_ID);
        ASSERT_NE(LIVES_IN_LABEL, INVALID_EDGE_LABEL_ID);

        executor_ = std::make_unique<QueryExecutor>(*store_, *meta_, QueryExecutor::Config{});
    }

    void TearDown() override {
        executor_.reset();
        blockingWait(meta_->close());
        store_->close();
        std::filesystem::remove_all(db_path_);
    }

    // Helper: insert test vertices
    void insertTestVertices() {
        auto txn = store_->beginTransaction();
        for (VertexId vid = 1; vid <= 5; ++vid) {
            std::vector<std::pair<LabelId, Properties>> label_props = {
                {PERSON_LABEL, Properties{PropertyValue(std::string("name") + std::to_string(vid))}}};
            ASSERT_TRUE(store_->insertVertex(txn, vid, label_props, nullptr));
        }
        ASSERT_TRUE(store_->commitTransaction(txn));
    }

    // Helper: insert test edges
    void insertTestEdges() {
        auto txn = store_->beginTransaction();
        ASSERT_TRUE(store_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(store_->insertEdge(txn, 2, 1, 3, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(store_->commitTransaction(txn));
    }
};

} // anonymous namespace

// ==================== Basic Scan Tests ====================

TEST_F(QueryExecutorTest, LabelScanReturnVertex) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, AllNodeScanWithLimit) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n) RETURN n LIMIT 3").rows;
    EXPECT_EQ(rows.size(), 3);
}

// ==================== Expand Test ====================

TEST_F(QueryExecutorTest, ExpandNeighbors) {
    insertTestVertices();
    insertTestEdges();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);
}

// ==================== Create Tests ====================

TEST_F(QueryExecutorTest, CreateNode) {
    auto rows = executor_->executeSync("CREATE (n:Person)").rows;
    EXPECT_EQ(rows.size(), 1);

    auto txn = store_->beginTransaction();
    auto cursor = store_->createVertexScanCursor(txn, PERSON_LABEL);
    int count = 0;
    while (cursor->valid()) {
        ++count;
        cursor->next();
    }
    cursor.reset();
    store_->commitTransaction(txn);
    EXPECT_EQ(count, 1);
}

// ==================== Empty Results ====================

TEST_F(QueryExecutorTest, EmptyScan) {
    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

// ==================== WHERE Filter Tests ====================

TEST_F(QueryExecutorTest, WhereTruePassesAllRows) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereFalseFiltersAllRows) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE false RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereTrueWithLimit) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n LIMIT 2").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, WhereAndTrueTrue) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true AND true RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereAndTrueFalse) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true AND false RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereOrFalseTrue) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE false OR true RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereOrFalseFalse) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE false OR false RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereNotTrue) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE NOT false RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereNotFalse) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE NOT true RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereComplexExpression) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE (true AND true) OR false RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereComplexExpressionFiltersAll) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE (true AND false) OR false RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereOnEmptyData) {
    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

// ==================== AllNodeScan Tests ====================

TEST_F(QueryExecutorTest, AllNodeScanAllVertices) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n) RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, AllNodeScanNoData) {
    auto rows = executor_->executeSync("MATCH (n) RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, AllNodeScanLimitZero) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n) RETURN n LIMIT 0").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, AllNodeScanLimitOne) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n) RETURN n LIMIT 1").rows;
    EXPECT_EQ(rows.size(), 1);
}

// ==================== LabelScan Tests ====================

TEST_F(QueryExecutorTest, LabelScanSpecificLabel) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LabelScanOtherLabelEmpty) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:City) RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, LabelScanWithWhereTrueAndLimit) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n LIMIT 3").rows;
    EXPECT_EQ(rows.size(), 3);
}

TEST_F(QueryExecutorTest, LabelScanMultipleLabelsIndependently) {
    auto txn = store_->beginTransaction();
    for (VertexId vid = 1; vid <= 3; ++vid) {
        std::vector<std::pair<LabelId, Properties>> lp = {{PERSON_LABEL, Properties{}}};
        ASSERT_TRUE(store_->insertVertex(txn, vid, lp, nullptr));
    }
    for (VertexId vid = 10; vid <= 12; ++vid) {
        std::vector<std::pair<LabelId, Properties>> lp = {{CITY_LABEL, Properties{}}};
        ASSERT_TRUE(store_->insertVertex(txn, vid, lp, nullptr));
    }
    ASSERT_TRUE(store_->commitTransaction(txn));

    auto person_rows = executor_->executeSync("MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(person_rows.size(), 3);

    auto city_rows = executor_->executeSync("MATCH (n:City) RETURN n").rows;
    EXPECT_EQ(city_rows.size(), 3);

    auto all_rows = executor_->executeSync("MATCH (n) RETURN n").rows;
    EXPECT_EQ(all_rows.size(), 6);
}

// ==================== Expand Tests ====================

TEST_F(QueryExecutorTest, ExpandOutgoingEdges) {
    insertTestVertices();
    insertTestEdges();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandNoEdgesReturnsEmpty) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, ExpandMultipleEdgesFromSameSource) {
    insertTestVertices();
    auto txn = store_->beginTransaction();
    for (VertexId dst = 2; dst <= 5; ++dst) {
        ASSERT_TRUE(store_->insertEdge(txn, static_cast<EdgeId>(dst), 1, dst, KNOWS_LABEL, 0, {}));
    }
    ASSERT_TRUE(store_->commitTransaction(txn));

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 4);
}

TEST_F(QueryExecutorTest, ExpandWithWhereFilter) {
    insertTestVertices();
    insertTestEdges();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) WHERE true RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandWithWhereFalseFiltersAll) {
    insertTestVertices();
    insertTestEdges();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) WHERE false RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, ExpandWithLimit) {
    insertTestVertices();
    auto txn = store_->beginTransaction();
    for (VertexId dst = 2; dst <= 5; ++dst) {
        ASSERT_TRUE(store_->insertEdge(txn, static_cast<EdgeId>(dst), 1, dst, KNOWS_LABEL, 0, {}));
    }
    ASSERT_TRUE(store_->commitTransaction(txn));

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b LIMIT 2").rows;
    EXPECT_EQ(rows.size(), 2);
}

// ==================== Create Node Tests ====================

TEST_F(QueryExecutorTest, CreateNodeReturnsId) {
    auto rows = executor_->executeSync("CREATE (n:Person)").rows;
    ASSERT_EQ(rows.size(), 1);
    ASSERT_EQ(rows[0].size(), 1);
    ASSERT_TRUE(std::holds_alternative<int64_t>(rows[0][0]));
    // ID comes from metadata service, should be > 0
    EXPECT_GT(std::get<int64_t>(rows[0][0]), 0);
}

TEST_F(QueryExecutorTest, CreateMultipleNodesSequentially) {
    auto rows1 = executor_->executeSync("CREATE (n:Person)").rows;
    ASSERT_EQ(rows1.size(), 1);

    auto rows2 = executor_->executeSync("CREATE (n:Person)").rows;
    ASSERT_EQ(rows2.size(), 1);
    ASSERT_TRUE(std::holds_alternative<int64_t>(rows2[0][0]));
    // Second vertex gets a different ID
    auto id1 = std::get<int64_t>(rows1[0][0]);
    auto id2 = std::get<int64_t>(rows2[0][0]);
    EXPECT_NE(id1, id2);

    // Verify both vertices exist via scan
    auto scan_rows = executor_->executeSync("MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(scan_rows.size(), 2);
}

TEST_F(QueryExecutorTest, CreateNodeVerifyInStore) {
    executor_->executeSync("CREATE (n:Person)");

    auto txn = store_->beginTransaction();
    auto cursor = store_->createVertexScanCursor(txn, PERSON_LABEL);
    int count = 0;
    while (cursor->valid()) {
        ++count;
        cursor->next();
    }
    cursor.reset();
    store_->commitTransaction(txn);
    EXPECT_EQ(count, 1);
}

TEST_F(QueryExecutorTest, CreateNodeDifferentLabels) {
    auto r1 = executor_->executeSync("CREATE (n:Person)").rows;
    auto r2 = executor_->executeSync("CREATE (n:City)").rows;

    ASSERT_EQ(r1.size(), 1);
    ASSERT_EQ(r2.size(), 1);

    auto person_rows = executor_->executeSync("MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(person_rows.size(), 1);

    auto city_rows = executor_->executeSync("MATCH (n:City) RETURN n").rows;
    EXPECT_EQ(city_rows.size(), 1);
}

// ==================== Create Edge Tests ====================

TEST_F(QueryExecutorTest, CreateEdgeReturnsId) {
    auto rows = executor_->executeSync("CREATE (a:Person)-[:KNOWS]->(b:Person)").rows;
    ASSERT_EQ(rows.size(), 1);
    ASSERT_EQ(rows[0].size(), 1);
    ASSERT_TRUE(std::holds_alternative<int64_t>(rows[0][0]));
    EXPECT_GT(std::get<int64_t>(rows[0][0]), 0);
}

TEST_F(QueryExecutorTest, CreateEdgeVerifyInStore) {
    executor_->executeSync("CREATE (a:Person)-[:KNOWS]->(b:Person)");

    auto person_rows = executor_->executeSync("MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(person_rows.size(), 2);

    auto expand_rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(expand_rows.size(), 1);
}

TEST_F(QueryExecutorTest, CreateEdgeThenExpand) {
    executor_->executeSync("CREATE (a:Person)-[:KNOWS]->(b:Person)");

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 1);
}

// ==================== Combined Scenario Tests ====================

TEST_F(QueryExecutorTest, CreateThenScanThenFilter) {
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n").rows;
    EXPECT_EQ(rows.size(), 3);
}

TEST_F(QueryExecutorTest, CreateThenScanWithLimit) {
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n LIMIT 2").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, CreateNodesAndEdgesThenExpand) {
    insertTestVertices();
    insertTestEdges();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);

    auto filtered = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) WHERE true RETURN a, b").rows;
    EXPECT_EQ(filtered.size(), 2);
}

TEST_F(QueryExecutorTest, EmptyGraphAllOperations) {
    EXPECT_EQ(executor_->executeSync("MATCH (n:Person) RETURN n").rows.size(), 0);
    EXPECT_EQ(executor_->executeSync("MATCH (n) RETURN n").rows.size(), 0);
    EXPECT_EQ(executor_->executeSync("MATCH (n:Person) WHERE true RETURN n").rows.size(), 0);
    EXPECT_EQ(executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows.size(), 0);
}

// ==================== Limit Edge Cases ====================

TEST_F(QueryExecutorTest, LimitGreaterThanRowCount) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n LIMIT 100").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LimitExactlyRowCount) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n LIMIT 5").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LimitOne) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n LIMIT 1").rows;
    EXPECT_EQ(rows.size(), 1);
}

// ==================== RETURN * Tests ====================

TEST_F(QueryExecutorTest, ReturnStar) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN *").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, ReturnStarWithLimit) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN * LIMIT 2").rows;
    EXPECT_EQ(rows.size(), 2);
}
