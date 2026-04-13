#include <gtest/gtest.h>

#include "compute_service/executor/query_executor.hpp"
#include "storage/graph_store_impl.hpp"

#include <filesystem>

using namespace eugraph;
using namespace eugraph::compute;

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_executor_test_" + std::to_string(getpid());
}

class QueryExecutorTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<GraphStoreImpl> store_;
    std::unique_ptr<QueryExecutor> executor_;

    // Test label IDs
    static constexpr LabelId PERSON_LABEL = 1;
    static constexpr LabelId CITY_LABEL = 2;
    static constexpr EdgeLabelId KNOWS_LABEL = 1;
    static constexpr EdgeLabelId LIVES_IN_LABEL = 2;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);

        store_ = std::make_unique<GraphStoreImpl>();
        ASSERT_TRUE(store_->open(db_path_));

        // Create labels
        ASSERT_TRUE(store_->createLabel(PERSON_LABEL));
        ASSERT_TRUE(store_->createLabel(CITY_LABEL));
        ASSERT_TRUE(store_->createEdgeLabel(KNOWS_LABEL));
        ASSERT_TRUE(store_->createEdgeLabel(LIVES_IN_LABEL));

        // Create executor with label mappings
        executor_ = std::make_unique<QueryExecutor>(*store_);
        executor_->setLabelMappings({{"Person", PERSON_LABEL}, {"City", CITY_LABEL}},
                                    {{"KNOWS", KNOWS_LABEL}, {"LIVES_IN", LIVES_IN_LABEL}});
    }

    void TearDown() override {
        executor_.reset();
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
        // Person 1 KNOWS Person 2, Person 3
        ASSERT_TRUE(store_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(store_->insertEdge(txn, 2, 1, 3, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(store_->commitTransaction(txn));
    }
};

} // anonymous namespace

// ==================== Basic Scan Tests ====================

TEST_F(QueryExecutorTest, LabelScanReturnVertex) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, AllNodeScanWithLimit) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n) RETURN n LIMIT 3");
    EXPECT_EQ(rows.size(), 3);
}

// ==================== Expand Test ====================

TEST_F(QueryExecutorTest, ExpandNeighbors) {
    insertTestVertices();
    insertTestEdges();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b");
    EXPECT_EQ(rows.size(), 2); // Person 1 → Person 2, Person 1 → Person 3
}

// ==================== Create Tests ====================

TEST_F(QueryExecutorTest, CreateNode) {
    auto rows = executor_->executeSync("CREATE (n:Person)");
    EXPECT_EQ(rows.size(), 1); // Returns the created vertex ID

    // Verify vertex exists
    auto txn = store_->beginTransaction();
    auto cursor = store_->createVertexScanCursor(txn, PERSON_LABEL);
    int count = 0;
    while (cursor->valid()) {
        ++count;
        cursor->next();
    }
    // Cursor must be destroyed before committing (commit closes the WT session)
    cursor.reset();
    store_->commitTransaction(txn);
    EXPECT_EQ(count, 1);
}

// ==================== Empty Results ====================

TEST_F(QueryExecutorTest, EmptyScan) {
    // No vertices exist
    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

// ==================== WHERE Filter Tests ====================

TEST_F(QueryExecutorTest, WhereTruePassesAllRows) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereFalseFiltersAllRows) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE false RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereTrueWithLimit) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n LIMIT 2");
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, WhereAndTrueTrue) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true AND true RETURN n");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereAndTrueFalse) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true AND false RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereOrFalseTrue) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE false OR true RETURN n");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereOrFalseFalse) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE false OR false RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereNotTrue) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE NOT false RETURN n");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereNotFalse) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE NOT true RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereComplexExpression) {
    insertTestVertices();

    // (true AND true) OR false = true
    auto rows = executor_->executeSync("MATCH (n:Person) WHERE (true AND true) OR false RETURN n");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereComplexExpressionFiltersAll) {
    insertTestVertices();

    // (true AND false) OR false = false
    auto rows = executor_->executeSync("MATCH (n:Person) WHERE (true AND false) OR false RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereOnEmptyData) {
    // No data inserted, WHERE true still returns 0
    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

// ==================== AllNodeScan Tests ====================

TEST_F(QueryExecutorTest, AllNodeScanAllVertices) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n) RETURN n");
    // AllNodeScan scans all labels; only PERSON_LABEL has data
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, AllNodeScanNoData) {
    auto rows = executor_->executeSync("MATCH (n) RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, AllNodeScanLimitZero) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n) RETURN n LIMIT 0");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, AllNodeScanLimitOne) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n) RETURN n LIMIT 1");
    EXPECT_EQ(rows.size(), 1);
}

