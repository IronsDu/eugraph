#include <gtest/gtest.h>

#include "storage/data/sync_graph_data_store.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace eugraph;

class WTGraphStoreTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<SyncGraphDataStore> store_;

    void SetUp() override {
        db_path_ = "/tmp/eugraph_wt_graph_test_" + std::to_string(::getpid()) + "_" +
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        store_ = std::make_unique<SyncGraphDataStore>();
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
    bool result =
        store_->insertVertex(txn, 9999, std::vector<std::pair<LabelId, Properties>>{{200, Properties{}}}, nullptr);
    // Should fail because table doesn't exist
    EXPECT_FALSE(result);
    store_->rollbackTransaction(txn);
}

TEST_F(WTGraphStoreTest, DropLabelDoesNotAffectOtherLabels) {
    auto txn = writeTxn();
    auto props1 = makeProps({{0, std::string("under_label_1")}});
    auto props2 = makeProps({{0, std::string("under_label_2")}});

    ASSERT_TRUE(
        store_->insertVertex(txn, 500, std::vector<std::pair<LabelId, Properties>>{{1, props1}, {2, props2}}, nullptr));
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

    // Drop label 1 (drops the per-label tables)
    ASSERT_TRUE(store_->dropLabel(1));

    // In the new architecture, getVertexLabels returns raw labels from the
    // label_fwd table. Tombstone filtering is handled by the async layer
    // via GraphSchema, not by the sync data store.
    // After dropping label 1, the label_fwd entry may still exist but
    // per-label property tables are gone.
    auto props_result = store_->getVertexProperties(INVALID_GRAPH_TXN, 600, 1);
    EXPECT_FALSE(props_result.has_value());
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
    store_->scanEdges(INVALID_GRAPH_TXN, 1, Direction::OUT, std::nullopt,
                      [&](const ISyncGraphDataStore::EdgeIndexEntry& e) {
                          found.push_back(e.edge_id);
                          return true;
                      });

    EXPECT_EQ(found.size(), 2u);
}

// ==================== Vertex Properties ====================

TEST_F(WTGraphStoreTest, InsertVertexWithoutPrimaryKey) {
    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, 102, std::vector<std::pair<LabelId, Properties>>{{1, Properties{}}}, nullptr));
    commit(txn);

    auto result = store_->getVertexIdByPk(std::string("nonexistent"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(WTGraphStoreTest, DeleteVertexWithLabels) {
    LabelId label_id = 1;
    VertexId vid = 201;

    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr));
    commit(txn);

    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->deleteVertex(txn2, vid));
    commit(txn2);

    std::vector<VertexId> found;
    store_->scanVerticesByLabel(INVALID_GRAPH_TXN, label_id, [&](VertexId v) {
        found.push_back(v);
        return true;
    });
    EXPECT_TRUE(found.empty());
}

TEST_F(WTGraphStoreTest, PutAndGetVertexProperty) {
    VertexId vid = 300;
    LabelId label_id = 1;

    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr));
    ASSERT_TRUE(store_->putVertexProperty(txn, vid, label_id, 0, std::string("Charlie")));
    commit(txn);

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, label_id, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(*val), "Charlie");
}

TEST_F(WTGraphStoreTest, DeleteVertexProperty) {
    VertexId vid = 301;
    LabelId label_id = 1;
    auto props = makeProps({{0, std::string("Dave")}, {1, int64_t(25)}});

    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{label_id, props}}, nullptr));
    commit(txn);

    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->deleteVertexProperty(txn2, vid, label_id, 0));
    commit(txn2);

    auto deleted = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, label_id, 0);
    EXPECT_FALSE(deleted.has_value());

    auto remaining = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, label_id, 1);
    ASSERT_TRUE(remaining.has_value());
    EXPECT_EQ(std::get<int64_t>(*remaining), 25);
}

TEST_F(WTGraphStoreTest, UpdateVertexProperty) {
    VertexId vid = 302;
    LabelId label_id = 1;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->putVertexProperty(txn, vid, label_id, 0, int64_t(10)));
    ASSERT_TRUE(store_->putVertexProperty(txn, vid, label_id, 0, int64_t(20)));
    commit(txn);

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, label_id, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<int64_t>(*val), 20);
}

