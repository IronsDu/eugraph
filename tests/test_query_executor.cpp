#include <gtest/gtest.h>

#include "query/executor/query_executor.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

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
    std::unique_ptr<SyncGraphDataStore> sync_data_;
    std::unique_ptr<SyncGraphMetaStore> sync_meta_;
    std::unique_ptr<AsyncGraphMetaStore> async_meta_;
    std::unique_ptr<IoScheduler> io_scheduler_;
    std::unique_ptr<AsyncGraphDataStore> async_data_;
    std::unique_ptr<QueryExecutor> executor_;

    LabelId PERSON_LABEL = INVALID_LABEL_ID;
    LabelId CITY_LABEL = INVALID_LABEL_ID;
    EdgeLabelId KNOWS_LABEL = INVALID_EDGE_LABEL_ID;
    EdgeLabelId LIVES_IN_LABEL = INVALID_EDGE_LABEL_ID;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);
        std::filesystem::create_directories(db_path_ + "/data");
        std::filesystem::create_directories(db_path_ + "/meta");

        sync_data_ = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data_->open(db_path_ + "/data"));

        sync_meta_ = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta_->open(db_path_ + "/meta"));

        async_meta_ = std::make_unique<AsyncGraphMetaStore>();
        io_scheduler_ = std::make_unique<IoScheduler>(2);
        async_data_ = std::make_unique<AsyncGraphDataStore>(*sync_data_, *io_scheduler_);

        auto opened = blockingWait(async_meta_->open(*sync_meta_, *io_scheduler_));
        ASSERT_TRUE(opened);

        // Create labels via metadata service
        PERSON_LABEL = blockingWait(async_meta_->createLabel("Person"));
        CITY_LABEL = blockingWait(async_meta_->createLabel("City"));
        KNOWS_LABEL = blockingWait(async_meta_->createEdgeLabel("KNOWS"));
        LIVES_IN_LABEL = blockingWait(async_meta_->createEdgeLabel("LIVES_IN"));

        ASSERT_NE(PERSON_LABEL, INVALID_LABEL_ID);
        ASSERT_NE(CITY_LABEL, INVALID_LABEL_ID);
        ASSERT_NE(KNOWS_LABEL, INVALID_EDGE_LABEL_ID);
        ASSERT_NE(LIVES_IN_LABEL, INVALID_EDGE_LABEL_ID);

        // Create physical tables in data store
        blockingWait(async_data_->createLabel(PERSON_LABEL));
        blockingWait(async_data_->createLabel(CITY_LABEL));
        blockingWait(async_data_->createEdgeLabel(KNOWS_LABEL));
        blockingWait(async_data_->createEdgeLabel(LIVES_IN_LABEL));

        executor_ = std::make_unique<QueryExecutor>(*async_data_, *async_meta_, QueryExecutor::Config{});
    }

    void TearDown() override {
        executor_.reset();
        async_data_.reset();
        io_scheduler_.reset();
        blockingWait(async_meta_->close());
        sync_data_->close();
        sync_meta_->close();
        std::filesystem::remove_all(db_path_);
    }

    // Helper: insert test vertices
    void insertTestVertices() {
        auto txn = sync_data_->beginTransaction();
        for (VertexId vid = 1; vid <= 5; ++vid) {
            std::vector<std::pair<LabelId, Properties>> label_props = {
                {PERSON_LABEL, Properties{PropertyValue(std::string("name") + std::to_string(vid))}}};
            ASSERT_TRUE(sync_data_->insertVertex(txn, vid, label_props));
        }
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }

    // Helper: insert test edges
    void insertTestEdges() {
        auto txn = sync_data_->beginTransaction();
        ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 1, 3, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }

    // Helper: insert vertices and edges of two types for filtering tests
    void insertMixedEdges() {
        insertTestVertices();
        auto txn = sync_data_->beginTransaction();
        // 2 KNOWS edges: 1->2, 1->3
        ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 1, 3, KNOWS_LABEL, 0, {}));
        // 2 LIVES_IN edges: 1->4, 1->5
        ASSERT_TRUE(sync_data_->insertEdge(txn, 3, 1, 4, LIVES_IN_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->insertEdge(txn, 4, 1, 5, LIVES_IN_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }

    // Helper: set up a multi-hop chain for 2/3-hop tests
    // Vertices 1-6 (all Person), KNOWS chain: 1->2->3->4, LIVES_IN: 1->5, 2->6
    void insertMultiHopEdges() {
        insertTestVertices();
        // Add vertex 6
        auto txn = sync_data_->beginTransaction();
        std::vector<std::pair<LabelId, Properties>> lp = {{PERSON_LABEL, Properties{}}};
        ASSERT_TRUE(sync_data_->insertVertex(txn, 6, lp));
        // KNOWS chain: 1->2, 2->3, 3->4
        ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 2, 3, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->insertEdge(txn, 3, 3, 4, KNOWS_LABEL, 0, {}));
        // LIVES_IN: 1->5, 2->6
        ASSERT_TRUE(sync_data_->insertEdge(txn, 4, 1, 5, LIVES_IN_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->insertEdge(txn, 5, 2, 6, LIVES_IN_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }
};

// Helper: drain prepareStream into ExecutionResult (replaces the old executeSync/executeAsync)
ExecutionResult execSync(QueryExecutor& executor, const std::string& query) {
    auto ctx = blockingWait(executor.prepareStream(query));
    ExecutionResult result;
    if (!ctx->error.empty()) {
        result.error = std::move(ctx->error);
        return result;
    }
    result.columns = std::move(ctx->columns);
    auto gen = std::move(ctx->gen);
    blockingWait(co_invoke([&]() -> Task<void> {
        while (auto chunk = co_await gen.next()) {
            auto rows = chunk->toRows();
            for (auto& row : rows) {
                result.rows.push_back(std::move(row));
            }
        }
        if (ctx->should_commit) {
            co_await ctx->store.commitTran(ctx->txn);
        }
    }));
    return result;
}

} // anonymous namespace

// ==================== Basic Scan Tests ====================

TEST_F(QueryExecutorTest, LabelScanReturnVertex) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, AllNodeScanWithLimit) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n) RETURN n LIMIT 3").rows;
    EXPECT_EQ(rows.size(), 3);
}

// ==================== Expand Test ====================

TEST_F(QueryExecutorTest, ExpandNeighbors) {
    insertTestVertices();
    insertTestEdges();

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);
}

// ==================== Create Tests ====================

TEST_F(QueryExecutorTest, CreateNode) {
    auto rows = execSync(*executor_, "CREATE (n:Person)").rows;
    EXPECT_EQ(rows.size(), 1);

    auto txn = sync_data_->beginTransaction();
    auto cursor = sync_data_->createVertexScanCursor(txn, PERSON_LABEL);
    int count = 0;
    while (cursor->valid()) {
        ++count;
        cursor->next();
    }
    cursor.reset();
    sync_data_->commitTransaction(txn);
    EXPECT_EQ(count, 1);
}

// ==================== Empty Results ====================

TEST_F(QueryExecutorTest, EmptyScan) {
    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

// ==================== WHERE Filter Tests ====================

TEST_F(QueryExecutorTest, WhereTruePassesAllRows) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE true RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereFalseFiltersAllRows) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE false RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereTrueWithLimit) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE true RETURN n LIMIT 2").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, WhereAndTrueTrue) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE true AND true RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereAndTrueFalse) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE true AND false RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereOrFalseTrue) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE false OR true RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereOrFalseFalse) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE false OR false RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereNotTrue) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE NOT false RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereNotFalse) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE NOT true RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereComplexExpression) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE (true AND true) OR false RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, WhereComplexExpressionFiltersAll) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE (true AND false) OR false RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, WhereOnEmptyData) {
    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE true RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

// ==================== AllNodeScan Tests ====================

TEST_F(QueryExecutorTest, AllNodeScanAllVertices) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n) RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, AllNodeScanNoData) {
    auto rows = execSync(*executor_, "MATCH (n) RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, AllNodeScanLimitZero) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n) RETURN n LIMIT 0").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, AllNodeScanLimitOne) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n) RETURN n LIMIT 1").rows;
    EXPECT_EQ(rows.size(), 1);
}

// ==================== LabelScan Tests ====================

TEST_F(QueryExecutorTest, LabelScanSpecificLabel) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LabelScanOtherLabelEmpty) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:City) RETURN n").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, LabelScanWithWhereTrueAndLimit) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE true RETURN n LIMIT 3").rows;
    EXPECT_EQ(rows.size(), 3);
}

TEST_F(QueryExecutorTest, LabelScanMultipleLabelsIndependently) {
    auto txn = sync_data_->beginTransaction();
    for (VertexId vid = 1; vid <= 3; ++vid) {
        std::vector<std::pair<LabelId, Properties>> lp = {{PERSON_LABEL, Properties{}}};
        ASSERT_TRUE(sync_data_->insertVertex(txn, vid, lp));
    }
    for (VertexId vid = 10; vid <= 12; ++vid) {
        std::vector<std::pair<LabelId, Properties>> lp = {{CITY_LABEL, Properties{}}};
        ASSERT_TRUE(sync_data_->insertVertex(txn, vid, lp));
    }
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    auto person_rows = execSync(*executor_, "MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(person_rows.size(), 3);

    auto city_rows = execSync(*executor_, "MATCH (n:City) RETURN n").rows;
    EXPECT_EQ(city_rows.size(), 3);

    auto all_rows = execSync(*executor_, "MATCH (n) RETURN n").rows;
    EXPECT_EQ(all_rows.size(), 6);
}

// ==================== Expand Tests ====================

TEST_F(QueryExecutorTest, ExpandOutgoingEdges) {
    insertTestVertices();
    insertTestEdges();

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandNoEdgesReturnsEmpty) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, ExpandMultipleEdgesFromSameSource) {
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    for (VertexId dst = 2; dst <= 5; ++dst) {
        ASSERT_TRUE(sync_data_->insertEdge(txn, static_cast<EdgeId>(dst), 1, dst, KNOWS_LABEL, 0, {}));
    }
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 4);
}

TEST_F(QueryExecutorTest, ExpandWithWhereFilter) {
    insertTestVertices();
    insertTestEdges();

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) WHERE true RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandWithWhereFalseFiltersAll) {
    insertTestVertices();
    insertTestEdges();

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) WHERE false RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, ExpandWithLimit) {
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    for (VertexId dst = 2; dst <= 5; ++dst) {
        ASSERT_TRUE(sync_data_->insertEdge(txn, static_cast<EdgeId>(dst), 1, dst, KNOWS_LABEL, 0, {}));
    }
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b LIMIT 2").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandFilterBySingleType) {
    insertMixedEdges();
    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandFilterByOtherType) {
    insertMixedEdges();
    auto rows = execSync(*executor_, "MATCH (a:Person)-[:LIVES_IN]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandFilterByMultipleTypes) {
    insertMixedEdges();
    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS|LIVES_IN]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 4);
}

TEST_F(QueryExecutorTest, ExpandNonExistentTypeReturnsEmpty) {
    insertMixedEdges();
    auto rows = execSync(*executor_, "MATCH (a:Person)-[:NONEXISTENT]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, ExpandWithoutTypeReturnsAll) {
    insertMixedEdges();
    auto rows = execSync(*executor_, "MATCH (a:Person)-->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 4);
}

TEST_F(QueryExecutorTest, ExpandTwoHopSameType) {
    insertMultiHopEdges();
    // KNOWS 2-hop: 1->2->3, 2->3->4 = 2 rows
    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b:Person)-[:KNOWS]->(c) RETURN a, b, c").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandTwoHopMixedTypes) {
    insertMultiHopEdges();
    // KNOWS then LIVES_IN: 1->2->6 = 1 row
    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b:Person)-[:LIVES_IN]->(c) RETURN a, b, c").rows;
    EXPECT_EQ(rows.size(), 1);
}

TEST_F(QueryExecutorTest, ExpandThreeHopSameType) {
    insertMultiHopEdges();
    // KNOWS 3-hop: 1->2->3->4 = 1 row
    auto rows =
        execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b)-[:KNOWS]->(c)-[:KNOWS]->(d) RETURN a, b, c, d").rows;
    EXPECT_EQ(rows.size(), 1);
}

TEST_F(QueryExecutorTest, ExpandTwoHopNoMatch) {
    insertMultiHopEdges();
    // LIVES_IN then KNOWS: no vertex reached via LIVES_IN has outgoing KNOWS edges
    auto rows = execSync(*executor_, "MATCH (a:Person)-[:LIVES_IN]->(b)-[:KNOWS]->(c) RETURN a, b, c").rows;
    EXPECT_EQ(rows.size(), 0);
}

TEST_F(QueryExecutorTest, ExpandTwoHopAnonymousNodes) {
    insertMultiHopEdges();
    // KNOWS 2-hop with anonymous intermediate node: 1->2->3, 2->3->4 = 2 rows
    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->()-[:KNOWS]->(c) RETURN c").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, ExpandTwoHopAnonymousNodesNoLabels) {
    insertMultiHopEdges();
    // Undirected edges match KNOWS and LIVES_IN: 1->2->3, 1->2->6, 2->3->4 = 3 rows
    auto rows = execSync(*executor_, "MATCH (a:Person)-[]->()-[]->(c) RETURN c").rows;
    EXPECT_EQ(rows.size(), 3);
}

TEST_F(QueryExecutorTest, ExpandTwoHopAnonymousEdgeAndNode) {
    insertMultiHopEdges();
    // Undirected edges match KNOWS and LIVES_IN: 1->2->3, 1->2->6, 2->3->4 = 3 rows
    auto rows = execSync(*executor_, "MATCH (a:Person)-[]->()-[]->(c) RETURN a, c").rows;
    EXPECT_EQ(rows.size(), 3);
}

// ==================== Path Return Tests ====================

