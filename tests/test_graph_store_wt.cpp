#include <gtest/gtest.h>

#include "storage/graph_store_impl.hpp"
#include "storage/kv/wiredtiger_store.hpp"

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
        auto engine = std::make_unique<WiredTigerStore>();
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