TEST_F(WTGraphStoreTest, MultiLabelProperties) {
    VertexId vid = 303;
    LabelId label1 = 1, label2 = 2;
    auto props1 = makeProps({{0, std::string("Alice")}});
    auto props2 = makeProps({{0, int64_t(999)}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid, std::vector<std::pair<LabelId, Properties>>{{label1, props1}, {label2, props2}}, nullptr));
    commit(txn);

    auto r1 = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label1);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE((*r1)[0].has_value());
    EXPECT_EQ(std::get<std::string>((*r1)[0].value()), "Alice");

    auto r2 = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label2);
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE((*r2)[0].has_value());
    EXPECT_EQ(std::get<int64_t>((*r2)[0].value()), 999);
}

TEST_F(WTGraphStoreTest, InsertVertexWithoutLabels) {
    VertexId vid = 199;
    PropertyValue pk = std::string("bare@example.com");

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(txn, vid, {}, &pk));
    commit(txn);

    // No labels
    auto labels = store_->getVertexLabels(INVALID_GRAPH_TXN, vid);
    EXPECT_TRUE(labels.empty());

    // No properties
    auto props = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, 1);
    EXPECT_FALSE(props.has_value());

    // Primary key still works
    auto found_vid = store_->getVertexIdByPk(pk);
    ASSERT_TRUE(found_vid.has_value());
    EXPECT_EQ(*found_vid, vid);

    // Can add label afterwards
    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->addVertexLabel(txn2, vid, 1));
    ASSERT_TRUE(store_->putVertexProperty(txn2, vid, 1, 0, std::string("late_prop")));
    commit(txn2);

    labels = store_->getVertexLabels(INVALID_GRAPH_TXN, vid);
    EXPECT_EQ(labels.size(), 1u);
    EXPECT_TRUE(labels.contains(1));

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, 1, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(*val), "late_prop");
}

TEST_F(WTGraphStoreTest, PutVertexProperties) {
    VertexId vid = 310;
    LabelId label_id = 1;

    // Insert with label but no properties
    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr));
    // Batch-set properties
    auto props = makeProps({{0, std::string("Eve")}, {1, int64_t(28)}, {2, 3.14}});
    ASSERT_TRUE(store_->putVertexProperties(txn, vid, label_id, props));
    commit(txn);

    auto result = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label_id);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3u);
    ASSERT_TRUE((*result)[0].has_value());
    EXPECT_EQ(std::get<std::string>((*result)[0].value()), "Eve");
    ASSERT_TRUE((*result)[1].has_value());
    EXPECT_EQ(std::get<int64_t>((*result)[1].value()), 28);
    ASSERT_TRUE((*result)[2].has_value());
    EXPECT_DOUBLE_EQ(std::get<double>((*result)[2].value()), 3.14);
}

TEST_F(WTGraphStoreTest, PutVertexPropertiesOverwrite) {
    VertexId vid = 311;
    LabelId label_id = 1;
    auto orig = makeProps({{0, std::string("old_name")}, {1, int64_t(10)}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{label_id, orig}}, nullptr));
    // Overwrite with new values
    auto updated = makeProps({{0, std::string("new_name")}, {1, int64_t(99)}});
    ASSERT_TRUE(store_->putVertexProperties(txn, vid, label_id, updated));
    commit(txn);

    auto result = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label_id);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE((*result)[0].has_value());
    EXPECT_EQ(std::get<std::string>((*result)[0].value()), "new_name");
    ASSERT_TRUE((*result)[1].has_value());
    EXPECT_EQ(std::get<int64_t>((*result)[1].value()), 99);
}

// ==================== Vertex Labels ====================