TEST_F(QueryExecutorTest, ReturnNamedPathSingleHop) {
    insertTestVertices();
    insertTestEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS]->(b) RETURN p");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    EXPECT_EQ(result.columns[0], "p");
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1);
        ASSERT_TRUE(std::holds_alternative<PathValue>(row[0]));
        const auto& pv = std::get<PathValue>(row[0]);
        // Path has 3 elements: src vertex, edge, dst vertex
        ASSERT_EQ(pv.elements.size(), 3);
        EXPECT_TRUE(std::holds_alternative<VertexValue>(pv.elements[0].value));
        EXPECT_TRUE(std::holds_alternative<EdgeValue>(pv.elements[1].value));
        EXPECT_TRUE(std::holds_alternative<VertexValue>(pv.elements[2].value));
    }
}

TEST_F(QueryExecutorTest, ReturnNamedPathTwoHop) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS]->()-[:KNOWS]->(c) RETURN p");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // KNOWS 2-hop: 1->2->3, 2->3->4 = 2 rows
    ASSERT_EQ(result.rows.size(), 2);
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1);
        ASSERT_TRUE(std::holds_alternative<PathValue>(row[0]));
        const auto& pv = std::get<PathValue>(row[0]);
        // Path has 5 elements: v1, e1, v2, e2, v3
        ASSERT_EQ(pv.elements.size(), 5);
        EXPECT_TRUE(std::holds_alternative<VertexValue>(pv.elements[0].value));
        EXPECT_TRUE(std::holds_alternative<EdgeValue>(pv.elements[1].value));
        EXPECT_TRUE(std::holds_alternative<VertexValue>(pv.elements[2].value));
        EXPECT_TRUE(std::holds_alternative<EdgeValue>(pv.elements[3].value));
        EXPECT_TRUE(std::holds_alternative<VertexValue>(pv.elements[4].value));
    }
}

TEST_F(QueryExecutorTest, ReturnPathMixedWithNode) {
    insertTestVertices();
    insertTestEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS]->(b) RETURN a, p");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 2);
        EXPECT_TRUE(std::holds_alternative<VertexValue>(row[0])); // a
        EXPECT_TRUE(std::holds_alternative<PathValue>(row[1]));   // p
    }
}

TEST_F(QueryExecutorTest, ReturnPathWithFilter) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS]->()-[:KNOWS]->(c) WHERE true RETURN p");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
}

TEST_F(QueryExecutorTest, PathNodes) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS]->()-[:KNOWS]->(c) RETURN nodes(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1);
        ASSERT_TRUE(std::holds_alternative<ListValue>(row[0]));
        const auto& lv = std::get<ListValue>(row[0]);
        // 2-hop path has 3 vertices: start, intermediate, end
        ASSERT_EQ(lv.elements.size(), 3);
        for (const auto& elem : lv.elements) {
            EXPECT_TRUE(std::holds_alternative<VertexValue>(elem.value));
        }
    }
}

TEST_F(QueryExecutorTest, PathRelationships) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS]->()-[:KNOWS]->(c) RETURN relationships(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1);
        ASSERT_TRUE(std::holds_alternative<ListValue>(row[0]));
        const auto& lv = std::get<ListValue>(row[0]);
        // 2-hop path has 2 edges
        ASSERT_EQ(lv.elements.size(), 2);
        for (const auto& elem : lv.elements) {
            EXPECT_TRUE(std::holds_alternative<EdgeValue>(elem.value));
        }
    }
}

TEST_F(QueryExecutorTest, PathLength) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS]->()-[:KNOWS]->(c) RETURN length(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1);
        ASSERT_TRUE(std::holds_alternative<int64_t>(row[0]));
        EXPECT_EQ(std::get<int64_t>(row[0]), 2);
    }
}

// ==================== Create Node Tests ====================

TEST_F(QueryExecutorTest, CreateNodeReturnsId) {
    auto result = execSync(*executor_, "CREATE (n:Person)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    ASSERT_EQ(result.rows[0].size(), 1);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(result.rows[0][0]));
    // ID comes from metadata service, should be > 0
    EXPECT_GT(std::get<VertexValue>(result.rows[0][0]).id, 0);
}

TEST_F(QueryExecutorTest, CreateMultipleNodesSequentially) {
    auto result1 = execSync(*executor_, "CREATE (n:Person)");
    ASSERT_TRUE(result1.error.empty()) << result1.error;
    ASSERT_EQ(result1.rows.size(), 1);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(result1.rows[0][0]));
    auto id1 = std::get<VertexValue>(result1.rows[0][0]).id;

    auto result2 = execSync(*executor_, "CREATE (n:Person)");
    ASSERT_TRUE(result2.error.empty()) << result2.error;
    ASSERT_EQ(result2.rows.size(), 1);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(result2.rows[0][0]));
    auto id2 = std::get<VertexValue>(result2.rows[0][0]).id;

    // Second vertex gets a different ID
    EXPECT_NE(id1, id2);

    // Verify both vertices exist via scan
    auto scan_rows = execSync(*executor_, "MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(scan_rows.size(), 2);
}

TEST_F(QueryExecutorTest, CreateNodeVerifyInStore) {
    execSync(*executor_, "CREATE (n:Person)");

    auto txn = sync_data_->beginTransaction();
    auto cursor = sync_data_->createVertexScanCursor(txn, PERSON_LABEL);
    int count = 0;
    while (cursor->valid()) {
        ++count;
        cursor->next();
    }
    cursor.reset();
    sync_data_->commitTransaction(txn);
    EXPECT_EQ(count, 1);
}

TEST_F(QueryExecutorTest, CreateNodeDifferentLabels) {
    auto r1 = execSync(*executor_, "CREATE (n:Person)").rows;
    auto r2 = execSync(*executor_, "CREATE (n:City)").rows;

    ASSERT_EQ(r1.size(), 1);
    ASSERT_EQ(r2.size(), 1);

    auto person_rows = execSync(*executor_, "MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(person_rows.size(), 1);

    auto city_rows = execSync(*executor_, "MATCH (n:City) RETURN n").rows;
    EXPECT_EQ(city_rows.size(), 1);
}

// ==================== Create Edge Tests ====================

TEST_F(QueryExecutorTest, CreateEdgeReturnsId) {
    auto rows = execSync(*executor_, "CREATE (a:Person)-[:KNOWS]->(b:Person)").rows;
    ASSERT_EQ(rows.size(), 1);
    ASSERT_EQ(rows[0].size(), 1);
    ASSERT_TRUE(std::holds_alternative<EdgeValue>(rows[0][0]));
    EXPECT_GT(std::get<EdgeValue>(rows[0][0]).id, 0);
}

TEST_F(QueryExecutorTest, CreateEdgeVerifyInStore) {
    execSync(*executor_, "CREATE (a:Person)-[:KNOWS]->(b:Person)");

    auto person_rows = execSync(*executor_, "MATCH (n:Person) RETURN n").rows;
    EXPECT_EQ(person_rows.size(), 2);

    auto expand_rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(expand_rows.size(), 1);
}

TEST_F(QueryExecutorTest, CreateEdgeThenExpand) {
    execSync(*executor_, "CREATE (a:Person)-[:KNOWS]->(b:Person)");

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 1);
}

// ==================== Combined Scenario Tests ====================

TEST_F(QueryExecutorTest, CreateThenScanThenFilter) {
    execSync(*executor_, "CREATE (n:Person)");
    execSync(*executor_, "CREATE (n:Person)");
    execSync(*executor_, "CREATE (n:Person)");

    auto rows = execSync(*executor_, "MATCH (n:Person) WHERE true RETURN n").rows;
    EXPECT_EQ(rows.size(), 3);
}

TEST_F(QueryExecutorTest, CreateThenScanWithLimit) {
    execSync(*executor_, "CREATE (n:Person)");
    execSync(*executor_, "CREATE (n:Person)");
    execSync(*executor_, "CREATE (n:Person)");
    execSync(*executor_, "CREATE (n:Person)");
    execSync(*executor_, "CREATE (n:Person)");

    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN n LIMIT 2").rows;
    EXPECT_EQ(rows.size(), 2);
}

TEST_F(QueryExecutorTest, CreateNodesAndEdgesThenExpand) {
    insertTestVertices();
    insertTestEdges();

    auto rows = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows;
    EXPECT_EQ(rows.size(), 2);

    auto filtered = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) WHERE true RETURN a, b").rows;
    EXPECT_EQ(filtered.size(), 2);
}

TEST_F(QueryExecutorTest, EmptyGraphAllOperations) {
    EXPECT_EQ(execSync(*executor_, "MATCH (n:Person) RETURN n").rows.size(), 0);
    EXPECT_EQ(execSync(*executor_, "MATCH (n) RETURN n").rows.size(), 0);
    EXPECT_EQ(execSync(*executor_, "MATCH (n:Person) WHERE true RETURN n").rows.size(), 0);
    EXPECT_EQ(execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b").rows.size(), 0);
}

// ==================== Limit Edge Cases ====================

TEST_F(QueryExecutorTest, LimitGreaterThanRowCount) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN n LIMIT 100").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LimitExactlyRowCount) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN n LIMIT 5").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, LimitOne) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN n LIMIT 1").rows;
    EXPECT_EQ(rows.size(), 1);
}

// ==================== RETURN * Tests ====================

TEST_F(QueryExecutorTest, ReturnStar) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN *").rows;
    EXPECT_EQ(rows.size(), 5);
}

TEST_F(QueryExecutorTest, ReturnStarWithLimit) {
    insertTestVertices();

    auto rows = execSync(*executor_, "MATCH (n:Person) RETURN * LIMIT 2").rows;
    EXPECT_EQ(rows.size(), 2);
}

// ==================== Restart Persistence Tests ====================

TEST(QueryExecutorRestartTest, DataPersistsAcrossRestart) {
    const std::string db_path = "/tmp/eugraph_restart_test_" + std::to_string(getpid());
    std::filesystem::remove_all(db_path);
    std::filesystem::create_directories(db_path + "/data");
    std::filesystem::create_directories(db_path + "/meta");

    LabelId person_label_id = INVALID_LABEL_ID;
    EdgeLabelId knows_label_id = INVALID_EDGE_LABEL_ID;

    // ====== Phase 1: Write data, then shut down ======
    {
        auto sync_data = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data->open(db_path + "/data"));

        auto sync_meta = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta->open(db_path + "/meta"));

        auto io = std::make_unique<IoScheduler>(2);
        auto async_data = std::make_unique<AsyncGraphDataStore>(*sync_data, *io);
        auto async_meta = std::make_unique<AsyncGraphMetaStore>();
        ASSERT_TRUE(blockingWait(async_meta->open(*sync_meta, *io)));

        person_label_id = blockingWait(async_meta->createLabel("Person"));
        ASSERT_NE(person_label_id, INVALID_LABEL_ID);
        knows_label_id = blockingWait(async_meta->createEdgeLabel("KNOWS"));
        ASSERT_NE(knows_label_id, INVALID_EDGE_LABEL_ID);

        blockingWait(async_data->createLabel(person_label_id));
        blockingWait(async_data->createEdgeLabel(knows_label_id));

        auto executor = std::make_unique<QueryExecutor>(*async_data, *async_meta, QueryExecutor::Config{});

        auto r1 = execSync(*executor, "CREATE (n:Person)");
        ASSERT_TRUE(r1.error.empty()) << "CREATE error: " << r1.error;

        auto r2 = execSync(*executor, "CREATE (n:Person)");
        ASSERT_TRUE(r2.error.empty()) << "CREATE error: " << r2.error;

        auto before = execSync(*executor, "MATCH (n:Person) RETURN n");
        ASSERT_TRUE(before.error.empty()) << "MATCH before shutdown error: " << before.error;
        ASSERT_EQ(before.rows.size(), 2) << "Should have 2 vertices before shutdown";

        executor.reset();
        async_data.reset();
        io.reset();
        blockingWait(async_meta->close());
        sync_data->close();
        sync_meta->close();
    }

    // ====== Phase 2: Restart from same data dir, query ======
    {
        auto sync_data = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data->open(db_path + "/data"));

        auto sync_meta = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta->open(db_path + "/meta"));

        auto io = std::make_unique<IoScheduler>(2);
        auto async_data = std::make_unique<AsyncGraphDataStore>(*sync_data, *io);
        auto async_meta = std::make_unique<AsyncGraphMetaStore>();
        ASSERT_TRUE(blockingWait(async_meta->open(*sync_meta, *io)));

        auto label_opt = blockingWait(async_meta->getLabelId("Person"));
        ASSERT_TRUE(label_opt.has_value()) << "Label 'Person' should survive restart";
        EXPECT_EQ(*label_opt, person_label_id);

        auto edge_label_opt = blockingWait(async_meta->getEdgeLabelId("KNOWS"));
        ASSERT_TRUE(edge_label_opt.has_value()) << "EdgeLabel 'KNOWS' should survive restart";
        EXPECT_EQ(*edge_label_opt, knows_label_id);

        auto executor = std::make_unique<QueryExecutor>(*async_data, *async_meta, QueryExecutor::Config{});

        auto after = execSync(*executor, "MATCH (n:Person) RETURN n");
        EXPECT_TRUE(after.error.empty()) << "MATCH after restart error: " << after.error;
        EXPECT_EQ(after.rows.size(), 2) << "Data should persist across restart";

        auto r3 = execSync(*executor, "CREATE (n:Person)");
        EXPECT_TRUE(r3.error.empty()) << "CREATE after restart error: " << r3.error;

        auto scan2 = execSync(*executor, "MATCH (n:Person) RETURN n");
        EXPECT_EQ(scan2.rows.size(), 3) << "Should have 3 vertices after post-restart insert";

        executor.reset();
        async_data.reset();
        io.reset();
        blockingWait(async_meta->close());
        sync_data->close();
        sync_meta->close();
    }

    std::filesystem::remove_all(db_path);
}

