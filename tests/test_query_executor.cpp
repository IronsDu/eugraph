#include <gtest/gtest.h>

#include "compute_service/executor/query_executor.hpp"
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
            ASSERT_TRUE(sync_data_->insertVertex(txn, vid, label_props, nullptr));
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
    auto txn = sync_data_->beginTransaction();
    for (VertexId vid = 1; vid <= 3; ++vid) {
        std::vector<std::pair<LabelId, Properties>> lp = {{PERSON_LABEL, Properties{}}};
        ASSERT_TRUE(sync_data_->insertVertex(txn, vid, lp, nullptr));
    }
    for (VertexId vid = 10; vid <= 12; ++vid) {
        std::vector<std::pair<LabelId, Properties>> lp = {{CITY_LABEL, Properties{}}};
        ASSERT_TRUE(sync_data_->insertVertex(txn, vid, lp, nullptr));
    }
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

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
    auto txn = sync_data_->beginTransaction();
    for (VertexId dst = 2; dst <= 5; ++dst) {
        ASSERT_TRUE(sync_data_->insertEdge(txn, static_cast<EdgeId>(dst), 1, dst, KNOWS_LABEL, 0, {}));
    }
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

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
    auto txn = sync_data_->beginTransaction();
    for (VertexId dst = 2; dst <= 5; ++dst) {
        ASSERT_TRUE(sync_data_->insertEdge(txn, static_cast<EdgeId>(dst), 1, dst, KNOWS_LABEL, 0, {}));
    }
    ASSERT_TRUE(sync_data_->commitTransaction(txn));

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

        auto r1 = executor->executeSync("CREATE (n:Person)");
        ASSERT_TRUE(r1.error.empty()) << "CREATE error: " << r1.error;

        auto r2 = executor->executeSync("CREATE (n:Person)");
        ASSERT_TRUE(r2.error.empty()) << "CREATE error: " << r2.error;

        auto before = executor->executeSync("MATCH (n:Person) RETURN n");
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

        auto after = executor->executeSync("MATCH (n:Person) RETURN n");
        EXPECT_TRUE(after.error.empty()) << "MATCH after restart error: " << after.error;
        EXPECT_EQ(after.rows.size(), 2) << "Data should persist across restart";

        auto r3 = executor->executeSync("CREATE (n:Person)");
        EXPECT_TRUE(r3.error.empty()) << "CREATE after restart error: " << r3.error;

        auto scan2 = executor->executeSync("MATCH (n:Person) RETURN n");
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

        auto r1 = executor->executeSync("CREATE (n:Person {name: 'Alice'})");
        ASSERT_TRUE(r1.error.empty()) << r1.error;
        auto r2 = executor->executeSync("CREATE (n:Person {name: 'Bob'})");
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

        auto result = executor->executeSync("MATCH (n:Person) RETURN n.name ORDER BY n.name");
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

        auto r1 = executor->executeSync("CREATE (a:Person)-[:KNOWS]->(b:Person)");
        ASSERT_TRUE(r1.error.empty()) << r1.error;

        // Verify before shutdown
        auto before = executor->executeSync("MATCH (a:Person)-[e:KNOWS]->(b:Person) RETURN a, b");
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

        auto result = executor->executeSync("MATCH (a:Person)-[e:KNOWS]->(b:Person) RETURN a, b");
        EXPECT_TRUE(result.error.empty()) << result.error;
        EXPECT_EQ(result.rows.size(), 1) << "Edge should persist across restart";

        auto vertices = executor->executeSync("MATCH (n:Person) RETURN n");
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
    auto result = executor_->executeSync("EXPLAIN MATCH (n:Person) RETURN n");
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
    auto result = executor_->executeSync("EXPLAIN MATCH (n) RETURN n");
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
    auto result = executor_->executeSync("EXPLAIN MATCH (n:Person) WHERE true RETURN n");
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
    auto result = executor_->executeSync("EXPLAIN MATCH (n:Person) RETURN n LIMIT 5");
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
    auto result = executor_->executeSync("EXPLAIN CREATE (n:Person {name: 'test'})");
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
    auto before = executor_->executeSync("MATCH (n:Person) RETURN n");
    size_t count_before = before.rows.size();

    executor_->executeSync("EXPLAIN CREATE (n:Person {name: 'Ghost'})");

    auto after = executor_->executeSync("MATCH (n:Person) RETURN n");
    EXPECT_EQ(after.rows.size(), count_before) << "EXPLAIN should not execute the query";
}

TEST_F(QueryExecutorTest, ExplainCaseInsensitive) {
    auto r1 = executor_->executeSync("explain MATCH (n:Person) RETURN n");
    auto r2 = executor_->executeSync("Explain MATCH (n:Person) RETURN n");
    auto r3 = executor_->executeSync("EXPLAIN MATCH (n:Person) RETURN n");
    EXPECT_TRUE(r1.error.empty()) << r1.error;
    EXPECT_TRUE(r2.error.empty()) << r2.error;
    EXPECT_TRUE(r3.error.empty()) << r3.error;
    EXPECT_EQ(r1.rows.size(), r2.rows.size());
    EXPECT_EQ(r2.rows.size(), r3.rows.size());
}

// ==================== M4: SKIP Tests ====================

TEST_F(QueryExecutorTest, SkipRows) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n SKIP 2");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 3);
}

TEST_F(QueryExecutorTest, SkipZero) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n SKIP 0");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 5);
}

TEST_F(QueryExecutorTest, SkipAll) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n SKIP 10");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0);
}

TEST_F(QueryExecutorTest, SkipWithLimit) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n SKIP 2 LIMIT 2");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2);
}

