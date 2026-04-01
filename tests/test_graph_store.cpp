#include <gtest/gtest.h>

#include "storage/graph_store_impl.hpp"
#include "storage/kv/rocksdb_store.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

using namespace eugraph;

class GraphStoreTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<GraphStoreImpl> store_;

    void SetUp() override {
        db_path_ = "/tmp/eugraph_test_" + std::to_string(::getpid()) + "_" +
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        auto engine = std::make_unique<RocksDBStore>();
        store_ = std::make_unique<GraphStoreImpl>(std::move(engine));
        ASSERT_TRUE(store_->open(db_path_));
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(db_path_);
    }

    static Properties makeProps(std::initializer_list<std::pair<uint16_t, PropertyValue>> items) {
        Properties props;
        for (auto& [id, val] : items) {
            if (id >= props.size()) props.resize(id + 1);
            props[id] = val;
        }
        return props;
    }

    // Write in txn, commit
    GraphTxnHandle writeTxn() {
        auto txn = store_->beginTransaction();
        EXPECT_NE(txn, INVALID_GRAPH_TXN);
        return txn;
    }

    void commit(GraphTxnHandle txn) {
        EXPECT_TRUE(store_->commitTransaction(txn));
    }
};

// ==================== Vertex CRUD ====================

TEST_F(GraphStoreTest, InsertAndGetVertex) {
    LabelId label_id = 1;
    VertexId vid = 100;
    auto props = makeProps({{0, std::string("Alice")}, {1, int64_t(30)}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{label_id, props}},
        nullptr));
    commit(txn);

    auto result = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label_id);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    ASSERT_TRUE((*result)[0].has_value());
    EXPECT_EQ(std::get<std::string>((*result)[0].value()), "Alice");
    ASSERT_TRUE((*result)[1].has_value());
    EXPECT_EQ(std::get<int64_t>((*result)[1].value()), 30);
}

TEST_F(GraphStoreTest, InsertVertexWithPrimaryKey) {
    VertexId vid = 101;
    PropertyValue pk = std::string("alice@example.com");

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{1, Properties{}}},
        &pk));
    commit(txn);

    auto found_vid = store_->getVertexIdByPk(pk);
    ASSERT_TRUE(found_vid.has_value());
    EXPECT_EQ(*found_vid, vid);

    auto found_pk = store_->getPkByVertexId(vid);
    ASSERT_TRUE(found_pk.has_value());
    EXPECT_EQ(std::get<std::string>(*found_pk), "alice@example.com");
}

TEST_F(GraphStoreTest, InsertVertexWithoutPrimaryKey) {
    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, 102,
        std::vector<std::pair<LabelId, Properties>>{{1, Properties{}}},
        nullptr));
    commit(txn);

    auto result = store_->getVertexIdByPk(std::string("nonexistent"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(GraphStoreTest, DeleteVertex) {
    LabelId label_id = 1;
    VertexId vid = 200;
    auto props = makeProps({{0, std::string("Bob")}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{label_id, props}},
        nullptr));
    commit(txn);

    auto txn2 = writeTxn();
    ASSERT_TRUE(store_->deleteVertex(txn2, vid));
    commit(txn2);

    auto result = store_->getVertexProperties(INVALID_GRAPH_TXN, vid, label_id);
    EXPECT_FALSE(result.has_value());

    auto labels = store_->getVertexLabels(INVALID_GRAPH_TXN, vid);
    EXPECT_TRUE(labels.empty());
}

TEST_F(GraphStoreTest, DeleteVertexWithLabels) {
    LabelId label_id = 1;
    VertexId vid = 201;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}},
        nullptr));
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

// ==================== Vertex Properties ====================

TEST_F(GraphStoreTest, PutAndGetVertexProperty) {
    VertexId vid = 300;
    LabelId label_id = 1;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}},
        nullptr));
    ASSERT_TRUE(store_->putVertexProperty(txn, vid, label_id, 0, std::string("Charlie")));
    commit(txn);

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, label_id, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(*val), "Charlie");
}