TEST_F(WTGraphStoreTest, GetVertexLabels) {
    VertexId vid = 400;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid, std::vector<std::pair<LabelId, Properties>>{{1, Properties{}}, {2, Properties{}}}, nullptr));
    commit(txn);

    auto labels = store_->getVertexLabels(INVALID_GRAPH_TXN, vid);
    EXPECT_EQ(labels.size(), 2u);
    EXPECT_TRUE(labels.contains(1));
    EXPECT_TRUE(labels.contains(2));
}

TEST_F(WTGraphStoreTest, AddVertexLabel) {
    VertexId vid = 401;

    auto txn = writeTxn();
    ASSERT_TRUE(
        store_->insertVertex(txn, vid, std::vector<std::pair<LabelId, Properties>>{{1, Properties{}}}, nullptr));
    ASSERT_TRUE(store_->addVertexLabel(txn, vid, 2));
    commit(txn);

    auto labels = store_->getVertexLabels(INVALID_GRAPH_TXN, vid);
    EXPECT_EQ(labels.size(), 2u);
    EXPECT_TRUE(labels.contains(2));
}

TEST_F(WTGraphStoreTest, RemoveVertexLabel) {
    VertexId vid = 402;
    LabelId label1 = 1, label2 = 2;
    auto props1 = makeProps({{0, std::string("under_label1")}});
    auto props2 = makeProps({{0, int64_t(42)}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid, std::vector<std::pair<LabelId, Properties>>{{label1, props1}, {label2, props2}}, nullptr));
    commit(txn);

    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->removeVertexLabel(txn2, vid, label1));
    commit(txn2);

    auto labels = store_->getVertexLabels(INVALID_GRAPH_TXN, vid);
    EXPECT_EQ(labels.size(), 1u);
    EXPECT_TRUE(labels.contains(label2));

    auto r1 = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label1);
    EXPECT_FALSE(r1.has_value());

    auto r2 = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label2);
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE((*r2)[0].has_value());
    EXPECT_EQ(std::get<int64_t>((*r2)[0].value()), 42);
}

// ==================== Edge CRUD ====================

TEST_F(WTGraphStoreTest, DeleteEdge) {
    EdgeLabelId label_id = 1;
    EdgeId eid = 1001;
    auto props = makeProps({{0, int64_t(2021)}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertEdge(txn, eid, 1, 2, label_id, 0, props));
    commit(txn);

    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->deleteEdge(txn2, eid, label_id, 1, 2, 0));
    commit(txn2);

    auto result = store_->getEdgeProperties(INVALID_GRAPH_TXN, label_id, eid);
    EXPECT_FALSE(result.has_value());

    std::vector<EdgeId> found;
    store_->scanEdges(INVALID_GRAPH_TXN, 1, Direction::OUT, label_id,
                      [&](const ISyncGraphDataStore::EdgeIndexEntry& e) {
                          found.push_back(e.edge_id);
                          return true;
                      });
    EXPECT_TRUE(found.empty());
}

TEST_F(WTGraphStoreTest, EdgePropertyCRUD) {
    EdgeLabelId label_id = 1;
    EdgeId eid = 1002;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertEdge(txn, eid, 1, 2, label_id, 0, Properties{}));
    ASSERT_TRUE(store_->putEdgeProperty(txn, label_id, eid, 0, std::string("active")));
    commit(txn);

    auto val = store_->getEdgeProperty(INVALID_GRAPH_TXN, label_id, eid, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(*val), "active");

    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->deleteEdgeProperty(txn2, label_id, eid, 0));
    commit(txn2);

    auto deleted = store_->getEdgeProperty(INVALID_GRAPH_TXN, label_id, eid, 0);
    EXPECT_FALSE(deleted.has_value());
}

// ==================== Edge Traversal ====================

TEST_F(WTGraphStoreTest, ScanInEdges) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 200, 1, 5, label_id, 0, Properties{});
    store_->insertEdge(txn, 201, 2, 5, label_id, 0, Properties{});
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdges(INVALID_GRAPH_TXN, 5, Direction::IN, std::nullopt,
                      [&](const ISyncGraphDataStore::EdgeIndexEntry& e) {
                          found.push_back(e.edge_id);
                          return true;
                      });

    EXPECT_EQ(found.size(), 2u);
    EXPECT_NE(std::find(found.begin(), found.end(), 200), found.end());
    EXPECT_NE(std::find(found.begin(), found.end(), 201), found.end());
}

