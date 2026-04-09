#include <gtest/gtest.h>

#include "storage/graph_store_impl.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace eugraph;

class WTGraphStoreTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<GraphStoreImpl> store_;

    void SetUp() override {
        db_path_ = "/tmp/eugraph_wt_graph_test_" + std::to_string(::getpid()) + "_" +
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        store_ = std::make_unique<GraphStoreImpl>();
        ASSERT_TRUE(store_->open(db_path_));

        // Pre-create commonly used labels and edge labels
        ASSERT_TRUE(store_->createLabel(1));
        ASSERT_TRUE(store_->createLabel(2));
        ASSERT_TRUE(store_->createLabel(10));
        ASSERT_TRUE(store_->createLabel(11));
        ASSERT_TRUE(store_->createEdgeLabel(1));
        ASSERT_TRUE(store_->createEdgeLabel(2));
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(db_path_);
    }

    static Properties makeProps(std::initializer_list<std::pair<uint16_t, PropertyValue>> items) {
        Properties props;
        for (auto& [id, val] : items) {
            if (id >= props.size())
                props.resize(id + 1);
            props[id] = val;
        }
        return props;
    }

    GraphTxnHandle writeTxn() {
        auto txn = store_->beginTransaction();
        EXPECT_NE(txn, INVALID_GRAPH_TXN);
        return txn;
    }

    void commit(GraphTxnHandle txn) {
        EXPECT_TRUE(store_->commitTransaction(txn));
    }
};

// ==================== DDL ====================

TEST_F(WTGraphStoreTest, CreateAndDropLabel) {
    LabelId label_id = 100;
    ASSERT_TRUE(store_->createLabel(label_id));

    // Insert a vertex with this label
    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(txn, 1000, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}},
                                     nullptr));
    commit(txn);

    // Verify vertex is visible
    EXPECT_EQ(store_->countVerticesByLabel(INVALID_GRAPH_TXN, label_id), 1u);

    // Drop the label
    ASSERT_TRUE(store_->dropLabel(label_id));

    // Verify vertex scan returns empty (table dropped)
    EXPECT_EQ(store_->countVerticesByLabel(INVALID_GRAPH_TXN, label_id), 0u);

    // Re-create label should work
    ASSERT_TRUE(store_->createLabel(label_id));
    EXPECT_EQ(store_->countVerticesByLabel(INVALID_GRAPH_TXN, label_id), 0u);
}

TEST_F(WTGraphStoreTest, CreateAndDropEdgeLabel) {
    EdgeLabelId elabel_id = 100;
    ASSERT_TRUE(store_->createEdgeLabel(elabel_id));

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertEdge(txn, 2000, 1, 2, elabel_id, 0, Properties{}));
    commit(txn);

    // Drop the edge label
    ASSERT_TRUE(store_->dropEdgeLabel(elabel_id));

    // Edge properties and type index should be gone
    auto props = store_->getEdgeProperties(INVALID_GRAPH_TXN, elabel_id, 2000);
    EXPECT_FALSE(props.has_value());
}

TEST_F(WTGraphStoreTest, InsertFailsWithoutLabelCreation) {
    // Try to insert a vertex with a label that was never created
    auto txn = writeTxn();
    bool result = store_->insertVertex(txn, 9999, std::vector<std::pair<LabelId, Properties>>{{200, Properties{}}},
                                       nullptr);
    // Should fail because table doesn't exist
    EXPECT_FALSE(result);
    store_->rollbackTransaction(txn);
}

TEST_F(WTGraphStoreTest, DropLabelDoesNotAffectOtherLabels) {
    auto txn = writeTxn();
    auto props1 = makeProps({{0, std::string("under_label_1")}});
    auto props2 = makeProps({{0, std::string("under_label_2")}});

    ASSERT_TRUE(store_->insertVertex(txn, 500,
                                     std::vector<std::pair<LabelId, Properties>>{{1, props1}, {2, props2}}, nullptr));
    commit(txn);

    // Drop label 1
    ASSERT_TRUE(store_->dropLabel(1));

    // Vertex properties under label 2 should still be accessible
    auto result = store_->getVertexProperties(INVALID_GRAPH_TXN, 500, 2);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE((*result)[0].has_value());
    EXPECT_EQ(std::get<std::string>((*result)[0].value()), "under_label_2");

    // Vertex properties under label 1 should be gone
    auto result1 = store_->getVertexProperties(INVALID_GRAPH_TXN, 500, 1);
    EXPECT_FALSE(result1.has_value());
}