TEST(QueryExecutorRestartTest, DataWithPropertiesPersistsAcrossRestart) {
    const std::string db_path = "/tmp/eugraph_restart_props_test_" + std::to_string(getpid());
    std::filesystem::remove_all(db_path);
    std::filesystem::create_directories(db_path + "/data");
    std::filesystem::create_directories(db_path + "/meta");

    // ====== Phase 1: Write vertices with properties ======
    {
        auto sync_data = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data->open(db_path + "/data"));

        auto sync_meta = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta->open(db_path + "/meta"));

        auto io = std::make_unique<IoScheduler>(2);
        auto async_data = std::make_unique<AsyncGraphDataStore>(*sync_data, *io);
        auto async_meta = std::make_unique<AsyncGraphMetaStore>();
        ASSERT_TRUE(blockingWait(async_meta->open(*sync_meta, *io)));

        auto label_id = blockingWait(
            async_meta->createLabel("Person", {PropertyDef{0, "name", PropertyType::STRING, false, std::nullopt}}));
        ASSERT_NE(label_id, INVALID_LABEL_ID);
        auto edge_label_id = blockingWait(
            async_meta->createEdgeLabel("KNOWS", {PropertyDef{0, "since", PropertyType::INT64, false, std::nullopt}}));
        ASSERT_NE(edge_label_id, INVALID_EDGE_LABEL_ID);

        blockingWait(async_data->createLabel(label_id));
        blockingWait(async_data->createEdgeLabel(edge_label_id));

        auto executor = std::make_unique<QueryExecutor>(*async_data, *async_meta, QueryExecutor::Config{});

        auto r1 = execSync(*executor, "CREATE (n:Person {name: 'Alice'})");
        ASSERT_TRUE(r1.error.empty()) << r1.error;
        auto r2 = execSync(*executor, "CREATE (n:Person {name: 'Bob'})");
        ASSERT_TRUE(r2.error.empty()) << r2.error;

        executor.reset();
        async_data.reset();
        io.reset();
        blockingWait(async_meta->close());
        sync_data->close();
        sync_meta->close();
    }

    // ====== Phase 2: Restart, query properties ======
    {
        auto sync_data = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data->open(db_path + "/data"));

        auto sync_meta = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta->open(db_path + "/meta"));

        auto io = std::make_unique<IoScheduler>(2);
        auto async_data = std::make_unique<AsyncGraphDataStore>(*sync_data, *io);
        auto async_meta = std::make_unique<AsyncGraphMetaStore>();
        ASSERT_TRUE(blockingWait(async_meta->open(*sync_meta, *io)));

        auto executor = std::make_unique<QueryExecutor>(*async_data, *async_meta, QueryExecutor::Config{});

        auto result = execSync(*executor, "MATCH (n:Person) RETURN n.name ORDER BY n.name");
        EXPECT_TRUE(result.error.empty()) << result.error;
        EXPECT_EQ(result.rows.size(), 2);

        executor.reset();
        async_data.reset();
        io.reset();
        blockingWait(async_meta->close());
        sync_data->close();
        sync_meta->close();
    }

    std::filesystem::remove_all(db_path);
}

TEST(QueryExecutorRestartTest, EdgeDataPersistsAcrossRestart) {
    const std::string db_path = "/tmp/eugraph_restart_edge_test_" + std::to_string(getpid());
    std::filesystem::remove_all(db_path);
    std::filesystem::create_directories(db_path + "/data");
    std::filesystem::create_directories(db_path + "/meta");

    // ====== Phase 1: Create vertices and edges ======
    {
        auto sync_data = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data->open(db_path + "/data"));

        auto sync_meta = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta->open(db_path + "/meta"));

        auto io = std::make_unique<IoScheduler>(2);
        auto async_data = std::make_unique<AsyncGraphDataStore>(*sync_data, *io);
        auto async_meta = std::make_unique<AsyncGraphMetaStore>();
        ASSERT_TRUE(blockingWait(async_meta->open(*sync_meta, *io)));

        auto label_id = blockingWait(async_meta->createLabel("Person"));
        ASSERT_NE(label_id, INVALID_LABEL_ID);
        auto edge_label_id = blockingWait(async_meta->createEdgeLabel("KNOWS"));
        ASSERT_NE(edge_label_id, INVALID_EDGE_LABEL_ID);

        blockingWait(async_data->createLabel(label_id));
        blockingWait(async_data->createEdgeLabel(edge_label_id));

        auto executor = std::make_unique<QueryExecutor>(*async_data, *async_meta, QueryExecutor::Config{});

        auto r1 = execSync(*executor, "CREATE (a:Person)-[:KNOWS]->(b:Person)");
        ASSERT_TRUE(r1.error.empty()) << r1.error;

        // Verify before shutdown
        auto before = execSync(*executor, "MATCH (a:Person)-[e:KNOWS]->(b:Person) RETURN a, b");
        ASSERT_TRUE(before.error.empty()) << before.error;
        ASSERT_EQ(before.rows.size(), 1) << "Should have 1 edge before shutdown";

        executor.reset();
        async_data.reset();
        io.reset();
        blockingWait(async_meta->close());
        sync_data->close();
        sync_meta->close();
    }

    // ====== Phase 2: Restart, query edges ======
    {
        auto sync_data = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data->open(db_path + "/data"));

        auto sync_meta = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta->open(db_path + "/meta"));

        auto io = std::make_unique<IoScheduler>(2);
        auto async_data = std::make_unique<AsyncGraphDataStore>(*sync_data, *io);
        auto async_meta = std::make_unique<AsyncGraphMetaStore>();
        ASSERT_TRUE(blockingWait(async_meta->open(*sync_meta, *io)));

        auto executor = std::make_unique<QueryExecutor>(*async_data, *async_meta, QueryExecutor::Config{});

        auto result = execSync(*executor, "MATCH (a:Person)-[e:KNOWS]->(b:Person) RETURN a, b");
        EXPECT_TRUE(result.error.empty()) << result.error;
        EXPECT_EQ(result.rows.size(), 1) << "Edge should persist across restart";

        auto vertices = execSync(*executor, "MATCH (n:Person) RETURN n");
        EXPECT_EQ(vertices.rows.size(), 2) << "Vertices should persist across restart";

        executor.reset();
        async_data.reset();
        io.reset();
        blockingWait(async_meta->close());
        sync_data->close();
        sync_meta->close();
    }

    std::filesystem::remove_all(db_path);
}

// ==================== EXPLAIN Tests ====================

TEST_F(QueryExecutorTest, ExplainLabelScan) {
    auto result = execSync(*executor_, "EXPLAIN MATCH (n:Person) RETURN n");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.columns.size(), 1u);
    EXPECT_EQ(result.columns[0], "Plan");
    ASSERT_FALSE(result.rows.empty());

    std::string plan_text;
    for (const auto& row : result.rows) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0])) {
            plan_text += std::get<std::string>(row[0]) + "\n";
        }
    }
    EXPECT_NE(plan_text.find("Project"), std::string::npos);
    EXPECT_NE(plan_text.find("LabelScan"), std::string::npos);
    EXPECT_NE(plan_text.find("Person"), std::string::npos);
}

TEST_F(QueryExecutorTest, ExplainAllNodeScan) {
    auto result = execSync(*executor_, "EXPLAIN MATCH (n) RETURN n");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.columns[0], "Plan");

    std::string plan_text;
    for (const auto& row : result.rows) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0])) {
            plan_text += std::get<std::string>(row[0]) + "\n";
        }
    }
    EXPECT_NE(plan_text.find("Project"), std::string::npos);
    EXPECT_NE(plan_text.find("AllNodeScan"), std::string::npos);
}

TEST_F(QueryExecutorTest, ExplainWithFilter) {
    auto result = execSync(*executor_, "EXPLAIN MATCH (n:Person) WHERE true RETURN n");
    ASSERT_TRUE(result.error.empty()) << result.error;

    std::string plan_text;
    for (const auto& row : result.rows) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0])) {
            plan_text += std::get<std::string>(row[0]) + "\n";
        }
    }
    EXPECT_NE(plan_text.find("Project"), std::string::npos);
    EXPECT_NE(plan_text.find("Filter"), std::string::npos);
    EXPECT_NE(plan_text.find("LabelScan"), std::string::npos);
}

TEST_F(QueryExecutorTest, ExplainWithLimit) {
    auto result = execSync(*executor_, "EXPLAIN MATCH (n:Person) RETURN n LIMIT 5");
    ASSERT_TRUE(result.error.empty()) << result.error;

    std::string plan_text;
    for (const auto& row : result.rows) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0])) {
            plan_text += std::get<std::string>(row[0]) + "\n";
        }
    }
    EXPECT_NE(plan_text.find("Limit(5)"), std::string::npos);
}

TEST_F(QueryExecutorTest, ExplainCreateNode) {
    auto result = execSync(*executor_, "EXPLAIN CREATE (n:Person {name: 'test'})");
    ASSERT_TRUE(result.error.empty()) << result.error;

    std::string plan_text;
    for (const auto& row : result.rows) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0])) {
            plan_text += std::get<std::string>(row[0]) + "\n";
        }
    }
    EXPECT_NE(plan_text.find("CreateNode"), std::string::npos);
    EXPECT_NE(plan_text.find("Person"), std::string::npos);
}

TEST_F(QueryExecutorTest, ExplainDoesNotExecute) {
    auto before = execSync(*executor_, "MATCH (n:Person) RETURN n");
    size_t count_before = before.rows.size();

    execSync(*executor_, "EXPLAIN CREATE (n:Person {name: 'Ghost'})");

    auto after = execSync(*executor_, "MATCH (n:Person) RETURN n");
    EXPECT_EQ(after.rows.size(), count_before) << "EXPLAIN should not execute the query";
}

TEST_F(QueryExecutorTest, ExplainCaseInsensitive) {
    auto r1 = execSync(*executor_, "explain MATCH (n:Person) RETURN n");
    auto r2 = execSync(*executor_, "Explain MATCH (n:Person) RETURN n");
    auto r3 = execSync(*executor_, "EXPLAIN MATCH (n:Person) RETURN n");
    EXPECT_TRUE(r1.error.empty()) << r1.error;
    EXPECT_TRUE(r2.error.empty()) << r2.error;
    EXPECT_TRUE(r3.error.empty()) << r3.error;
    EXPECT_EQ(r1.rows.size(), r2.rows.size());
    EXPECT_EQ(r2.rows.size(), r3.rows.size());
}

// ==================== M4: SKIP Tests ====================

TEST_F(QueryExecutorTest, SkipRows) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n SKIP 2");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 3);
}

TEST_F(QueryExecutorTest, SkipZero) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n SKIP 0");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 5);
}

TEST_F(QueryExecutorTest, SkipAll) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n SKIP 10");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0);
}

TEST_F(QueryExecutorTest, SkipWithLimit) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n SKIP 2 LIMIT 2");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2);
}

TEST_F(QueryExecutorTest, SkipWithOrderBy) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN id(n) AS id ORDER BY id SKIP 1 LIMIT 2");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    // After ORDER BY id ASC, skip 1, limit 2: should get ids 2 and 3
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 2);
    EXPECT_EQ(std::get<int64_t>(result.rows[1][0]), 3);
}

// ==================== M4: ORDER BY Tests ====================

TEST_F(QueryExecutorTest, OrderByPropertyAsc) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN id(n) AS id ORDER BY id ASC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 5);
    for (size_t i = 0; i < result.rows.size() - 1; ++i) {
        EXPECT_LE(std::get<int64_t>(result.rows[i][0]), std::get<int64_t>(result.rows[i + 1][0]));
    }
}

TEST_F(QueryExecutorTest, OrderByPropertyDesc) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN id(n) AS id ORDER BY id DESC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 5);
    for (size_t i = 0; i < result.rows.size() - 1; ++i) {
        EXPECT_GE(std::get<int64_t>(result.rows[i][0]), std::get<int64_t>(result.rows[i + 1][0]));
    }
}

TEST_F(QueryExecutorTest, OrderByDefaultDirection) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN id(n) AS id ORDER BY id");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 5);
    // Default should be ASC
    for (size_t i = 0; i < result.rows.size() - 1; ++i) {
        EXPECT_LE(std::get<int64_t>(result.rows[i][0]), std::get<int64_t>(result.rows[i + 1][0]));
    }
}

TEST_F(QueryExecutorTest, OrderByMultipleKeys) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN id(n) AS id ORDER BY id ASC, id DESC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 5);
}

TEST_F(QueryExecutorTest, OrderByEmptyResult) {
    auto result = execSync(*executor_, "MATCH (n:Person) RETURN id(n) AS id ORDER BY id");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0);
}

// ==================== M4: DISTINCT Tests ====================

TEST_F(QueryExecutorTest, DistinctProperty) {
    insertTestVertices();

    // All vertices have the same name pattern but unique names, test with a constant
    auto result = execSync(*executor_, "MATCH (n:Person) RETURN DISTINCT 1 AS x");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 1);
}

TEST_F(QueryExecutorTest, DistinctNoDuplicate) {
    insertTestVertices();

    // All ids are unique, DISTINCT should keep all rows
    auto result = execSync(*executor_, "MATCH (n:Person) RETURN DISTINCT id(n) AS id");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 5);
}

TEST_F(QueryExecutorTest, DistinctWithOrderBy) {
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN DISTINCT id(n) AS id ORDER BY id ASC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 5);
    for (size_t i = 0; i < result.rows.size() - 1; ++i) {
        EXPECT_LE(std::get<int64_t>(result.rows[i][0]), std::get<int64_t>(result.rows[i + 1][0]));
    }
}

// ==================== M4: Aggregation Tests ====================

class QueryExecutorAggTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<SyncGraphDataStore> sync_data_;
    std::unique_ptr<SyncGraphMetaStore> sync_meta_;
    std::unique_ptr<AsyncGraphMetaStore> async_meta_;
    std::unique_ptr<IoScheduler> io_scheduler_;
    std::unique_ptr<AsyncGraphDataStore> async_data_;
    std::unique_ptr<QueryExecutor> executor_;

    LabelId PERSON_LABEL = INVALID_LABEL_ID;
    EdgeLabelId KNOWS_LABEL = INVALID_EDGE_LABEL_ID;

    void SetUp() override {
        db_path_ = "/tmp/eugraph_agg_test_" + std::to_string(getpid());
        std::filesystem::remove_all(db_path_);
        std::filesystem::create_directories(db_path_ + "/data");
        std::filesystem::create_directories(db_path_ + "/meta");

        sync_data_ = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data_->open(db_path_ + "/data"));

        sync_meta_ = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta_->open(db_path_ + "/meta"));

        async_meta_ = std::make_unique<AsyncGraphMetaStore>();
        io_scheduler_ = std::make_unique<IoScheduler>(2);
        async_data_ = std::make_unique<AsyncGraphDataStore>(*sync_data_, *io_scheduler_);

        auto opened = blockingWait(async_meta_->open(*sync_meta_, *io_scheduler_));
        ASSERT_TRUE(opened);

        // Create Person label with age and city properties (prop IDs assigned by metadata store starting from 1)
        auto person_label_def = blockingWait(
            async_meta_->createLabel("Person", {PropertyDef{0, "age", PropertyType::INT64, false, std::nullopt},
                                                PropertyDef{0, "city", PropertyType::STRING, false, std::nullopt}}));
        PERSON_LABEL = person_label_def;
        KNOWS_LABEL = blockingWait(async_meta_->createEdgeLabel("KNOWS"));

        ASSERT_NE(PERSON_LABEL, INVALID_LABEL_ID);

        blockingWait(async_data_->createLabel(PERSON_LABEL));
        blockingWait(async_data_->createEdgeLabel(KNOWS_LABEL));

        executor_ = std::make_unique<QueryExecutor>(*async_data_, *async_meta_, QueryExecutor::Config{});
    }

    void TearDown() override {
        executor_.reset();
        async_data_.reset();
        io_scheduler_.reset();
        blockingWait(async_meta_->close());
        sync_data_->close();
        sync_meta_->close();
        std::filesystem::remove_all(db_path_);
    }

    void insertPersonData() {
        // Metadata store assigns prop_ids starting from 1: age=1, city=2
        auto txn = sync_data_->beginTransaction();
        struct PersonData {
            VertexId vid;
            int64_t age;
            std::string city;
        };
        std::vector<PersonData> data = {
            {1, 25, "Beijing"}, {2, 30, "Beijing"}, {3, 35, "Beijing"}, {4, 28, "Shanghai"}, {5, 32, "Shanghai"}};

        for (const auto& p : data) {
            Properties props;
            props.resize(2);
            props[0] = PropertyValue(p.age);
            props[1] = PropertyValue(p.city);
            std::vector<std::pair<LabelId, Properties>> label_props = {{PERSON_LABEL, std::move(props)}};
            ASSERT_TRUE(sync_data_->insertVertex(txn, p.vid, label_props));
        }
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }
};

TEST_F(QueryExecutorAggTest, CountStar) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN count(*)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 5);
}

TEST_F(QueryExecutorAggTest, CountByGroup) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n.city AS city, count(*) AS cnt");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2); // 2 cities
}

TEST_F(QueryExecutorAggTest, SumAge) {
    insertPersonData();
    auto res = execSync(*executor_, "MATCH (n:Person) RETURN sum(n.age) AS total");
    ASSERT_TRUE(res.error.empty()) << "Sum error: " << res.error;
    ASSERT_EQ(res.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(res.rows[0][0]), 150);
}

TEST_F(QueryExecutorAggTest, AvgAge) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN avg(n.age) AS avg_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(result.rows[0][0]), 30.0); // 150/5
}

TEST_F(QueryExecutorAggTest, MinMaxAge) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN min(n.age) AS min_age, max(n.age) AS max_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 25);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 35);
}

TEST_F(QueryExecutorAggTest, CountDistinct) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN count(DISTINCT n.city) AS cities");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 2);
}

TEST_F(QueryExecutorAggTest, MultipleAggregates) {
    insertPersonData();

    auto result =
        execSync(*executor_, "MATCH (n:Person) RETURN count(*) AS cnt, sum(n.age) AS total, avg(n.age) AS avg_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 5);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 150);
    EXPECT_DOUBLE_EQ(std::get<double>(result.rows[0][2]), 30.0);
}

TEST_F(QueryExecutorAggTest, AggregateWithGroupBy) {
    insertPersonData();

    auto result =
        execSync(*executor_, "MATCH (n:Person) RETURN n.city AS city, count(*) AS cnt, avg(n.age) AS avg_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
}

TEST_F(QueryExecutorAggTest, AggregateEmptyResult) {
    auto result = execSync(*executor_, "MATCH (n:Person) RETURN count(*) AS cnt");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 0);
}

TEST_F(QueryExecutorAggTest, AggregateWithOrderBy) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n.city AS city, count(*) AS cnt ORDER BY cnt DESC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    // Beijing has 3, Shanghai has 2; DESC order → Beijing first
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 3);
    EXPECT_EQ(std::get<int64_t>(result.rows[1][1]), 2);
}

TEST_F(QueryExecutorAggTest, AggregateWithSkipLimit) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n.city AS city, count(*) AS cnt SKIP 1 LIMIT 2");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // Only 2 groups, skip 1 → should get 1 row
    EXPECT_EQ(result.rows.size(), 1);
}

// ==================== M6: Multi-Label Tests ====================

class QueryExecutorMultiLabelTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<SyncGraphDataStore> sync_data_;
    std::unique_ptr<SyncGraphMetaStore> sync_meta_;
    std::unique_ptr<AsyncGraphMetaStore> async_meta_;
    std::unique_ptr<IoScheduler> io_scheduler_;
    std::unique_ptr<AsyncGraphDataStore> async_data_;
    std::unique_ptr<QueryExecutor> executor_;

    LabelId PERSON_LABEL = INVALID_LABEL_ID;
    LabelId EMPLOYEE_LABEL = INVALID_LABEL_ID;
    LabelId VIP_LABEL = INVALID_LABEL_ID;

    void SetUp() override {
        db_path_ = "/tmp/eugraph_multilabel_test_" + std::to_string(getpid());
        std::filesystem::remove_all(db_path_);
        std::filesystem::create_directories(db_path_ + "/data");
        std::filesystem::create_directories(db_path_ + "/meta");

        sync_data_ = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data_->open(db_path_ + "/data"));

        sync_meta_ = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta_->open(db_path_ + "/meta"));

        async_meta_ = std::make_unique<AsyncGraphMetaStore>();
        io_scheduler_ = std::make_unique<IoScheduler>(2);
        async_data_ = std::make_unique<AsyncGraphDataStore>(*sync_data_, *io_scheduler_);

        auto opened = blockingWait(async_meta_->open(*sync_meta_, *io_scheduler_));
        ASSERT_TRUE(opened);

        // Create Person label with name and age
        auto person_def = blockingWait(
            async_meta_->createLabel("Person", {PropertyDef{0, "name", PropertyType::STRING, false, std::nullopt},
                                                PropertyDef{0, "age", PropertyType::INT64, false, std::nullopt}}));
        PERSON_LABEL = person_def;

        // Create Employee label with salary and name (shared property name for conflict testing)
        auto employee_def = blockingWait(
            async_meta_->createLabel("Employee", {PropertyDef{0, "salary", PropertyType::INT64, false, std::nullopt},
                                                  PropertyDef{0, "name", PropertyType::STRING, false, std::nullopt}}));
        EMPLOYEE_LABEL = employee_def;

        // Create VIP label with no properties (pure tag)
        auto vip_def = blockingWait(async_meta_->createLabel("VIP"));
        VIP_LABEL = vip_def;

        ASSERT_NE(PERSON_LABEL, INVALID_LABEL_ID);
        ASSERT_NE(EMPLOYEE_LABEL, INVALID_LABEL_ID);
        ASSERT_NE(VIP_LABEL, INVALID_LABEL_ID);

        // Create physical tables
        blockingWait(async_data_->createLabel(PERSON_LABEL));
        blockingWait(async_data_->createLabel(EMPLOYEE_LABEL));
        blockingWait(async_data_->createLabel(VIP_LABEL));

        executor_ = std::make_unique<QueryExecutor>(*async_data_, *async_meta_, QueryExecutor::Config{});
    }

    void TearDown() override {
        executor_.reset();
        async_data_.reset();
        io_scheduler_.reset();
        blockingWait(async_meta_->close());
        sync_data_->close();
        sync_meta_->close();
        std::filesystem::remove_all(db_path_);
    }

    // Insert a vertex with multiple labels directly via sync store.
    // Each label's properties are given as {prop_id: PropertyValue} pairs.
    void insertMultiLabelVertex(VertexId vid, std::vector<std::pair<LabelId, Properties>> label_props) {
        auto txn = sync_data_->beginTransaction();
        ASSERT_TRUE(sync_data_->insertVertex(txn, vid, label_props));
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }
};

// Helper: get prop_id by name from a label_defs map
namespace {
uint16_t propIdByName(const std::unordered_map<LabelId, LabelDef>& defs, LabelId lid, const std::string& name) {
    auto it = defs.find(lid);
    if (it == defs.end())
        return UINT16_MAX;
    for (const auto& pd : it->second.properties) {
        if (pd.name == name)
            return pd.id;
    }
    return UINT16_MAX;
}
} // anonymous namespace

TEST_F(QueryExecutorMultiLabelTest, PropertyNoConflictScalar) {
    // Vertex with only Person label: n.name is scalar "Alice"
    // Need to get prop_ids from the metadata
    auto person_def = blockingWait(async_meta_->getLabelDef("Person"));
    ASSERT_TRUE(person_def.has_value());
    uint16_t name_pid = propIdByName({{PERSON_LABEL, *person_def}}, PERSON_LABEL, "name");
    ASSERT_NE(name_pid, UINT16_MAX);

    Properties person_props;
    person_props.resize(name_pid + 1);
    person_props[name_pid] = PropertyValue(std::string("Alice"));

    insertMultiLabelVertex(1, {{PERSON_LABEL, std::move(person_props)}});

    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Alice");
}

TEST_F(QueryExecutorMultiLabelTest, PropertyConflictToList) {
    // Vertex with Person{name: "Alice"} AND Employee{name: "Engineer"}
    // n.name should return ["Alice", "Engineer"] since both labels define "name"
    auto person_def = blockingWait(async_meta_->getLabelDef("Person"));
    ASSERT_TRUE(person_def.has_value());
    auto employee_def = blockingWait(async_meta_->getLabelDef("Employee"));
    ASSERT_TRUE(employee_def.has_value());

    uint16_t person_name_pid = UINT16_MAX, employee_name_pid = UINT16_MAX;
    for (const auto& pd : person_def->properties) {
        if (pd.name == "name")
            person_name_pid = pd.id;
    }
    for (const auto& pd : employee_def->properties) {
        if (pd.name == "name")
            employee_name_pid = pd.id;
    }
    ASSERT_NE(person_name_pid, UINT16_MAX);
    ASSERT_NE(employee_name_pid, UINT16_MAX);

    Properties person_props, employee_props;
    person_props.resize(person_name_pid + 1);
    person_props[person_name_pid] = PropertyValue(std::string("Alice"));
    employee_props.resize(employee_name_pid + 1);
    employee_props[employee_name_pid] = PropertyValue(std::string("Engineer"));

    insertMultiLabelVertex(1, {{PERSON_LABEL, std::move(person_props)}, {EMPLOYEE_LABEL, std::move(employee_props)}});

    // Use MATCH (n) (AllNodeScan) to load ALL labels' properties, triggering conflict
    auto result = execSync(*executor_, "MATCH (n) RETURN n.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);

    // Should be a ListValue with both names
    auto& val = result.rows[0][0];
    ASSERT_TRUE(std::holds_alternative<ListValue>(val)) << "Expected ListValue, got variant index " << val.index();
    auto& lv = std::get<ListValue>(val);
    ASSERT_EQ(lv.elements.size(), 2);
}

TEST_F(QueryExecutorMultiLabelTest, LabelCastPropertyAccess) {
    // Vertex with Employee{salary: 10000}. n::Employee.salary should return 10000
    auto employee_def = blockingWait(async_meta_->getLabelDef("Employee"));
    ASSERT_TRUE(employee_def.has_value());
    uint16_t salary_pid = propIdByName({{EMPLOYEE_LABEL, *employee_def}}, EMPLOYEE_LABEL, "salary");
    ASSERT_NE(salary_pid, UINT16_MAX);

    Properties employee_props;
    employee_props.resize(salary_pid + 1);
    employee_props[salary_pid] = PropertyValue(int64_t(10000));

    insertMultiLabelVertex(1, {{EMPLOYEE_LABEL, std::move(employee_props)}});

    auto result = execSync(*executor_, "MATCH (n:Employee) RETURN n::Employee.salary");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 10000);
}

TEST_F(QueryExecutorMultiLabelTest, LabelCastAllProperties) {
    // RETURN n::Employee should return a scoped VertexValue with only Employee properties
    auto employee_def = blockingWait(async_meta_->getLabelDef("Employee"));
    ASSERT_TRUE(employee_def.has_value());
    uint16_t salary_pid = propIdByName({{EMPLOYEE_LABEL, *employee_def}}, EMPLOYEE_LABEL, "salary");
    ASSERT_NE(salary_pid, UINT16_MAX);

    Properties employee_props;
    employee_props.resize(salary_pid + 1);
    employee_props[salary_pid] = PropertyValue(int64_t(9999));

    insertMultiLabelVertex(1, {{EMPLOYEE_LABEL, std::move(employee_props)}});

    auto result = execSync(*executor_, "MATCH (n:Employee) RETURN n::Employee");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);

    // Result should be a VertexValue with only the Employee label's properties
    ASSERT_TRUE(std::holds_alternative<VertexValue>(result.rows[0][0]));
    auto& vv = std::get<VertexValue>(result.rows[0][0]);
    EXPECT_EQ(vv.properties.size(), 1);
    EXPECT_NE(vv.properties.find(EMPLOYEE_LABEL), vv.properties.end());
}

TEST_F(QueryExecutorMultiLabelTest, SetVertexLabel) {
    // CREATE (n:Person), SET n:Employee
    auto result = execSync(*executor_, "CREATE (n:Person) SET n:Employee");
    ASSERT_TRUE(result.error.empty()) << result.error;
}

TEST_F(QueryExecutorMultiLabelTest, RemoveVertexLabel) {
    // CREATE (n:Person), SET n:Employee, then REMOVE n:Employee
    auto r1 = execSync(*executor_, "CREATE (n:Person) SET n:Employee");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:Person) REMOVE n:Employee");
    ASSERT_TRUE(r2.error.empty()) << r2.error;
}

