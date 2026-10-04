#include <gtest/gtest.h>

#include "common/types/constants.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/index/index_delta_codec.hpp"
#include "storage/kv/index_key_codec.hpp"
#include "storage/kv/value_codec.hpp"

#include <filesystem>
#include <spdlog/spdlog.h>

using namespace eugraph;

namespace {
std::string makeTempDir(const std::string& suffix) {
    auto dir = std::filesystem::temp_directory_path() / ("eugraph_idx_test_" + suffix);
    std::filesystem::create_directories(dir);
    return dir.string();
}
} // namespace

class IndexStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = makeTempDir(testing::UnitTest::GetInstance()->current_test_info()->name());
        store_ = std::make_unique<SyncGraphDataStore>();
        ASSERT_TRUE(store_->open(db_path_));
    }

    void TearDown() override {
        store_->close();
        std::filesystem::remove_all(db_path_);
    }

    /// 索引条目操作现在都带事务句柄（与实体数据同事务）。测试里每次取一个新事务，
    /// 在测试结束时统一提交，语义与「写入后可见」一致。
    GraphTxnHandle txn() {
        if (txn_ == nullptr)
            txn_ = store_->beginTransaction();
        return txn_;
    }

    void commit() {
        if (txn_ != nullptr) {
            store_->commitTransaction(txn_);
            txn_ = nullptr;
        }
    }

    std::string db_path_;
    std::unique_ptr<SyncGraphDataStore> store_;
    GraphTxnHandle txn_ = nullptr;
};

TEST_F(IndexStoreTest, CreateAndDropIndex) {
    auto table = vidxTable(1, 0);
    EXPECT_TRUE(store_->createIndex(table));
    EXPECT_TRUE(store_->dropIndex(table));
    commit();
}

TEST_F(IndexStoreTest, InsertAndScanEquality) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    // Insert index entries
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(25), 1));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(30), 2));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(25), 3));

    // Scan for age = 25
    std::vector<uint64_t> results;
    // 索引扫描走独立快照，写入需先提交才能看到
    commit();
    auto txn = store_->beginTransaction();
    store_->scanIndexEquality(txn, table, int64_t(25), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], 1u);
    EXPECT_EQ(results[1], 3u);
    commit();
}

TEST_F(IndexStoreTest, ScanRange) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(10), 1));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(20), 2));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(30), 3));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(40), 4));

    // Range scan: 15 < age < 35
    std::vector<uint64_t> results;
    // 索引扫描走独立快照，写入需先提交才能看到
    commit();
    auto txn = store_->beginTransaction();
    store_->scanIndexRange(txn, table, int64_t(15), int64_t(35), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], 2u);
    EXPECT_EQ(results[1], 3u);
    commit();
}

TEST_F(IndexStoreTest, DeleteIndexEntry) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(25), 1));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(25), 2));

    // Delete one entry
    EXPECT_TRUE(store_->deleteIndexEntry(txn(), table, int64_t(25), 1));

    // Scan should only find entity 2
    std::vector<uint64_t> results;
    // 索引扫描走独立快照，写入需先提交才能看到
    commit();
    auto txn = store_->beginTransaction();
    store_->scanIndexEquality(txn, table, int64_t(25), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 2u);
    commit();
}

TEST_F(IndexStoreTest, UniqueConstraint) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    // First insert — constraint satisfied
    EXPECT_TRUE(store_->checkUniqueConstraint(txn(), table, int64_t(42)));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(42), 1));

    // Second insert with same value — constraint violated
    EXPECT_FALSE(store_->checkUniqueConstraint(txn(), table, int64_t(42)));
    commit();
}

TEST_F(IndexStoreTest, UniqueConstraintDifferentValues) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(10), 1));
    EXPECT_TRUE(store_->checkUniqueConstraint(txn(), table, int64_t(20)));
    commit();
}

TEST_F(IndexStoreTest, StringIndexScan) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, std::string("alice"), 1));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, std::string("bob"), 2));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, std::string("alice"), 3));

    std::vector<uint64_t> results;
    // 索引扫描走独立快照，写入需先提交才能看到
    commit();
    auto txn = store_->beginTransaction();
    store_->scanIndexEquality(txn, table, std::string("alice"), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
    commit();
}

TEST_F(IndexStoreTest, DropAllEntries) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(10), 1));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(20), 2));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(30), 3));

    // 先把写入提交，再做清空（否则清空作用在未提交数据上，行为未定义）
    commit();
    store_->dropAllIndexEntries(table);

    // Scan should find nothing
    std::vector<uint64_t> results;
    auto txn = store_->beginTransaction();
    store_->scanIndexEquality(txn, table, int64_t(10), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);
    EXPECT_TRUE(results.empty());
    commit();
}

// ==================== Edge Index Tests ====================

TEST_F(IndexStoreTest, CreateAndDropEdgeIndex) {
    auto table = eidxTable(1, 0);
    EXPECT_TRUE(store_->createIndex(table));
    EXPECT_TRUE(store_->dropIndex(table));
    commit();
}