TEST_F(GraphStoreTest, DeleteVertexProperty) {
    VertexId vid = 301;
    LabelId label_id = 1;
    auto props = makeProps({{0, std::string("Dave")}, {1, int64_t(25)}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{label_id, props}},
        nullptr));
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

TEST_F(GraphStoreTest, UpdateVertexProperty) {
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

TEST_F(GraphStoreTest, MultiLabelProperties) {
    VertexId vid = 303;
    LabelId label1 = 1, label2 = 2;
    auto props1 = makeProps({{0, std::string("Alice")}});
    auto props2 = makeProps({{0, int64_t(999)}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{label1, props1}, {label2, props2}},
        nullptr));
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

// ==================== Vertex Labels ====================

TEST_F(GraphStoreTest, GetVertexLabels) {
    VertexId vid = 400;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{1, Properties{}}, {2, Properties{}}},
        nullptr));
    commit(txn);

    auto labels = store_->getVertexLabels(INVALID_GRAPH_TXN, vid);
    EXPECT_EQ(labels.size(), 2u);
    EXPECT_TRUE(labels.contains(1));
    EXPECT_TRUE(labels.contains(2));
}

TEST_F(GraphStoreTest, AddVertexLabel) {
    VertexId vid = 401;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{1, Properties{}}},
        nullptr));
    ASSERT_TRUE(store_->addVertexLabel(txn, vid, 2));
    commit(txn);

    auto labels = store_->getVertexLabels(INVALID_GRAPH_TXN, vid);
    EXPECT_EQ(labels.size(), 2u);
    EXPECT_TRUE(labels.contains(2));
}

TEST_F(GraphStoreTest, RemoveVertexLabel) {
    VertexId vid = 402;
    LabelId label1 = 1, label2 = 2;
    auto props1 = makeProps({{0, std::string("under_label1")}});
    auto props2 = makeProps({{0, int64_t(42)}});

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{label1, props1}, {label2, props2}},
        nullptr));
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

TEST_F(GraphStoreTest, ScanVerticesByLabel) {
    LabelId label_id = 10;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, 500, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr));
    ASSERT_TRUE(store_->insertVertex(
        txn, 501, std::vector<std::pair<LabelId, Properties>>{{label_id, Properties{}}}, nullptr));
    ASSERT_TRUE(store_->insertVertex(
        txn, 502, std::vector<std::pair<LabelId, Properties>>{{11, Properties{}}}, nullptr));
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

TEST_F(GraphStoreTest, InsertAndGetEdge) {
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

TEST_F(GraphStoreTest, DeleteEdge) {
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
    store_->scanEdges(INVALID_GRAPH_TXN, 1, Direction::OUT, label_id, [&](const IGraphStore::EdgeIndexEntry& e) {
        found.push_back(e.edge_id);
        return true;
    });
    EXPECT_TRUE(found.empty());
}

TEST_F(GraphStoreTest, EdgePropertyCRUD) {
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

TEST_F(GraphStoreTest, ScanOutEdges) {
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
    EXPECT_NE(std::find(found.begin(), found.end(), 100), found.end());
    EXPECT_NE(std::find(found.begin(), found.end(), 101), found.end());
}

TEST_F(GraphStoreTest, ScanInEdges) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 200, 1, 5, label_id, 0, Properties{});
    store_->insertEdge(txn, 201, 2, 5, label_id, 0, Properties{});
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdges(INVALID_GRAPH_TXN, 5, Direction::IN, std::nullopt, [&](const IGraphStore::EdgeIndexEntry& e) {
        found.push_back(e.edge_id);
        return true;
    });

    EXPECT_EQ(found.size(), 2u);
    EXPECT_NE(std::find(found.begin(), found.end(), 200), found.end());
    EXPECT_NE(std::find(found.begin(), found.end(), 201), found.end());
}