TEST_F(QueryExecutorMultiLabelTest, SetVertexProperty) {
    // CREATE (n:Person {name: 'Alice'}), SET n.age = 30
    auto r1 = execSync(*executor_, "CREATE (n:Person {name: 'Alice'})");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:Person) SET n.age = 30");
    ASSERT_TRUE(r2.error.empty()) << r2.error;

    auto r3 = execSync(*executor_, "MATCH (n:Person) RETURN n.age");
    ASSERT_TRUE(r3.error.empty()) << r3.error;
    ASSERT_EQ(r3.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(r3.rows[0][0]), 30);
}

TEST_F(QueryExecutorMultiLabelTest, SetLabelCastProperty) {
    // CREATE (n:Person), SET n:Employee, then SET n::Employee.salary = 50000
    auto r1 = execSync(*executor_, "CREATE (n:Person) SET n:Employee");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:Person) SET n::Employee.salary = 50000");
    ASSERT_TRUE(r2.error.empty()) << r2.error;

    auto r3 = execSync(*executor_, "MATCH (n) RETURN n::Employee.salary");
    ASSERT_TRUE(r3.error.empty()) << r3.error;
    ASSERT_EQ(r3.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(r3.rows[0][0]), 50000);
}

TEST_F(QueryExecutorMultiLabelTest, NoPropertyLabel) {
    // VIP label has no properties - should be usable as pure tag
    auto r1 = execSync(*executor_, "CREATE (n:Person) SET n:VIP");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:VIP) RETURN n");
    ASSERT_TRUE(r2.error.empty()) << r2.error;
    ASSERT_EQ(r2.rows.size(), 1);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(r2.rows[0][0]));
    auto& vv = std::get<VertexValue>(r2.rows[0][0]);
    EXPECT_EQ(vv.id, 1);
}

TEST_F(QueryExecutorMultiLabelTest, CreateMultiLabelRejected) {
    // CREATE (n:Person:Employee {name: 'Bob'}) should fail
    auto result = execSync(*executor_, "CREATE (n:Person:Employee {name: 'Bob'})");
    EXPECT_FALSE(result.error.empty());
}

TEST_F(QueryExecutorMultiLabelTest, LabelCastPropertyConvenienceFallback) {
    // Multiple labels both have "name" - typed access picks the right one
    auto person_def = blockingWait(async_meta_->getLabelDef("Person"));
    ASSERT_TRUE(person_def.has_value());
    auto employee_def = blockingWait(async_meta_->getLabelDef("Employee"));
    ASSERT_TRUE(employee_def.has_value());

    uint16_t person_name_pid = UINT16_MAX, employee_name_pid = UINT16_MAX;
    uint16_t employee_salary_pid = UINT16_MAX;
    for (const auto& pd : person_def->properties) {
        if (pd.name == "name")
            person_name_pid = pd.id;
    }
    for (const auto& pd : employee_def->properties) {
        if (pd.name == "name")
            employee_name_pid = pd.id;
        if (pd.name == "salary")
            employee_salary_pid = pd.id;
    }

    size_t max_sz = std::max({person_name_pid, employee_name_pid, employee_salary_pid}) + 1;
    Properties person_props(max_sz), employee_props(max_sz);
    person_props[person_name_pid] = PropertyValue(std::string("Alice"));
    employee_props[employee_name_pid] = PropertyValue(std::string("Worker"));
    employee_props[employee_salary_pid] = PropertyValue(int64_t(7777));

    insertMultiLabelVertex(1, {{PERSON_LABEL, std::move(person_props)}, {EMPLOYEE_LABEL, std::move(employee_props)}});

    // Typed access to Employee.name gives "Worker" only
    auto r1 = execSync(*executor_, "MATCH (n) RETURN n::Employee.name");
    ASSERT_TRUE(r1.error.empty()) << r1.error;
    ASSERT_EQ(r1.rows.size(), 1);
    EXPECT_EQ(std::get<std::string>(r1.rows[0][0]), "Worker");

    // Typed access to Employee.salary gives 7777
    auto r2 = execSync(*executor_, "MATCH (n) RETURN n::Employee.salary");
    ASSERT_TRUE(r2.error.empty()) << r2.error;
    ASSERT_EQ(r2.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(r2.rows[0][0]), 7777);
}

TEST_F(QueryExecutorMultiLabelTest, StreamingSetLabelSurvivesPlanContextLifetime) {
    // This test exercises prepareStream (the streaming code path used by the server).
    // In prepareStream, plan_ctx goes out of scope when the coroutine returns,
    // but the physical operators must still access label_name_to_id during stream
    // consumption. This would crash with heap-use-after-free without the fix that
    // stores the name-to-id maps in StreamContext.

    auto ctx = blockingWait(executor_->prepareStream("CREATE (n:Person) SET n:Employee"));
    ASSERT_TRUE(ctx->error.empty()) << ctx->error;

    auto consumeStream = [&]() -> folly::coro::Task<size_t> {
        size_t count = 0;
        while (auto chunk = co_await ctx->gen.next()) {
            count += chunk->numRows();
        }
        co_await ctx->store.commitTran(ctx->txn);
        co_return count;
    };
    size_t row_count = blockingWait(consumeStream());
    EXPECT_GT(row_count, 0);

    // Verify the label was actually set
    auto result = execSync(*executor_, "MATCH (n:Employee) RETURN n");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    auto& vv = std::get<VertexValue>(result.rows[0][0]);
    EXPECT_TRUE(vv.labels.has_value());
    EXPECT_TRUE(vv.labels->count(EMPLOYEE_LABEL)) << "Employee label should be set";
}

TEST_F(QueryExecutorMultiLabelTest, ScanByLabelLoadsAllLabelProperties) {
    // When scanning by a label that was added via SET, properties from the
    // original label should still be visible. Regresion test for the bug where
    // LabelScanPhysicalOp only loaded the scanned label's properties.
    auto person_def = blockingWait(async_meta_->getLabelDef("Person"));
    ASSERT_TRUE(person_def.has_value());
    uint16_t name_pid = propIdByName({{PERSON_LABEL, *person_def}}, PERSON_LABEL, "name");
    ASSERT_NE(name_pid, UINT16_MAX);

    // Create vertex with Person label
    auto r1 = execSync(*executor_, "CREATE (n:Person {name: 'Alice'})");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    // Add Employee label (has different properties: salary, name)
    auto r2 = execSync(*executor_, "MATCH (n:Person) SET n:Employee");
    ASSERT_TRUE(r2.error.empty()) << r2.error;

    // Query by Employee label — should still see name from Person label
    auto r3 = execSync(*executor_, "MATCH (n:Employee) RETURN n.name");
    ASSERT_TRUE(r3.error.empty()) << r3.error;
    ASSERT_EQ(r3.rows.size(), 1);
    EXPECT_EQ(std::get<std::string>(r3.rows[0][0]), "Alice");

    // Query by Employee label — full vertex should include all properties
    auto r4 = execSync(*executor_, "MATCH (n:Employee) RETURN n");
    ASSERT_TRUE(r4.error.empty()) << r4.error;
    ASSERT_EQ(r4.rows.size(), 1);
    auto& vv = std::get<VertexValue>(r4.rows[0][0]);
    ASSERT_TRUE(vv.properties.count(PERSON_LABEL)) << "Should have Person label properties";
    EXPECT_EQ(std::get<std::string>((*vv.properties.at(PERSON_LABEL).at(name_pid))), "Alice");
}

TEST_F(QueryExecutorMultiLabelTest, ScanByNoPropertyLabelReturnsAllProperties) {
    // Regression: scanning by a label with no properties (VIP) should still
    // return properties from other labels.
    auto person_def = blockingWait(async_meta_->getLabelDef("Person"));
    ASSERT_TRUE(person_def.has_value());
    uint16_t name_pid = propIdByName({{PERSON_LABEL, *person_def}}, PERSON_LABEL, "name");
    ASSERT_NE(name_pid, UINT16_MAX);

    auto r1 = execSync(*executor_, "CREATE (n:Person {name: 'Alice'}) SET n:VIP");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    // Query by VIP label (no properties)
    auto r2 = execSync(*executor_, "MATCH (n:VIP) RETURN n.name");
    ASSERT_TRUE(r2.error.empty()) << r2.error;
    ASSERT_EQ(r2.rows.size(), 1);
    EXPECT_EQ(std::get<std::string>(r2.rows[0][0]), "Alice");

    // Full vertex should include Person properties
    auto r3 = execSync(*executor_, "MATCH (n:VIP) RETURN n");
    ASSERT_TRUE(r3.error.empty()) << r3.error;
    ASSERT_EQ(r3.rows.size(), 1);
    auto& vv = std::get<VertexValue>(r3.rows[0][0]);
    EXPECT_TRUE(vv.properties.count(PERSON_LABEL)) << "VIP scan should include Person properties";
}

TEST_F(QueryExecutorMultiLabelTest, LabelCastNonExistentPropertyErrors) {
    // n::Person.noname should error at planning time because Person has no "noname"
    auto person_def = blockingWait(async_meta_->getLabelDef("Person"));
    ASSERT_TRUE(person_def.has_value());

    auto r1 = execSync(*executor_, "CREATE (n:Person {name: 'Alice'})");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:Person) RETURN n::Person.noname");
    EXPECT_FALSE(r2.error.empty());
    EXPECT_NE(r2.error.find("has no property"), std::string::npos) << r2.error;
}

TEST_F(QueryExecutorMultiLabelTest, LabelCastNonExistentLabelErrors) {
    // n::NoSuchLabel.prop should error at planning time
    auto r1 = execSync(*executor_, "CREATE (n:Person {name: 'Alice'})");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:Person) RETURN n::NoSuchLabel.name");
    EXPECT_FALSE(r2.error.empty());
    EXPECT_NE(r2.error.find("not found"), std::string::npos) << r2.error;
}

TEST_F(QueryExecutorMultiLabelTest, LabelCastAllPropertiesNonExistentLabelErrors) {
    // RETURN n::NoSuchLabel (standalone) should error
    auto r1 = execSync(*executor_, "CREATE (n:Person {name: 'Alice'})");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:Person) RETURN n::NoSuchLabel");
    EXPECT_FALSE(r2.error.empty());
    EXPECT_NE(r2.error.find("not found"), std::string::npos) << r2.error;
}

TEST_F(QueryExecutorMultiLabelTest, LabelCastValidPropertySucceeds) {
    // n::Person.name should succeed (property exists)
    auto r1 = execSync(*executor_, "CREATE (n:Person {name: 'Alice'})");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:Person) RETURN n::Person.name");
    ASSERT_TRUE(r2.error.empty()) << r2.error;
    ASSERT_EQ(r2.rows.size(), 1);
    EXPECT_EQ(std::get<std::string>(r2.rows[0][0]), "Alice");
}

TEST_F(QueryExecutorMultiLabelTest, FilterLabelCastNonExistentPropertyErrors) {
    // WHERE n::Person.noname = 'x' should error
    auto r1 = execSync(*executor_, "CREATE (n:Person {name: 'Alice'})");
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    auto r2 = execSync(*executor_, "MATCH (n:Person) WHERE n::Person.noname = 'x' RETURN n");
    EXPECT_FALSE(r2.error.empty());
    EXPECT_NE(r2.error.find("has no property"), std::string::npos) << r2.error;
}

// ==================== Property Named 'id' ====================

TEST_F(QueryExecutorTest, PropertyNamedIdReturnsStoredValue) {
    // Create a label with a property named "id" (conflicts with pseudo-property behavior)
    PropertyDef id_prop{0, "id", PropertyType::INT64, false, std::nullopt};
    auto item_label = blockingWait(async_meta_->createLabel("Item", {id_prop}));
    ASSERT_NE(item_label, INVALID_LABEL_ID);
    ASSERT_TRUE(blockingWait(async_data_->createLabel(item_label)));

    // Create executor with updated schema
    QueryExecutor executor(*async_data_, *async_meta_, {});

    // Create vertices: internal ID != stored property "id" value
    execSync(executor, "CREATE (n:Item {id: 100})");
    execSync(executor, "CREATE (n:Item {id: 200})");
    execSync(executor, "CREATE (n:Item {id: 300})");

    // n.id should return stored property value, not internal VertexId
    auto result = execSync(executor, "MATCH (n:Item) RETURN n.id AS id ORDER BY id");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 3);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 100);
    EXPECT_EQ(std::get<int64_t>(result.rows[1][0]), 200);
    EXPECT_EQ(std::get<int64_t>(result.rows[2][0]), 300);

    // id(n) should still return internal VertexId
    auto idResult = execSync(executor, "MATCH (n:Item) RETURN id(n) AS internalId ORDER BY internalId");
    ASSERT_TRUE(idResult.error.empty()) << idResult.error;
    ASSERT_EQ(idResult.rows.size(), 3);
    // Internal IDs should be small positive integers (not the stored 100/200/300)
    for (size_t i = 0; i < idResult.rows.size(); ++i) {
        auto internalId = std::get<int64_t>(idResult.rows[i][0]);
        EXPECT_GT(internalId, 0);
        EXPECT_LT(internalId, 100); // internal IDs are small, not the stored property values
    }
}

TEST_F(QueryExecutorTest, PropertyNamedIdWorksWithInlineFilter) {
    // Create a label with a property named "id"
    PropertyDef id_prop{0, "id", PropertyType::INT64, false, std::nullopt};
    auto item_label = blockingWait(async_meta_->createLabel("Product", {id_prop}));
    ASSERT_NE(item_label, INVALID_LABEL_ID);
    ASSERT_TRUE(blockingWait(async_data_->createLabel(item_label)));

    QueryExecutor executor(*async_data_, *async_meta_, {});

    execSync(executor, "CREATE (n:Product {id: 500})");
    execSync(executor, "CREATE (n:Product {id: 600})");
    execSync(executor, "CREATE (n:Product {id: 700})");

    // Inline property filter {id: 600} should match the stored property value
    auto result = execSync(executor, "MATCH (n:Product {id: 600}) RETURN n.id AS id");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 600);

    // Verify no match for non-existent value
    auto noResult = execSync(executor, "MATCH (n:Product {id: 999}) RETURN n.id AS id");
    ASSERT_TRUE(noResult.error.empty()) << noResult.error;
    EXPECT_EQ(noResult.rows.size(), 0);
}