// ==================== LabelScan Tests ====================

TEST_F(QueryExecutorTest, LabelScanSpecificLabel) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LabelScanOtherLabelEmpty) {
    insertTestVertices();

    // No City vertices inserted
    auto rows = executor_->executeSync("MATCH (n:City) RETURN n");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, LabelScanWithWhereTrueAndLimit) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n LIMIT 3");
    EXPECT_EQ(rows.size(), 3);
}

TEST_F(QueryExecutorTest, LabelScanMultipleLabelsIndependently) {
    // Insert Person and City vertices
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

    auto person_rows = executor_->executeSync("MATCH (n:Person) RETURN n");
    EXPECT_EQ(person_rows.size(), 3);

    auto city_rows = executor_->executeSync("MATCH (n:City) RETURN n");
    EXPECT_EQ(city_rows.size(), 3);

    auto all_rows = executor_->executeSync("MATCH (n) RETURN n");
    EXPECT_EQ(all_rows.size(), 6);
}

// ==================== Expand Tests ====================

TEST_F(QueryExecutorTest, ExpandOutgoingEdges) {
    insertTestVertices();
    insertTestEdges();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b");
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandNoEdgesReturnsEmpty) {
    insertTestVertices();
    // No edges inserted

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, ExpandMultipleEdgesFromSameSource) {
    insertTestVertices();
    auto txn = store_->beginTransaction();
    // Person 1 KNOWS Person 2, 3, 4, 5
    for (VertexId dst = 2; dst <= 5; ++dst) {
        ASSERT_TRUE(store_->insertEdge(txn, static_cast<EdgeId>(dst), 1, dst, KNOWS_LABEL, 0, {}));
    }
    ASSERT_TRUE(store_->commitTransaction(txn));

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b");
    EXPECT_EQ(rows.size(), 4);
}

TEST_F(QueryExecutorTest, ExpandWithWhereFilter) {
    insertTestVertices();
    insertTestEdges();

    // WHERE true should pass all expanded rows
    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) WHERE true RETURN a, b");
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandWithWhereFalseFiltersAll) {
    insertTestVertices();
    insertTestEdges();

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) WHERE false RETURN a, b");
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, ExpandWithLimit) {
    insertTestVertices();
    auto txn = store_->beginTransaction();
    for (VertexId dst = 2; dst <= 5; ++dst) {
        ASSERT_TRUE(store_->insertEdge(txn, static_cast<EdgeId>(dst), 1, dst, KNOWS_LABEL, 0, {}));
    }
    ASSERT_TRUE(store_->commitTransaction(txn));

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b LIMIT 2");
    EXPECT_EQ(rows.size(), 2);
}

// ==================== Create Node Tests ====================

TEST_F(QueryExecutorTest, CreateNodeReturnsId) {
    auto rows = executor_->executeSync("CREATE (n:Person)");
    ASSERT_EQ(rows.size(), 1);
    ASSERT_EQ(rows[0].size(), 1);
    // The returned value is the created vertex ID as int64_t
    ASSERT_TRUE(std::holds_alternative<int64_t>(rows[0][0]));
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 1); // first assigned VID
}