TEST_F(WTGraphStoreTest, ScanEdgesWithLabelFilter) {
    auto txn = writeTxn();
    store_->insertEdge(txn, 300, 1, 2, 1, 0, Properties{}); // label 1
    store_->insertEdge(txn, 301, 1, 3, 2, 0, Properties{}); // label 2
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdges(INVALID_GRAPH_TXN, 1, Direction::OUT, 1, [&](const ISyncGraphDataStore::EdgeIndexEntry& e) {
        found.push_back(e.edge_id);
        return true;
    });

    EXPECT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], 300);
}

TEST_F(WTGraphStoreTest, MultipleEdgesSamePair) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 400, 1, 2, label_id, 1, Properties{}); // seq=1
    store_->insertEdge(txn, 401, 1, 2, label_id, 2, Properties{}); // seq=2
    store_->insertEdge(txn, 402, 1, 2, label_id, 3, Properties{}); // seq=3
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdges(INVALID_GRAPH_TXN, 1, Direction::OUT, label_id,
                      [&](const ISyncGraphDataStore::EdgeIndexEntry& e) {
                          found.push_back(e.edge_id);
                          return true;
                      });

    EXPECT_EQ(found.size(), 3u);
}

// ==================== Statistics ====================

TEST_F(WTGraphStoreTest, CountVerticesByLabel) {
    // Create extra labels needed for this test
    ASSERT_TRUE(store_->createLabel(20));
    ASSERT_TRUE(store_->createLabel(21));

    LabelId label_id = 20;

    auto txn = writeTxn();
    store_->insertVertex(txn, 600, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr);
    store_->insertVertex(txn, 601, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr);
    store_->insertVertex(txn, 602, std::vector<std::pair<LabelId, Properties>>{{21, Properties{}}}, nullptr);
    commit(txn);

    EXPECT_EQ(store_->countVerticesByLabel(INVALID_GRAPH_TXN, label_id), 2u);
    EXPECT_EQ(store_->countVerticesByLabel(INVALID_GRAPH_TXN, 21), 1u);
    EXPECT_EQ(store_->countVerticesByLabel(INVALID_GRAPH_TXN, 99), 0u);
}

TEST_F(WTGraphStoreTest, CountDegree) {
    auto txn = writeTxn();
    store_->insertEdge(txn, 500, 1, 2, 1, 0, Properties{});
    store_->insertEdge(txn, 501, 1, 3, 1, 0, Properties{});
    store_->insertEdge(txn, 502, 1, 4, 2, 0, Properties{});
    commit(txn);

    EXPECT_EQ(store_->countDegree(INVALID_GRAPH_TXN, 1, Direction::OUT, std::nullopt), 3u);
    EXPECT_EQ(store_->countDegree(INVALID_GRAPH_TXN, 1, Direction::OUT, 1), 2u);
    EXPECT_EQ(store_->countDegree(INVALID_GRAPH_TXN, 2, Direction::IN, std::nullopt), 1u);
}

// ==================== Edge Type Index Scan ====================

TEST_F(WTGraphStoreTest, ScanEdgesByType) {
    EdgeLabelId label1 = 1, label2 = 2;

    auto txn = writeTxn();
    store_->insertEdge(txn, 700, 1, 2, label1, 0, Properties{});
    store_->insertEdge(txn, 701, 3, 4, label1, 0, Properties{});
    store_->insertEdge(txn, 702, 5, 6, label2, 0, Properties{});
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdgesByType(INVALID_GRAPH_TXN, label1, std::nullopt, std::nullopt,
                            [&](const ISyncGraphDataStore::EdgeTypeIndexEntry& e) {
                                found.push_back(e.edge_id);
                                return true;
                            });

    EXPECT_EQ(found.size(), 2u);
    EXPECT_NE(std::find(found.begin(), found.end(), 700), found.end());
    EXPECT_NE(std::find(found.begin(), found.end(), 701), found.end());
}