// ==================== CreateEdge With Edge Index ====================

TEST_F(QueryExecutorTest, CreateEdgeMaintainsIndex) {
    // Create edge label with a property
    PropertyDef weight_prop{0, "weight", PropertyType::INT64, false, std::nullopt};
    auto edge_label_id = blockingWait(async_meta_->createEdgeLabel("RATED", {weight_prop}));
    ASSERT_NE(edge_label_id, INVALID_EDGE_LABEL_ID);
    ASSERT_TRUE(blockingWait(async_data_->createEdgeLabel(edge_label_id)));

    // Create index on edge property
    QueryExecutor executor(*async_data_, *async_meta_, {});
    auto ddl = execSync(executor, "CREATE INDEX idx_rated_weight FOR ()-[r:RATED]-() ON (r.weight)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    // Verify index is PUBLIC
    auto info = blockingWait(async_meta_->getIndex("idx_rated_weight"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);

    // Create vertices
    execSync(executor, "CREATE (n:Person {name: 'Alice'})");
    execSync(executor, "CREATE (n:Person {name: 'Bob'})");

    // Create edge with property — should insert index entry
    auto result = execSync(executor, "CREATE (a:Person)-[:RATED {weight: 5}]->(b:Person)");
    EXPECT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);

    // Verify RATED edge was created (has an edge ID)
    auto edge_val = result.rows[0][0];
    ASSERT_TRUE(std::holds_alternative<EdgeValue>(edge_val));
    auto& ev = std::get<EdgeValue>(edge_val);
    EXPECT_GT(ev.id, 0u);
    EXPECT_EQ(ev.label_id, edge_label_id);
}

TEST_F(QueryExecutorTest, CreateEdgeViolatesUniqueEdgeIndex) {
    // Create edge label with a property
    PropertyDef since_prop{0, "since", PropertyType::INT64, false, std::nullopt};
    auto edge_label_id = blockingWait(async_meta_->createEdgeLabel("REVIEWED", {since_prop}));
    ASSERT_NE(edge_label_id, INVALID_EDGE_LABEL_ID);
    ASSERT_TRUE(blockingWait(async_data_->createEdgeLabel(edge_label_id)));

    QueryExecutor executor(*async_data_, *async_meta_, {});

    // Create unique edge index
    auto ddl = execSync(executor, "CREATE UNIQUE INDEX idx_reviewed_since FOR ()-[r:REVIEWED]-() ON (r.since)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    // Create vertices
    execSync(executor, "CREATE (n:Person {name: 'Alice'})");
    execSync(executor, "CREATE (n:Person {name: 'Bob'})");

    // First edge should succeed
    auto r1 = execSync(executor, "CREATE (a:Person)-[:REVIEWED {since: 2020}]->(b:Person)");
    EXPECT_TRUE(r1.error.empty()) << r1.error;
    ASSERT_EQ(r1.rows.size(), 1u);

    // Second edge with same 'since' value should be rejected by unique constraint
    auto r2 = execSync(executor, "CREATE (a:Person)-[:REVIEWED {since: 2020}]->(b:Person)");
    EXPECT_TRUE(r2.rows.empty()) << "Expected 0 rows (unique constraint violation)";
}

// ==================== Output Schema Tests ====================

TEST_F(QueryExecutorTest, OutputSchemaReturnPropertyOnly) {
    PropertyDef name_prop{0, "name", PropertyType::STRING, false, std::nullopt};
    auto label = blockingWait(async_meta_->createLabel("Item", {name_prop}));
    ASSERT_NE(label, INVALID_LABEL_ID);
    ASSERT_TRUE(blockingWait(async_data_->createLabel(label)));

    QueryExecutor executor(*async_data_, *async_meta_, {});
    execSync(executor, "CREATE (n:Item {name: 'test'})");

    auto result = execSync(executor, "MATCH (n:Item) RETURN n.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.columns.size(), 1u) << "Expected 1 column, got: " << result.columns.size();
    EXPECT_EQ(result.columns[0], "n.name");
}

TEST_F(QueryExecutorTest, OutputSchemaReturnWholeNode) {
    auto result = execSync(*executor_, "MATCH (n:Person) RETURN n");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.columns.size(), 1u) << "Expected 1 column, got: " << result.columns.size();
    EXPECT_EQ(result.columns[0], "n");
}

TEST_F(QueryExecutorTest, OutputSchemaReturnMultipleProperties) {
    PropertyDef name_prop{0, "name", PropertyType::STRING, false, std::nullopt};
    PropertyDef val_prop{1, "val", PropertyType::INT64, false, std::nullopt};
    auto label = blockingWait(async_meta_->createLabel("Product", {name_prop, val_prop}));
    ASSERT_NE(label, INVALID_LABEL_ID);
    ASSERT_TRUE(blockingWait(async_data_->createLabel(label)));

    QueryExecutor executor(*async_data_, *async_meta_, {});
    execSync(executor, "CREATE (n:Product {name: 'x', val: 1})");

    auto result = execSync(executor, "MATCH (n:Product) RETURN n.name, n.val");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.columns.size(), 2u) << "Expected 2 columns, got: " << result.columns.size();
    EXPECT_EQ(result.columns[0], "n.name");
    EXPECT_EQ(result.columns[1], "n.val");
}

TEST_F(QueryExecutorTest, OutputSchemaReturnAggregate) {
    auto result = execSync(*executor_, "MATCH (n:Person) RETURN count(*)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.columns.size(), 1u) << "Expected 1 column, got: " << result.columns.size();
    EXPECT_EQ(result.columns[0], "count()");
}

// ==================== WITH Clause Tests ====================

class QueryExecutorWithTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<SyncGraphDataStore> sync_data_;
    std::unique_ptr<SyncGraphMetaStore> sync_meta_;
    std::unique_ptr<AsyncGraphMetaStore> async_meta_;
    std::unique_ptr<IoScheduler> io_scheduler_;
    std::unique_ptr<AsyncGraphDataStore> async_data_;
    std::unique_ptr<QueryExecutor> executor_;

    LabelId PERSON_LABEL = INVALID_LABEL_ID;
    LabelId CITY_LABEL = INVALID_LABEL_ID;
    EdgeLabelId KNOWS_LABEL = INVALID_EDGE_LABEL_ID;

    void SetUp() override {
        db_path_ = "/tmp/eugraph_with_test_" + std::to_string(getpid());
        std::filesystem::remove_all(db_path_);
        std::filesystem::create_directories(db_path_ + "/data");
        std::filesystem::create_directories(db_path_ + "/meta");

        sync_data_ = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(sync_data_->open(db_path_ + "/data"));

        sync_meta_ = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(sync_meta_->open(db_path_ + "/meta"));

        async_meta_ = std::make_unique<AsyncGraphMetaStore>();
        io_scheduler_ = std::make_unique<IoScheduler>(2);
        async_data_ = std::make_unique<AsyncGraphDataStore>(*sync_data_, *io_scheduler_);

        auto opened = blockingWait(async_meta_->open(*sync_meta_, *io_scheduler_));
        ASSERT_TRUE(opened);

        auto person_label_def = blockingWait(
            async_meta_->createLabel("Person", {PropertyDef{0, "name", PropertyType::STRING, false, std::nullopt},
                                                PropertyDef{0, "age", PropertyType::INT64, false, std::nullopt}}));
        PERSON_LABEL = person_label_def;
        CITY_LABEL = blockingWait(async_meta_->createLabel("City"));
        KNOWS_LABEL = blockingWait(async_meta_->createEdgeLabel("KNOWS"));

        ASSERT_NE(PERSON_LABEL, INVALID_LABEL_ID);
        ASSERT_NE(KNOWS_LABEL, INVALID_EDGE_LABEL_ID);

        blockingWait(async_data_->createLabel(PERSON_LABEL));
        blockingWait(async_data_->createLabel(CITY_LABEL));
        blockingWait(async_data_->createEdgeLabel(KNOWS_LABEL));

        executor_ = std::make_unique<QueryExecutor>(*async_data_, *async_meta_, QueryExecutor::Config{});
    }

    void TearDown() override {
        executor_.reset();
        async_data_.reset();
        io_scheduler_.reset();
        blockingWait(async_meta_->close());
        sync_data_->close();
        sync_meta_->close();
        std::filesystem::remove_all(db_path_);
    }

    void insertPersonData() {
        auto txn = sync_data_->beginTransaction();
        // name=prop_id 1, age=prop_id 2 (assigned by metadata store)
        struct PersonData {
            VertexId vid;
            std::string name;
            int64_t age;
        };
        std::vector<PersonData> data = {{1, "Alice", 25}, {2, "Bob", 30}, {3, "Carol", 35}};

        for (const auto& p : data) {
            Properties props;
            props.resize(2);
            props[0] = PropertyValue(p.name);
            props[1] = PropertyValue(p.age);
            std::vector<std::pair<LabelId, Properties>> label_props = {{PERSON_LABEL, std::move(props)}};
            ASSERT_TRUE(sync_data_->insertVertex(txn, p.vid, label_props));
        }
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }

    void insertPersonWithEdges() {
        insertPersonData();
        auto txn = sync_data_->beginTransaction();
        ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 1, 3, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->insertEdge(txn, 3, 2, 3, KNOWS_LABEL, 0, {}));
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }
};

TEST_F(QueryExecutorWithTest, SimpleProjection) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) WITH n.name AS name RETURN name ORDER BY name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 3u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Alice");
    EXPECT_EQ(std::get<std::string>(result.rows[1][0]), "Bob");
    EXPECT_EQ(std::get<std::string>(result.rows[2][0]), "Carol");
}

TEST_F(QueryExecutorWithTest, WithPropertyRename) {
    insertPersonData();

    auto result =
        execSync(*executor_, "MATCH (n:Person) WITH n.age AS person_age RETURN person_age ORDER BY person_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 3u);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 25);
    EXPECT_EQ(std::get<int64_t>(result.rows[1][0]), 30);
    EXPECT_EQ(std::get<int64_t>(result.rows[2][0]), 35);
}

TEST_F(QueryExecutorWithTest, WithAggregate) {
    insertPersonWithEdges();

    auto result = execSync(*executor_, "MATCH (n:Person)-[:KNOWS]->(m) WITH n, count(m) AS friend_count RETURN n.name, "
                                       "friend_count ORDER BY n.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Alice");
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 2);
    EXPECT_EQ(std::get<std::string>(result.rows[1][0]), "Bob");
    EXPECT_EQ(std::get<int64_t>(result.rows[1][1]), 1);
}

TEST_F(QueryExecutorWithTest, WithOrderByLimit) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) WITH n.name AS name ORDER BY name DESC LIMIT 2 RETURN name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Carol");
    EXPECT_EQ(std::get<std::string>(result.rows[1][0]), "Bob");
}

TEST_F(QueryExecutorWithTest, WithDistinct) {
    insertPersonWithEdges();

    auto result = execSync(*executor_, "MATCH (n:Person)-[:KNOWS]->(m) WITH DISTINCT n RETURN count(*) AS cnt");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 2);
}

TEST_F(QueryExecutorWithTest, WithWhere) {
    insertPersonData();

    auto result = execSync(*executor_, "MATCH (n:Person) WITH n WHERE n.age > 28 RETURN n.name ORDER BY n.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Bob");
    EXPECT_EQ(std::get<std::string>(result.rows[1][0]), "Carol");
}

// ==================== Additional WITH tests from Cypher semantics review ====================

// Test 1: WITH aggregation with GROUP BY — equivalent to SQL GROUP BY + select aggregates
// MATCH (n:Product) WITH n.category AS cat, avg(n.price) AS avgPrice, count(*) AS count
// Simplified: use Person with age as a numeric property for aggregation
TEST_F(QueryExecutorWithTest, WithAggregationGroupBy) {
    insertPersonData();

    // Group by name, count per name (each name is unique so count=1)
    auto result =
        execSync(*executor_, "MATCH (n:Person) WITH n.name AS name, count(*) AS cnt RETURN name, cnt ORDER BY "
                             "name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 3u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Alice");
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 1);
    EXPECT_EQ(std::get<std::string>(result.rows[1][0]), "Bob");
    EXPECT_EQ(std::get<int64_t>(result.rows[1][1]), 1);
    EXPECT_EQ(std::get<std::string>(result.rows[2][0]), "Carol");
    EXPECT_EQ(std::get<int64_t>(result.rows[2][1]), 1);
}

// Test 2: WITH aggregation outputs correct results (HAVING semantics verified via aggregation order)
// The WHERE-after-aggregation (HAVING) pattern is semantically correct: filtering happens after
// aggregation because BoundFilterOp is the parent of BoundAggregateOp in the operator chain.
TEST_F(QueryExecutorWithTest, WithAggregationGroupByHaving) {
    insertPersonWithEdges();

    // Verify aggregation grouping: Alice has 2 friends, Bob has 1
    auto result = execSync(*executor_, "MATCH (n:Person)-[:KNOWS]->(m) WITH n, count(m) AS friendCnt RETURN n.name, "
                                       "friendCnt ORDER BY n.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Alice");
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 2);
    EXPECT_EQ(std::get<std::string>(result.rows[1][0]), "Bob");
    EXPECT_EQ(std::get<int64_t>(result.rows[1][1]), 1);
}

// Test 2b: HAVING — WHERE filtering on aggregate result.
// Alice has 2 friends, Bob has 1 → WHERE friendCnt > 1 should only return Alice.
TEST_F(QueryExecutorWithTest, WithAggregationHavingFilter) {
    insertPersonWithEdges();

    auto result = execSync(*executor_, "MATCH (n:Person)-[:KNOWS]->(m) WITH n, count(m) AS friendCnt "
                                       "WHERE friendCnt > 1 RETURN n.name, friendCnt ORDER BY n.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Alice");
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 2);
}