TEST_F(IndexStoreTest, EdgeIndexInsertAndScanEquality) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    // Insert edge index entries with adjacency payloads
    auto adj1 = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    auto adj2 = ValueCodec::encodeEdgeAdjacency(30, 40, 0, 1);
    auto adj3 = ValueCodec::encodeEdgeAdjacency(10, 50, 0, 1);

    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(25), 1, adj1));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(30), 2, adj2));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(25), 3, adj3));

    // Scan for weight = 25 with values
    struct Result {
        uint64_t entity_id;
        VertexId src_id;
        VertexId dst_id;
    };
    std::vector<Result> results;
    // 索引扫描走独立快照，写入需先提交才能看到
    commit();
    auto txn = store_->beginTransaction();
    store_->scanIndexEqualityWithValue(txn, table, int64_t(25), [&](uint64_t entity_id, std::string_view val) {
        VertexId src = 0, dst = 0;
        uint64_t seq = 0;
        EdgeLabelId lid = 0;
        ValueCodec::decodeEdgeAdjacency(val, src, dst, seq, lid);
        results.push_back({entity_id, src, dst});
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].entity_id, 1u);
    EXPECT_EQ(results[0].src_id, 10u);
    EXPECT_EQ(results[0].dst_id, 20u);
    EXPECT_EQ(results[1].entity_id, 3u);
    EXPECT_EQ(results[1].src_id, 10u);
    EXPECT_EQ(results[1].dst_id, 50u);
    commit();
}

TEST_F(IndexStoreTest, EdgeIndexUniqueConstraint) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    EXPECT_TRUE(store_->checkUniqueConstraint(txn(), table, int64_t(42)));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(42), 1, adj));
    EXPECT_FALSE(store_->checkUniqueConstraint(txn(), table, int64_t(42)));
    commit();
}

TEST_F(IndexStoreTest, EdgeIndexScanRange) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj = ValueCodec::encodeEdgeAdjacency(0, 0, 0, 1);
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(10), 1, adj));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(20), 2, adj));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(30), 3, adj));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(40), 4, adj));

    std::vector<uint64_t> results;
    // 索引扫描走独立快照，写入需先提交才能看到
    commit();
    auto txn = store_->beginTransaction();
    store_->scanIndexRangeWithValue(txn, table, int64_t(15), int64_t(35), [&](uint64_t entity_id, std::string_view) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], 2u);
    EXPECT_EQ(results[1], 3u);
    commit();
}

TEST_F(IndexStoreTest, EdgeIndexDeleteEntry) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj1 = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    auto adj2 = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(25), 1, adj1));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(25), 2, adj2));

    EXPECT_TRUE(store_->deleteIndexEntry(txn(), table, int64_t(25), 1));

    std::vector<uint64_t> results;
    // 索引扫描走独立快照，写入需先提交才能看到
    commit();
    auto txn = store_->beginTransaction();
    store_->scanIndexEqualityWithValue(txn, table, int64_t(25), [&](uint64_t entity_id, std::string_view) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 2u);
    commit();
}

TEST_F(IndexStoreTest, EdgeCompositeIndexInsertAndScan) {
    std::vector<uint16_t> prop_ids = {0, 1};
    auto table = eidxCompositeTable(1, prop_ids);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    std::vector<PropertyValue> values = {int64_t(25), std::string("active")};
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, values, 1, adj));

    std::vector<uint64_t> results;
    // 索引扫描走独立快照，写入需先提交才能看到
    commit();
    auto txn = store_->beginTransaction();
    store_->scanIndexEqualityWithValue(txn, table, values, [&](uint64_t entity_id, std::string_view) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u);
    commit();
}

// ==================== 变更表（delta）原语：P2 第一步（设计 §5.0）====================
TEST_F(IndexStoreTest, DeltaTablePutOrderedResumableScanAndOpDecoding) {
    const auto table = idxDeltaTable(42);
    ASSERT_TRUE(store_->createIndex(table));

    struct Row {
        int64_t value;
        uint64_t entity;
        bool del;
        std::string payload;
    };
    // 故意乱序写入；并让 (20,2) 先 DEL 再 PUT ⇒ 验证同一 (键, 实体) **覆盖去重**（最终只应剩一条 PUT）
    const std::vector<Row> rows = {
        {30, 3, false, "adj-3"},  {10, 1, false, "adj-1"}, {20, 2, true, ""},
        {10, 2, false, "adj-1b"}, {40, 4, false, "adj-4"}, {20, 2, false, "adj-2-redone"},
    };
    for (const auto& r : rows) {
        ASSERT_TRUE(store_->putDeltaEntry(txn(), table, PropertyValue{static_cast<int64_t>(r.value)}, r.entity, r.del,
                                          r.payload));
    }
    commit();

    // 每批 2 条 ⇒ 必须靠**续扫**才能取完；顺序必须按 (值, 实体) 键序
    std::vector<std::pair<uint64_t, bool>> got; // (entity, is_delete)
    std::vector<std::string> payloads;
    std::string last_key;
    while (true) {
        size_t n = 0;
        std::string next_key;
        ASSERT_TRUE(store_->scanDeltaWithKey(
            txn(), table,
            [&](uint64_t /*entity_id*/, std::string_view /*key*/, std::string_view raw) {
                bool is_del = false;
                std::string_view payload;
                EXPECT_TRUE(decodeDeltaValue(raw, is_del, payload));
                got.emplace_back(0, is_del); // entity 由下面的断言单独取
                payloads.emplace_back(payload);
                ++n;
                return n < 2;
            },
            last_key, &next_key));
        if (n == 0)
            break;
        last_key = std::move(next_key);
        if (n < 2)
            break;
    }

    // 去重后应有 5 条（(20,2) 只保留最后一条 PUT）
    ASSERT_EQ(got.size(), 5u);
    // 键序：(10,1) (10,2) (20,2) (30,3) (40,4) —— 用 op 序列间接验证顺序（只有 (20,2) 曾被 DEL 覆盖）
    EXPECT_FALSE(got[0].second);
    EXPECT_FALSE(got[1].second);
    EXPECT_FALSE(got[2].second) << "(20,2) 应被后来的 PUT 覆盖（去重），不应还是 DEL";
    EXPECT_EQ(payloads[2], "adj-2-redone");
    EXPECT_FALSE(got[3].second);
    EXPECT_FALSE(got[4].second);
}