TEST_F(GraphStoreTest, ScanEdgesWithLabelFilter) {
    auto txn = writeTxn();
    store_->insertEdge(txn, 300, 1, 2, 1, 0, Properties{});  // label 1
    store_->insertEdge(txn, 301, 1, 3, 2, 0, Properties{});  // label 2
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdges(INVALID_GRAPH_TXN, 1, Direction::OUT, 1, [&](const IGraphStore::EdgeIndexEntry& e) {
        found.push_back(e.edge_id);
        return true;
    });

    EXPECT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], 300);
}

TEST_F(GraphStoreTest, MultipleEdgesSamePair) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 400, 1, 2, label_id, 1, Properties{});  // seq=1
    store_->insertEdge(txn, 401, 1, 2, label_id, 2, Properties{});  // seq=2
    store_->insertEdge(txn, 402, 1, 2, label_id, 3, Properties{});  // seq=3
    commit(txn);

    std::vector<EdgeId> found;
    store_->scanEdges(INVALID_GRAPH_TXN, 1, Direction::OUT, label_id, [&](const IGraphStore::EdgeIndexEntry& e) {
        found.push_back(e.edge_id);
        return true;
    });

    EXPECT_EQ(found.size(), 3u);
}

// ==================== Statistics ====================

TEST_F(GraphStoreTest, CountVerticesByLabel) {
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

TEST_F(GraphStoreTest, CountDegree) {
    auto txn = writeTxn();
    store_->insertEdge(txn, 500, 1, 2, 1, 0, Properties{});
    store_->insertEdge(txn, 501, 1, 3, 1, 0, Properties{});
    store_->insertEdge(txn, 502, 1, 4, 2, 0, Properties{});
    commit(txn);

    EXPECT_EQ(store_->countDegree(INVALID_GRAPH_TXN, 1, Direction::OUT, std::nullopt), 3u);
    EXPECT_EQ(store_->countDegree(INVALID_GRAPH_TXN, 1, Direction::OUT, 1), 2u);
    EXPECT_EQ(store_->countDegree(INVALID_GRAPH_TXN, 2, Direction::IN, std::nullopt), 1u);
}

// ==================== Transaction ====================

TEST_F(GraphStoreTest, TxnCommit) {
    VertexId vid = 700;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{1, makeProps({{0, std::string("txn_test")}})}},
        nullptr));
    commit(txn);

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, 1, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(*val), "txn_test");
}

TEST_F(GraphStoreTest, TxnRollback) {
    VertexId vid = 701;

    auto txn = writeTxn();
    ASSERT_TRUE(store_->insertVertex(
        txn, vid,
        std::vector<std::pair<LabelId, Properties>>{{1, makeProps({{0, std::string("rollback_test")}})}},
        nullptr));
    ASSERT_TRUE(store_->rollbackTransaction(txn));

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, 1, 0);
    EXPECT_FALSE(val.has_value());
}

TEST_F(GraphStoreTest, TxnVisibility) {
    VertexId vid = 702;
    ASSERT_TRUE(store_->putVertexProperty(
        INVALID_GRAPH_TXN, vid, 1, 0, std::string("no_txn")));

    auto val = store_->getVertexProperty(INVALID_GRAPH_TXN, vid, 1, 0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(*val), "no_txn");
}

// ==================== Edge Traversal Neighbor Info ====================

TEST_F(GraphStoreTest, ScanEdgesReturnsNeighborInfo) {
    EdgeLabelId label_id = 1;

    auto txn = writeTxn();
    store_->insertEdge(txn, 600, 10, 20, label_id, 0, Properties{});
    commit(txn);

    std::vector<IGraphStore::EdgeIndexEntry> entries;
    store_->scanEdges(INVALID_GRAPH_TXN, 10, Direction::OUT, std::nullopt, [&](const IGraphStore::EdgeIndexEntry& e) {
        entries.push_back(e);
        return true;
    });

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].neighbor_id, 20u);
    EXPECT_EQ(entries[0].edge_label_id, label_id);
    EXPECT_EQ(entries[0].seq, 0u);
    EXPECT_EQ(entries[0].edge_id, 600u);
}