TEST_F(QueryExecutorTest, SkipWithOrderBy) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.id AS id ORDER BY id SKIP 1 LIMIT 2");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    // After ORDER BY id ASC, skip 1, limit 2: should get ids 2 and 3
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 2);
    EXPECT_EQ(std::get<int64_t>(result.rows[1][0]), 3);
}

// ==================== M4: ORDER BY Tests ====================

TEST_F(QueryExecutorTest, OrderByPropertyAsc) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.id AS id ORDER BY id ASC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 5);
    for (size_t i = 0; i < result.rows.size() - 1; ++i) {
        EXPECT_LE(std::get<int64_t>(result.rows[i][0]), std::get<int64_t>(result.rows[i + 1][0]));
    }
}

TEST_F(QueryExecutorTest, OrderByPropertyDesc) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.id AS id ORDER BY id DESC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 5);
    for (size_t i = 0; i < result.rows.size() - 1; ++i) {
        EXPECT_GE(std::get<int64_t>(result.rows[i][0]), std::get<int64_t>(result.rows[i + 1][0]));
    }
}

TEST_F(QueryExecutorTest, OrderByDefaultDirection) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.id AS id ORDER BY id");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 5);
    // Default should be ASC
    for (size_t i = 0; i < result.rows.size() - 1; ++i) {
        EXPECT_LE(std::get<int64_t>(result.rows[i][0]), std::get<int64_t>(result.rows[i + 1][0]));
    }
}

TEST_F(QueryExecutorTest, OrderByMultipleKeys) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.id AS id ORDER BY id ASC, id DESC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 5);
}

TEST_F(QueryExecutorTest, OrderByEmptyResult) {
    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.id AS id ORDER BY id");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0);
}

// ==================== M4: DISTINCT Tests ====================

TEST_F(QueryExecutorTest, DistinctProperty) {
    insertTestVertices();

    // All vertices have the same name pattern but unique names, test with a constant
    auto result = executor_->executeSync("MATCH (n:Person) RETURN DISTINCT 1 AS x");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 1);
}

TEST_F(QueryExecutorTest, DistinctNoDuplicate) {
    insertTestVertices();

    // All ids are unique, DISTINCT should keep all rows
    auto result = executor_->executeSync("MATCH (n:Person) RETURN DISTINCT n.id AS id");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 5);
}

TEST_F(QueryExecutorTest, DistinctWithOrderBy) {
    insertTestVertices();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN DISTINCT n.id AS id ORDER BY id ASC");
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
            props.resize(3); // prop_id 1=age, prop_id 2=city
            props[1] = PropertyValue(p.age);
            props[2] = PropertyValue(p.city);
            std::vector<std::pair<LabelId, Properties>> label_props = {{PERSON_LABEL, std::move(props)}};
            ASSERT_TRUE(sync_data_->insertVertex(txn, p.vid, label_props, nullptr));
        }
        ASSERT_TRUE(sync_data_->commitTransaction(txn));
    }
};

TEST_F(QueryExecutorAggTest, CountStar) {
    insertPersonData();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN count(*)");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 5);
}

TEST_F(QueryExecutorAggTest, CountByGroup) {
    insertPersonData();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.city AS city, count(*) AS cnt");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 2); // 2 cities
}

TEST_F(QueryExecutorAggTest, SumAge) {
    insertPersonData();
    auto res = executor_->executeSync("MATCH (n:Person) RETURN sum(n.age) AS total");
    ASSERT_TRUE(res.error.empty()) << "Sum error: " << res.error;
    ASSERT_EQ(res.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(res.rows[0][0]), 150);
}

TEST_F(QueryExecutorAggTest, AvgAge) {
    insertPersonData();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN avg(n.age) AS avg_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(result.rows[0][0]), 30.0); // 150/5
}

TEST_F(QueryExecutorAggTest, MinMaxAge) {
    insertPersonData();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN min(n.age) AS min_age, max(n.age) AS max_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 25);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 35);
}

TEST_F(QueryExecutorAggTest, CountDistinct) {
    insertPersonData();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN count(DISTINCT n.city) AS cities");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 2);
}

TEST_F(QueryExecutorAggTest, MultipleAggregates) {
    insertPersonData();

    auto result =
        executor_->executeSync("MATCH (n:Person) RETURN count(*) AS cnt, sum(n.age) AS total, avg(n.age) AS avg_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 5);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 150);
    EXPECT_DOUBLE_EQ(std::get<double>(result.rows[0][2]), 30.0);
}

TEST_F(QueryExecutorAggTest, AggregateWithGroupBy) {
    insertPersonData();

    auto result =
        executor_->executeSync("MATCH (n:Person) RETURN n.city AS city, count(*) AS cnt, avg(n.age) AS avg_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
}

TEST_F(QueryExecutorAggTest, AggregateEmptyResult) {
    auto result = executor_->executeSync("MATCH (n:Person) RETURN count(*) AS cnt");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(std::get<int64_t>(result.rows[0][0]), 0);
}

TEST_F(QueryExecutorAggTest, AggregateWithOrderBy) {
    insertPersonData();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.city AS city, count(*) AS cnt ORDER BY cnt DESC");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 2);
    // Beijing has 3, Shanghai has 2; DESC order → Beijing first
    EXPECT_EQ(std::get<int64_t>(result.rows[0][1]), 3);
    EXPECT_EQ(std::get<int64_t>(result.rows[1][1]), 2);
}

TEST_F(QueryExecutorAggTest, AggregateWithSkipLimit) {
    insertPersonData();

    auto result = executor_->executeSync("MATCH (n:Person) RETURN n.city AS city, count(*) AS cnt SKIP 1 LIMIT 2");
    ASSERT_TRUE(result.error.empty()) << result.error;
    // Only 2 groups, skip 1 → should get 1 row
    EXPECT_EQ(result.rows.size(), 1);
}