TEST_F(QueryExecutorTest, CreateMultipleNodesSequentially) {
    auto rows1 = executor_->executeSync("CREATE (n:Person)");
    ASSERT_EQ(rows1.size(), 1);

    auto rows2 = executor_->executeSync("CREATE (n:Person)");
    ASSERT_EQ(rows2.size(), 1);
    ASSERT_TRUE(std::holds_alternative<int64_t>(rows2[0][0]));
    // Second vertex gets next ID
    EXPECT_EQ(std::get<int64_t>(rows2[0][0]), 2);

    // Verify both vertices exist via scan
    auto scan_rows = executor_->executeSync("MATCH (n:Person) RETURN n");
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
    auto r1 = executor_->executeSync("CREATE (n:Person)");
    auto r2 = executor_->executeSync("CREATE (n:City)");

    ASSERT_EQ(r1.size(), 1);
    ASSERT_EQ(r2.size(), 1);

    auto person_rows = executor_->executeSync("MATCH (n:Person) RETURN n");
    EXPECT_EQ(person_rows.size(), 1);

    auto city_rows = executor_->executeSync("MATCH (n:City) RETURN n");
    EXPECT_EQ(city_rows.size(), 1);
}

// ==================== Create Edge Tests ====================

TEST_F(QueryExecutorTest, CreateEdgeReturnsId) {
    auto rows = executor_->executeSync("CREATE (a:Person)-[:KNOWS]->(b:Person)");
    // CreateEdge is the topmost operator, returns its own row
    ASSERT_EQ(rows.size(), 1);
    ASSERT_EQ(rows[0].size(), 1);
    ASSERT_TRUE(std::holds_alternative<int64_t>(rows[0][0]));
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 1); // first assigned EID
}

TEST_F(QueryExecutorTest, CreateEdgeVerifyInStore) {
    executor_->executeSync("CREATE (a:Person)-[:KNOWS]->(b:Person)");

    // Verify two vertices were created
    auto person_rows = executor_->executeSync("MATCH (n:Person) RETURN n");
    EXPECT_EQ(person_rows.size(), 2);

    // Verify the edge exists by expanding
    auto expand_rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b");
    EXPECT_EQ(expand_rows.size(), 1);
}

TEST_F(QueryExecutorTest, CreateEdgeThenExpand) {
    // Create two vertices and an edge, then verify expand works
    executor_->executeSync("CREATE (a:Person)-[:KNOWS]->(b:Person)");

    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b");
    EXPECT_EQ(rows.size(), 1);
}

// ==================== Combined Scenario Tests ====================

TEST_F(QueryExecutorTest, CreateThenScanThenFilter) {
    // Create 3 vertices, scan all, filter with WHERE true
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");

    auto rows = executor_->executeSync("MATCH (n:Person) WHERE true RETURN n");
    EXPECT_EQ(rows.size(), 3);
}

TEST_F(QueryExecutorTest, CreateThenScanWithLimit) {
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");
    executor_->executeSync("CREATE (n:Person)");

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n LIMIT 2");
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, CreateNodesAndEdgesThenExpand) {
    // Create graph: 1 -> 2, 1 -> 3
    insertTestVertices();
    insertTestEdges();

    // Verify expansion
    auto rows = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b");
    EXPECT_EQ(rows.size(), 2);

    // Filter with WHERE true
    auto filtered = executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) WHERE true RETURN a, b");
    EXPECT_EQ(filtered.size(), 2);
}

TEST_F(QueryExecutorTest, EmptyGraphAllOperations) {
    // Various operations on empty graph should return 0 rows
    EXPECT_EQ(executor_->executeSync("MATCH (n:Person) RETURN n").size(), 0);
    EXPECT_EQ(executor_->executeSync("MATCH (n) RETURN n").size(), 0);
    EXPECT_EQ(executor_->executeSync("MATCH (n:Person) WHERE true RETURN n").size(), 0);
    EXPECT_EQ(executor_->executeSync("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").size(), 0);
}

// ==================== Limit Edge Cases ====================

TEST_F(QueryExecutorTest, LimitGreaterThanRowCount) {
    insertTestVertices(); // 5 vertices

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n LIMIT 100");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LimitExactlyRowCount) {
    insertTestVertices(); // 5 vertices

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n LIMIT 5");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LimitOne) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n LIMIT 1");
    EXPECT_EQ(rows.size(), 1);
}

// ==================== RETURN * Tests ====================

TEST_F(QueryExecutorTest, ReturnStar) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN *");
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, ReturnStarWithLimit) {
    insertTestVertices();

    auto rows = executor_->executeSync("MATCH (n:Person) RETURN * LIMIT 2");
    EXPECT_EQ(rows.size(), 2);
}