TEST_F(WTGraphStoreTest, DropLabelTombstoneFiltering) {
    auto txn = writeTxn();
    auto props = makeProps({{0, std::string("test")}});
    ASSERT_TRUE(store_->insertVertex(txn, 600, std::vector<std::pair<LabelId, Properties>>{{1, props}}, nullptr));
    commit(txn);

    // Drop label 1 (adds to deletedLabels_)
    ASSERT_TRUE(store_->dropLabel(1));

    // getVertexLabels should filter out label 1 (L| entry remains but is filtered)
    auto labels = store_->getVertexLabels(INVALID_GRAPH_TXN, 600);
    EXPECT_EQ(labels.size(), 0u);
}

// ==================== Vertex CRUD ====================

TEST_F(WTGraphStoreTest, InsertAndGetVertex) {
    LabelId label_id = 1;
    VertexId vid = 100;
    auto props = makeProps({{0, std::string("Alice")}, {1, int64_t(30)}});

    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{label_id, props}}, nullptr));
    commit(txn);

    auto result = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label_id);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    ASSERT_TRUE((*result)[0].has_value());
    EXPECT_EQ(std::get<std::string>((*result)[0].value()), "Alice");
    ASSERT_TRUE((*result)[1].has_value());
    EXPECT_EQ(std::get<int64_t>((*result)[1].value()), 30);
}

TEST_F(WTGraphStoreTest, InsertVertexWithPrimaryKey) {
    VertexId vid = 101;
    PropertyValue pk = std::string("alice@example.com");

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{1, Properties{}}}, &pk));
    commit(txn);

    auto found_vid = store_->getVertexIdByPk(pk);
    ASSERT_TRUE(found_vid.has_value());
    EXPECT_EQ(*found_vid, vid);

    auto found_pk = store_->getPkByVertexId(vid);
    ASSERT_TRUE(found_pk.has_value());
    EXPECT_EQ(std::get<std::string>(*found_pk), "alice@example.com");
}

TEST_F(WTGraphStoreTest, DeleteVertex) {
    LabelId label_id = 1;
    VertexId vid = 200;
    auto props = makeProps({{0, std::string("Bob")}});

    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{label_id, props}}, nullptr));
    commit(txn);

    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->deleteVertex(txn2, vid));
    commit(txn2);

    auto result = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label_id);
    EXPECT_FALSE(result.has_value());
}

TEST_F(WTGraphStoreTest, ScanVerticesByLabel) {
    LabelId label_id = 10;

    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, 500, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr));
    ASSERT_TRUE(
        store_->insertVertex(txn, 501, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr));
    ASSERT_TRUE(
        store_->insertVertex(txn, 502, std::vector<std::pair<LabelId, Properties>>{{11, Properties{}}}, nullptr));
    commit(txn);

    std::vector<VertexId> found;
    store_->scanVerticesByLabel(INVALID_GRAPH_TXN, label_id, [&](VertexId v) {
        found.push_back(v);
        return true;
    });

    EXPECT_EQ(found.size(), 2u);
    EXPECT_NE(std::find(found.begin(), found.end(), 500), found.end());
    EXPECT_NE(std::find(found.begin(), found.end(), 501), found.end());
}

// ==================== Edge CRUD ====================

TEST_F(WTGraphStoreTest, InsertAndGetEdge) {
    EdgeLabelId label_id = 1;
    EdgeId eid = 1000;
    auto props = makeProps({{0, int64_t(2020)}, {1, 0.8}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertEdge(txn, eid, 1, 2, label_id, 0, props));
    commit(txn);

    auto result = store_->getEdgeProperties(INVALID_GRAPH_TXN, label_id, eid);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    ASSERT_TRUE((*result)[0].has_value());
    EXPECT_EQ(std::get<int64_t>((*result)[0].value()), 2020);
    ASSERT_TRUE((*result)[1].has_value());
    EXPECT_DOUBLE_EQ(std::get<double>((*result)[1].value()), 0.8);
}

TEST_F(WTGraphStoreTest, ScanOutEdges) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 100, 1, 2, label_id, 0, Properties{});
    store_->insertEdge(txn, 101, 1, 3, label_id, 0, Properties{});
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdges(INVALID_GRAPH_TXN, 1, Direction::OUT, std::nullopt, [&](const IGraphStore::EdgeIndexEntry& e) {
        found.push_back(e.edge_id);
        return true;
    });

    EXPECT_EQ(found.size(), 2u);
}

// ==================== Transaction ====================

TEST_F(WTGraphStoreTest, TxnRollback) {
    VertexId vid = 701;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid, std::vector<std::pair<LabelId, Properties>>{{1, makeProps({{0, std::string("rollback_test")}})}},
        nullptr));
    ASSERT_TRUE(store_->rollbackTransaction(txn));

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, 1, 0);
    EXPECT_FALSE(val.has_value());
}

TEST_F(WTGraphStoreTest, TxnCommit) {
    VertexId vid = 700;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid, std::vector<std::pair<LabelId, Properties>>{{1, makeProps({{0, std::string("txn_test")}})}},
        nullptr));
    commit(txn);

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, 1, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(*val), "txn_test");
}