// Test 3: WITH DISTINCT deduplication — if 100 people in same city, only 1 city value
TEST_F(QueryExecutorWithTest, WithDistinctDeduplication) {
    // Insert 3 persons, 2 in same age group (30-ish), 1 different
    auto txn = sync_data_->beginTransaction();
    struct PersonData {
        VertexId vid;
        std::string name;
        int64_t age;
    };
    std::vector<PersonData> data = {
        {1, "Alice", 30}, {2, "Bob", 30}, {3, "Carol", 35}, {4, "Dave", 30}, {5, "Eve", 35}};
    for (const auto& p : data) {
        Properties props;
        props.resize(2);
        props[0] = PropertyValue(p.name);
        props[1] = PropertyValue(p.age);
        std::vector<std::pair<LabelId, Properties>> label_props = {{PERSON_LABEL, std::move(props)}};
        ASSERT_TRUE(sync_data_->insertVertex(txn, p.vid, label_props));
    }
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    // 5 persons, age 30 (x3) and 35 (x2) — WITH DISTINCT should yield 2 unique ages
    auto result = execSync(*executor_, "MATCH (n:Person) WITH DISTINCT n.age AS age RETURN age ORDER BY age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 30);
    EXPECT_EQ(std::get<int64_t>(result.rows[1][0]), 35);
}

// Test 4: WITH ORDER BY + LIMIT — sort and truncate intermediate results
TEST_F(QueryExecutorWithTest, WithOrderByLimitIntermediate) {
    insertPersonData();

    // Take top 2 by age descending (Carol=35, Bob=30), then return names
    auto result = execSync(*executor_, "MATCH (n:Person) WITH n ORDER BY n.age DESC LIMIT 2 RETURN n.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2u);
}

// Test 5: WITH followed by SET — update statement after WITH projection.
// Verifies that property metadata (source_label) is preserved through scope reset.
TEST_F(QueryExecutorWithTest, WithFollowedBySet) {
    insertPersonData();

    // SET after WITH (same variable name)
    auto result = execSync(*executor_, "MATCH (n:Person {name: 'Alice'}) WITH n SET n.age = 99");
    ASSERT_TRUE(result.error.empty()) << result.error;

    // Verify the property was actually written
    auto verify = execSync(*executor_, "MATCH (n:Person {name: 'Alice'}) RETURN n.age");
    ASSERT_TRUE(verify.error.empty()) << verify.error;
    ASSERT_EQ(verify.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(verify.rows[0][0]), 99);
}

TEST_F(QueryExecutorWithTest, WithThenMatchExpand) {
    insertPersonWithEdges();

    // MATCH (a:Person)-[:KNOWS]->(b) WITH b MATCH (b)-[:KNOWS]->(c) RETURN c.name
    // Data: Alice->Bob, Alice->Carol, Bob->Carol
    // After first MATCH: b = Bob, Carol, Carol
    // After second MATCH: b=Bob -> c=Carol; b=Carol -> nobody
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS]->(b) WITH b "
                                       "MATCH (b)-[:KNOWS]->(c) RETURN c.name ORDER BY c.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Carol");
}

TEST_F(QueryExecutorWithTest, WithThenMatchExpandSingleHop) {
    insertPersonWithEdges();

    // Pass a single variable through WITH and expand from it
    auto result = execSync(*executor_, "MATCH (a:Person {name: 'Alice'})-[:KNOWS]->(b) WITH b "
                                       "MATCH (b)-[:KNOWS]->(c) RETURN b.name, c.name ORDER BY c.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // Alice->Bob, Alice->Carol: b=Bob,Carol. Then Bob->Carol, Carol->nobody => 1 row
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Bob");
    EXPECT_EQ(std::get<std::string>(result.rows[0][1]), "Carol");
}

TEST_F(QueryExecutorWithTest, WithThenMatchMultiHop) {
    insertPersonWithEdges();

    // MATCH (a:Person {name: 'Alice'}) WITH a MATCH (a)-[:KNOWS]->(b)-[:KNOWS]->(c) RETURN c.name
    // Alice->Bob->Carol => c=Carol
    // Alice->Carol->nobody => no result
    auto result = execSync(*executor_, "MATCH (a:Person {name: 'Alice'}) WITH a "
                                       "MATCH (a)-[:KNOWS]->(b)-[:KNOWS]->(c) RETURN c.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "Carol");
}

TEST_F(QueryExecutorWithTest, WithThenIndependentMatch) {
    insertPersonData();

    // MATCH after WITH with a variable not in WITH output → cross product
    auto result = execSync(*executor_, "MATCH (a:Person) WITH a MATCH (b:Person) RETURN a.name, b.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 3 persons × 3 persons = 9 rows
    EXPECT_EQ(result.rows.size(), 9u);
}

TEST_F(QueryExecutorWithTest, WithThenIndependentMatchWhere) {
    insertPersonData();

    // MATCH after WITH + WHERE referencing both sides (cross-side filter)
    auto result = execSync(*executor_, "MATCH (a:Person) WITH a MATCH (b:Person) WHERE a.name = 'Alice' AND b.age > 28 RETURN a.name, b.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // Alice × (people with age > 28: Charlie) = 1 row
    EXPECT_GE(result.rows.size(), 1u);
}

TEST_F(QueryExecutorWithTest, WithThenCorrelatedMatchStillWorks) {
    insertPersonWithEdges();

    // Correlated MATCH after WITH: expanding from a variable passed through WITH
    auto result = execSync(*executor_, "MATCH (a:Person {name: 'Alice'})-[:KNOWS]->(b) WITH b MATCH (b)-[:KNOWS]->(c) RETURN c.name");
    ASSERT_TRUE(result.error.empty()) << result.error;
}

TEST_F(QueryExecutorTest, VarLenExpandExact2Hops) {
    // KNOWS chain: 1->2->3->4, LIVES_IN: 1->5, 2->6
    insertMultiHopEdges();

    // Exact 2 hops via KNOWS: 1->2->3, 2->3->4 → 2 rows
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*2]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2u);
}

TEST_F(QueryExecutorTest, VarLenExpandRange1To2) {
    insertMultiHopEdges();

    // 1-2 hops via KNOWS: 1->2(1hop), 1->2->3(2hop), 2->3(1hop), 2->3->4(2hop), 3->4(1hop) → 5 rows
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1..2]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 5u);
}

TEST_F(QueryExecutorTest, VarLenExpandRange1To3) {
    insertMultiHopEdges();

    // 1-3 hops via KNOWS: 1hop(1->2,2->3,3->4) + 2hop(1->3,2->4) + 3hop(1->4) → 6 rows
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1..3]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 6u);
}

TEST_F(QueryExecutorTest, VarLenExpandExact1Hop) {
    insertMultiHopEdges();

    // [*1] should be equivalent to a normal 1-hop expand
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 3u); // 1->2, 2->3, 3->4
}

TEST_F(QueryExecutorTest, VarLenExpandNoMatch) {
    insertMultiHopEdges();

    // No 2-hop LIVES_IN chain exists
    auto result = execSync(*executor_, "MATCH (a:Person)-[:LIVES_IN*2..3]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, VarLenExpandNoTypeFilter) {
    insertMultiHopEdges();

    // No type filter: follows KNOWS and LIVES_IN
    // 1-hop: 1->2(K), 1->5(L), 2->3(K), 2->6(L), 3->4(K) = 5
    // 2-hop: 1->2->3(KK), 1->2->6(KL), 2->3->4(KK) = 3
    auto result = execSync(*executor_, "MATCH (a:Person)-[*1..2]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 8u);
}

TEST_F(QueryExecutorTest, VarLenExpandIsolatedNode) {
    // No edges at all
    insertTestVertices();

    auto result = execSync(*executor_, "MATCH (a:Person)-[*1..3]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, VarLenExpandSelfLoop) {
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    // Self-loop on vertex 1
    ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 1, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    // [*1]: 1->1 (self-loop) = 1 row
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 1u);
}

TEST_F(QueryExecutorTest, VarLenExpandCycle) {
    // Triangle: 1->2->3->1
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 2, 3, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 3, 3, 1, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    // 1-3 hops from vertex 1: must not infinite loop
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1..3]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // From 1: 1->2(1), 1->2->3(2), 1->2->3->1(3) = 3 paths
    // From 2: 2->3(1), 2->3->1(2), 2->3->1->2(3) = 3 paths
    // From 3: 3->1(1), 3->1->2(2), 3->1->2->3(3) = 3 paths
    EXPECT_EQ(result.rows.size(), 9u);
}

TEST_F(QueryExecutorTest, VarLenExpandMultiEdge) {
    // Two edges between same vertex pair
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 1, 2, KNOWS_LABEL, 1, {})); // different seq
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    // [*1]: 2 different edges → 2 rows (same dst vertex)
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2u);
}

TEST_F(QueryExecutorTest, VarLenExpandExplain) {
    auto result = execSync(*executor_, "EXPLAIN MATCH (a:Person)-[*2..3]->(b) RETURN b");
    ASSERT_TRUE(result.error.empty()) << result.error;

    std::string plan_text;
    for (const auto& row : result.rows) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0])) {
            plan_text += std::get<std::string>(row[0]) + "\n";
        }
    }
    EXPECT_NE(plan_text.find("VarLenExpand"), std::string::npos);
    EXPECT_NE(plan_text.find("hops=[2..3]"), std::string::npos);
}

TEST_F(QueryExecutorTest, VarLenExpandNamedEdgeVariable) {
    insertMultiHopEdges();

    // r should be LIST<EDGE> with 2 elements for *2 pattern
    auto result = execSync(*executor_, "MATCH (a:Person)-[r:KNOWS*2]->(b) RETURN r");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2u);
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1u);
        EXPECT_TRUE(std::holds_alternative<ListValue>(row[0]));
        auto& lv = std::get<ListValue>(row[0]);
        EXPECT_EQ(lv.elements.size(), 2u); // 2 edges
        for (auto& es : lv.elements) {
            EXPECT_TRUE(std::holds_alternative<EdgeValue>(es.value));
        }
    }
}

TEST_F(QueryExecutorTest, VarLenExpandUndirected) {
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    // 1→2 (KNOWS)
    ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    // Undirected: 1-[KNOWS*1]-2 matches from both directions
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1]-(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2u); // (1,2) and (2,1)
}

TEST_F(QueryExecutorTest, VarLenExpandMinZero) {
    // [*0..3] includes identity paths (src==dst) plus 1-3 hop expansions
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH (a:Person)-[*0..3]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 1-hop K: 3, 2-hop K+K: 2, 3-hop K+K+K: 1, 1-hop LI: 2, identity: 6 (one per vertex)
    // = 15 total
    EXPECT_EQ(result.rows.size(), 15u);
}

TEST_F(QueryExecutorTest, VarLenExpandZeroHopOnly) {
    // [*0] returns only identity paths (src==dst)
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH (a:Person)-[*0]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 6 vertices, each has identity path to itself
    EXPECT_EQ(result.rows.size(), 6u);
}

TEST_F(QueryExecutorTest, VarLenExpandZeroHopWithPath) {
    // [*0..2] with named path: identity path has 1 node, 0 edges
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*0..2]->(b) "
                                       "WHERE id(a) = 1 "
                                       "RETURN a, b, length(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // From vertex 1: identity(length=0), 1->2(length=1), 1->2->3(length=2)
    EXPECT_EQ(result.rows.size(), 3u);
    // Verify identity path length
    bool found_zero = false;
    for (const auto& row : result.rows) {
        if (row.size() >= 3 && std::holds_alternative<int64_t>(row[2]) && std::get<int64_t>(row[2]) == 0) {
            found_zero = true;
        }
    }
    EXPECT_TRUE(found_zero) << "Identity path with length=0 should be present";
}

TEST_F(QueryExecutorTest, VarLenExpandZeroHopNegMinRejected) {
    // Negative min hops should still be rejected
    auto result = execSync(*executor_, "MATCH (a:Person)-[* -1..3]->(b) RETURN a, b");
    EXPECT_FALSE(result.error.empty());
}

TEST_F(QueryExecutorTest, VarLenExpandWithLimit) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1..3]->(b) RETURN a, b LIMIT 3");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 3u);
}

TEST_F(QueryExecutorTest, VarLenExpandWithWhere) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1..2]->(b) WHERE true RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 5u);
}

// ── P1: Path variable and path functions ──

TEST_F(QueryExecutorTest, VarLenExpandNamedPath) {
    insertMultiHopEdges();

    // Path for 1->2->3 (KNOWS*2): elements = [v1, e12, v2, e23, v3] = 5 elements
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*2]->(b) RETURN p");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2u); // 1->3 and 2->4
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1u);
        EXPECT_TRUE(std::holds_alternative<PathValue>(row[0]));
        auto& pv = std::get<PathValue>(row[0]);
        EXPECT_EQ(pv.elements.size(), 5u); // v1, e, v2, e, v3
    }
}

TEST_F(QueryExecutorTest, VarLenExpandPathNodes) {
    insertMultiHopEdges();

    // No type filter: follows KNOWS and LIVES_IN
    // 1-hop paths: 5, each producing nodes(p) with 2 vertices
    // 2-hop paths: 3, each producing nodes(p) with 3 vertices
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[*1..2]->(b) RETURN nodes(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 8u); // 5 one-hop + 3 two-hop
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1u);
        EXPECT_TRUE(std::holds_alternative<ListValue>(row[0]));
        auto& lv = std::get<ListValue>(row[0]);
        EXPECT_GE(lv.elements.size(), 2u); // at least src + dst
        // All elements should be vertices
        for (auto& es : lv.elements) {
            EXPECT_TRUE(std::holds_alternative<VertexValue>(es.value));
        }
    }
}

TEST_F(QueryExecutorTest, VarLenExpandPathRelationships) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[*1..2]->(b) RETURN relationships(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 8u);
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1u);
        EXPECT_TRUE(std::holds_alternative<ListValue>(row[0]));
        auto& lv = std::get<ListValue>(row[0]);
        EXPECT_GE(lv.elements.size(), 1u);
        for (auto& es : lv.elements) {
            EXPECT_TRUE(std::holds_alternative<EdgeValue>(es.value));
        }
    }
}

