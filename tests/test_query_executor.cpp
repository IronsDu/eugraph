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
        executor_->setLabelMappings(
            {{"Person", PERSON_LABEL}, {"City", CITY_LABEL}},
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
    store_->commitTransaction(txn);
    EXPECT_EQ(count, 1);
}

// ==================== Empty Results ====================

TEST_F(QueryExecutorTest, EmptyScan) {
    // No vertices exist
    auto rows = executor_->executeSync("MATCH (n:Person) RETURN n");
    EXPECT_EQ(rows.size(), 0);
}