TEST_F(WTGraphStoreTest, ScanEdgesByTypeWithSrcFilter) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 800, 10, 20, label_id, 0, Properties{});
    store_->insertEdge(txn, 801, 10, 30, label_id, 0, Properties{});
    store_->insertEdge(txn, 802, 40, 50, label_id, 0, Properties{});
    commit(txn);

    std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> found;
    store_->scanEdgesByType(INVALID_GRAPH_TXN, label_id, VertexId(10), std::nullopt,
                            [&](const ISyncGraphDataStore::EdgeTypeIndexEntry& e) {
                                found.push_back(e);
                                return true;
                            });

    EXPECT_EQ(found.size(), 2u);
    for (auto& e : found) {
        EXPECT_EQ(e.src_vertex_id, 10u);
    }
}

TEST_F(WTGraphStoreTest, ScanEdgesByTypeWithSrcDstFilter) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 900, 10, 20, label_id, 1, Properties{});
    store_->insertEdge(txn, 901, 10, 20, label_id, 2, Properties{});
    store_->insertEdge(txn, 902, 10, 30, label_id, 0, Properties{});
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdgesByType(INVALID_GRAPH_TXN, label_id, VertexId(10), VertexId(20),
                            [&](const ISyncGraphDataStore::EdgeTypeIndexEntry& e) {
                                found.push_back(e.edge_id);
                                return true;
                            });

    EXPECT_EQ(found.size(), 2u);
    EXPECT_NE(std::find(found.begin(), found.end(), 900), found.end());
    EXPECT_NE(std::find(found.begin(), found.end(), 901), found.end());
}

TEST_F(WTGraphStoreTest, ScanEdgesByTypeAfterDelete) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 950, 1, 2, label_id, 0, Properties{});
    commit(txn);

    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->deleteEdge(txn2, 950, label_id, 1, 2, 0));
    commit(txn2);

    std::vector<EdgeId> found;
    store_->scanEdgesByType(INVALID_GRAPH_TXN, label_id, std::nullopt, std::nullopt,
                            [&](const ISyncGraphDataStore::EdgeTypeIndexEntry& e) {
                                found.push_back(e.edge_id);
                                return true;
                            });
    EXPECT_TRUE(found.empty());
}

TEST_F(WTGraphStoreTest, ScanEdgesByTypeReturnsEntryInfo) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 960, 10, 20, label_id, 5, Properties{});
    commit(txn);

    std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> found;
    store_->scanEdgesByType(INVALID_GRAPH_TXN, label_id, std::nullopt, std::nullopt,
                            [&](const ISyncGraphDataStore::EdgeTypeIndexEntry& e) {
                                found.push_back(e);
                                return true;
                            });

    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].src_vertex_id, 10u);
    EXPECT_EQ(found[0].dst_vertex_id, 20u);
    EXPECT_EQ(found[0].seq, 5u);
    EXPECT_EQ(found[0].edge_id, 960u);
}

TEST_F(WTGraphStoreTest, ScanEdgesReturnsNeighborInfo) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 600, 10, 20, label_id, 0, Properties{});
    commit(txn);

    std::vector<ISyncGraphDataStore::EdgeIndexEntry> entries;
    store_->scanEdges(INVALID_GRAPH_TXN, 10, Direction::OUT, std::nullopt,
                      [&](const ISyncGraphDataStore::EdgeIndexEntry& e) {
                          entries.push_back(e);
                          return true;
                      });

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].neighbor_id, 20u);
    EXPECT_EQ(entries[0].edge_label_id, label_id);
    EXPECT_EQ(entries[0].seq, 0u);
    EXPECT_EQ(entries[0].edge_id, 600u);
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

TEST_F(WTGraphStoreTest, TxnVisibility) {
    VertexId vid = 702;
    ASSERT_TRUE(store_->putVertexProperty(INVALID_GRAPH_TXN, vid, 1, 0, std::string("no_txn")));

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, 1, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(*val), "no_txn");
}