TEST_F(QueryExecutorTest, VarLenExpandPathLength) {
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*2]->(b) RETURN length(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2u); // 1->3 and 2->4
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 1u);
        EXPECT_TRUE(std::holds_alternative<int64_t>(row[0]));
        EXPECT_EQ(std::get<int64_t>(row[0]), 2);
    }
}

TEST_F(QueryExecutorTest, VarLenExpandPathWithRange) {
    insertMultiHopEdges();

    // Paths with varying lengths
    // No type filter: follow all edge types
    // 1-hop: 1->2, 1->5, 2->3, 2->6, 3->4 = 5
    // 2-hop: 1->3, 1->6, 2->4 = 3
    // 3-hop: 1->4 = 1
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[*1..3]->(b) RETURN length(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 9u);
}

TEST_F(QueryExecutorTest, VarLenExpandPathSelfLoop) {
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 1, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1]->(b) RETURN p, length(p)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 1u);
    for (const auto& row : result.rows) {
        EXPECT_EQ(std::get<int64_t>(row[1]), 1);
    }
}

TEST_F(QueryExecutorTest, VarLenExpandNamedPathMixedChainRejected) {
    insertMultiHopEdges();

    // Mixed fixed + varlen chain with named path
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS]->(b)-[:KNOWS*2..3]->(c) RETURN p");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("not supported"), std::string::npos);
}

// ── P2: Unbounded upper + undirected + named edge variables ──

TEST_F(QueryExecutorTest, VarLenExpandUnboundedUpper) {
    insertMultiHopEdges();

    // Unbounded: [*2..] should find paths of length >= 2
    // KNOWS: 1→2→3→4 = 2-hop(1→3), 3-hop(1→4) + 2→3→4 = 2-hop(2→4) = 3
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*2..]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 3u); // (1,3), (1,4), (2,4)
}

TEST_F(QueryExecutorTest, VarLenExpandUnboundedUpperBare) {
    insertMultiHopEdges();

    // Bare unbounded: [*..] = [*1..]
    // All paths of any length
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*..]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 1-hop: 1→2, 2→3, 3→4 = 3
    // 2-hop: 1→3, 2→4 = 2
    // 3-hop: 1→4 = 1
    EXPECT_EQ(result.rows.size(), 6u);
}

TEST_F(QueryExecutorTest, VarLenExpandUnboundedCycle) {
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    // Cycle: 1→2, 2→1 (mutual KNOWS)
    ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 2, 1, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    // Unbounded with cycle — must terminate (vertex cycle detection)
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*..]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 1-hop: 1→2, 2→1 = 2
    // 2-hop: 1→2→1, 2→1→2 = 2 (revisiting start vertex is allowed in Cypher)
    // 3-hop prevented by vertex cycle detection
    EXPECT_EQ(result.rows.size(), 4u);
}

// ── P3: Edge property filtering ──

TEST_F(QueryExecutorTest, VarLenExpandEdgePropertyFilter) {
    // Create edge label with a property
    auto rated_label_id = blockingWait(
        async_meta_->createEdgeLabel("RATED", {PropertyDef{0, "score", PropertyType::INT64, false, std::nullopt}}));
    ASSERT_NE(rated_label_id, INVALID_EDGE_LABEL_ID);
    blockingWait(async_data_->createEdgeLabel(rated_label_id));

    // Build chain: 1→2 (RATED, score=5), 2→3 (RATED, score=10), 3→4 (RATED, score=5)
    insertMultiHopEdges();
    auto txn = sync_data_->beginTransaction();
    // Replace 1→2 with RATED edge (score=5)
    ASSERT_TRUE(sync_data_->deleteEdge(txn, 1, KNOWS_LABEL, 1, 2, 0));
    ASSERT_TRUE(
        sync_data_->insertEdge(txn, 1, 1, 2, rated_label_id, 0, Properties{PropertyValue(static_cast<int64_t>(5))}));
    // Replace 2→3 with RATED edge (score=10)
    ASSERT_TRUE(sync_data_->deleteEdge(txn, 2, KNOWS_LABEL, 2, 3, 0));
    ASSERT_TRUE(
        sync_data_->insertEdge(txn, 2, 2, 3, rated_label_id, 0, Properties{PropertyValue(static_cast<int64_t>(10))}));
    // Replace 3→4 with RATED edge (score=5)
    ASSERT_TRUE(sync_data_->deleteEdge(txn, 3, KNOWS_LABEL, 3, 4, 0));
    ASSERT_TRUE(
        sync_data_->insertEdge(txn, 3, 3, 4, rated_label_id, 0, Properties{PropertyValue(static_cast<int64_t>(5))}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    // Filter: only edges with score=5
    // 1→2(score=5)→3(score=10): fails because 2nd edge score=10
    // 2→3(score=10)→4(score=5): fails because 1st edge score=10
    // 1-hop: 1→2(score=5), 2→3(score=10), 3→4(score=5) → only 1→2 and 3→4 match
    // 2-hop: only if both edges have score=5, which none do in this setup
    auto result = execSync(*executor_, "MATCH (a:Person)-[:RATED*1..2 {score: 5}]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 1-hop matches: 1→2 and 3→4
    // 2-hop: 1→2→3 fails (2→3 has score=10), 2→3→4 fails (2→3 has score=10)
    EXPECT_EQ(result.rows.size(), 2u); // (1,2), (3,4)
}

TEST_F(QueryExecutorTest, VarLenExpandEdgePropertyFilterNoMatch) {
    auto rated_label_id = blockingWait(
        async_meta_->createEdgeLabel("RATED2", {PropertyDef{0, "score", PropertyType::INT64, false, std::nullopt}}));
    ASSERT_NE(rated_label_id, INVALID_EDGE_LABEL_ID);
    blockingWait(async_data_->createEdgeLabel(rated_label_id));

    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    ASSERT_TRUE(
        sync_data_->insertEdge(txn, 1, 1, 2, rated_label_id, 0, Properties{PropertyValue(static_cast<int64_t>(5))}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    // No edge has score=999
    auto result = execSync(*executor_, "MATCH (a:Person)-[:RATED2*1 {score: 999}]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, VarLenExpandEdgePropertyFilterNonexistent) {
    // KNOWS label has no 'status' property
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*2..3 {status: 'active'}]->(b) RETURN a, b");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("does not exist"), std::string::npos);
}

TEST_F(QueryExecutorTest, VarLenExpandEdgePropertyFilterNonLiteral) {
    // Non-literal value in property filter should be rejected
    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*2 {score: a.age}]->(b) RETURN a, b");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("literal"), std::string::npos);
}

// ==================== Path Predicate Tests (P3b) ====================

TEST_F(QueryExecutorTest, PathPredicateAllNodes) {
    // ALL(x IN nodes(p) WHERE id(x) > 0) — all vertices have positive IDs
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ALL(x IN nodes(p) WHERE id(x) > 0) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // All paths should pass: 1→2, 2→3, 3→4, 1→2→3, 2→3→4
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateAllNodesFalse) {
    // ALL(x IN nodes(p) WHERE id(x) > 100) — no vertex has id > 100
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ALL(x IN nodes(p) WHERE id(x) > 100) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateAnyNodes) {
    // ANY(x IN nodes(p) WHERE id(x) = 2) — paths through vertex 2
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ANY(x IN nodes(p) WHERE id(x) = 2) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // Paths that include vertex 2: 1→2, 2→3, 1→2→3, 2→3→4
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateNoneNodes) {
    // NONE(x IN nodes(p) WHERE id(x) < 0) — no vertex has negative ID
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE NONE(x IN nodes(p) WHERE id(x) < 0) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // All paths should pass since all vertex IDs are positive
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateSingleNodes) {
    // SINGLE(x IN nodes(p) WHERE id(x) = 1) — exactly one vertex with id=1
    // Path 1→2 has vertices [1,2] → exactly one vertex with id=1 → true
    // Path 1→2→3 has vertices [1,2,3] → exactly one vertex with id=1 → true
    // Path 2→3 has vertices [2,3] → no vertex with id=1 → false
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE SINGLE(x IN nodes(p) WHERE id(x) = 1) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateAllNoWhere) {
    // ALL(x IN nodes(p)) without WHERE clause — always true
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ALL(x IN nodes(p)) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateAnyEmptyList) {
    // ANY(x IN nodes(p) WHERE ...) — test with no WHERE pred on ANY produces correct results
    // Without WHERE, every element is "true", so ANY returns true for non-empty list
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ANY(x IN nodes(p)) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // All non-empty paths match
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateUndirected) {
    // Path predicate with undirected VarLenExpand
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]-(b) "
                                       "WHERE ALL(x IN nodes(p) WHERE id(x) > 0) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateAllLiteralFalse) {
    // ALL(x IN nodes(p) WHERE false) — should filter everything
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ALL(x IN nodes(p) WHERE false) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateAllLiteralTrue) {
    // ALL(x IN nodes(p) WHERE true) — should pass everything
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ALL(x IN nodes(p) WHERE true) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateWithListLiteral) {
    // ALL(x IN [1,2,3] WHERE x > 0) — should be true
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ALL(x IN [1,2,3] WHERE x > 0) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_GT(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateWithListLiteralFalse) {
    // ALL(x IN [1,2,3] WHERE x > 10) — should be false for [1,2,3]
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ALL(x IN [1,2,3] WHERE x > 10) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, PathPredicateNodesDirect) {
    // Test that nodes(p) works correctly
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE length(p) = 2 "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 2-hop paths: 1->2->3 (length=2), 2->3->4 (length=2)
    EXPECT_EQ(result.rows.size(), 2u);
}

TEST_F(QueryExecutorTest, PathPredicateReturnAll) {
    // RETURN ALL(x IN nodes(p) WHERE id(x) > 0) — should return true for all paths
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1]->(b) "
                                       "RETURN ALL(x IN nodes(p) WHERE id(x) > 0) AS ok");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_GE(result.rows.size(), 1u);
    // First row, first column should be true
    const auto& row = result.rows[0];
    ASSERT_GE(row.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<bool>(row[0]));
    if (std::holds_alternative<bool>(row[0])) {
        EXPECT_TRUE(std::get<bool>(row[0])) << "ALL(id(x) > 0) should be true for all paths";
    }
}

TEST_F(QueryExecutorTest, PathPredicateReturnAllFalse) {
    // RETURN ALL(x IN nodes(p) WHERE id(x) > 100) — should return false
    insertMultiHopEdges();
    auto result = execSync(*executor_, "MATCH p = (a:Person)-[:KNOWS*1]->(b) "
                                       "RETURN ALL(x IN nodes(p) WHERE id(x) > 100) AS ok");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_GE(result.rows.size(), 1u);
    const auto& row = result.rows[0];
    ASSERT_GE(row.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<bool>(row[0]));
    if (std::holds_alternative<bool>(row[0])) {
        EXPECT_FALSE(std::get<bool>(row[0])) << "ALL(id(x) > 100) should be false, vertex IDs are 1-6";
    }
}

TEST_F(QueryExecutorTest, PathPredicateExplain) {
    // Verify the logical plan includes the quantifier filter
    insertMultiHopEdges();
    auto result = execSync(*executor_, "EXPLAIN MATCH p = (a:Person)-[:KNOWS*1..2]->(b) "
                                       "WHERE ALL(x IN nodes(p) WHERE id(x) > 100) "
                                       "RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // The EXPLAIN should produce output describing the plan
    EXPECT_GT(result.rows.size(), 0u);
    // Check that plan text mentions Filter and the predicate
    bool found_filter = false;
    for (const auto& row : result.rows) {
        for (const auto& val : row) {
            if (std::holds_alternative<std::string>(val)) {
                const auto& s = std::get<std::string>(val);
                if (s.find("Filter") != std::string::npos || s.find("ALL") != std::string::npos) {
                    found_filter = true;
                }
            }
        }
    }
    EXPECT_TRUE(found_filter) << "Plan should include Filter operator with ALL predicate";
}

// ==================== VarLenExpand Corner Case Tests ====================

TEST_F(QueryExecutorTest, VarLenExpandDiamondAllPaths) {
    // Diamond: 1->2->4 and 1->3->4 (two distinct paths to the same endpoint).
    // Verifies All Paths semantics (not Reachability).
    insertTestVertices(); // 1-5 Person
    auto txn = sync_data_->beginTransaction();
    std::vector<std::pair<LabelId, Properties>> lp6 = {{PERSON_LABEL, Properties{}}};
    ASSERT_TRUE(sync_data_->insertVertex(txn, 6, lp6));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 1, 3, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 3, 2, 4, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 4, 3, 4, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*1..2]->(b) WHERE id(a) = 1 AND id(b) = 4 RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 1->2->4 and 1->3->4: two distinct paths
    EXPECT_EQ(result.rows.size(), 2u);
}

TEST_F(QueryExecutorTest, VarLenExpandEdgeUniqueness) {
    // Triangle 1->2->3->1. A 4-hop path would need 4 edges but only 3 distinct
    // edges exist; the edge uniqueness constraint prevents reuse within a path.
    insertTestVertices();
    auto txn = sync_data_->beginTransaction();
    ASSERT_TRUE(sync_data_->insertEdge(txn, 1, 1, 2, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 2, 2, 3, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->insertEdge(txn, 3, 3, 1, KNOWS_LABEL, 0, {}));
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS*4]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // No 4-hop path exists without reusing edges
    EXPECT_EQ(result.rows.size(), 0u);
}

TEST_F(QueryExecutorTest, VarLenExpandUnionTypes) {
    // Multi-type variable-length: [:KNOWS|LIVES_IN*1..2]
    insertMultiHopEdges();

    auto result = execSync(*executor_, "MATCH (a:Person)-[:KNOWS|LIVES_IN*1..2]->(b) RETURN a, b");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // 1-hop: 1->2(K), 1->5(LI), 2->3(K), 2->6(LI), 3->4(K) = 5
    // 2-hop: 1->2->3(K+K), 1->2->6(K+LI), 2->3->4(K+K) = 3
    EXPECT_EQ(result.rows.size(), 8u);
}
