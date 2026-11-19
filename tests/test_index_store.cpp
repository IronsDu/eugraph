#include <gtest/gtest.h>

#include "common/types/constants.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/index/index_delta_codec.hpp"
#include "storage/kv/index_key_codec.hpp"
#include "storage/kv/value_codec.hpp"
#include "test_sync_store.hpp"

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
        store_ = std::make_unique<test::TestSyncGraphDataStore>();
        ASSERT_TRUE(store_->open(db_path_));
    }

    void TearDown() override {
        store_->close();
        std::filesystem::remove_all(db_path_);
    }

    /// 索引条目操作现在都带事务句柄（与实体数据同事务）。测试里每次取一个新事务，
    /// 在测试结束时统一提交，语义与「写入后可见」一致。
    std::reference_wrapper<GraphSession> txn() {
        if (!txn_)
            txn_ = store_->beginTransaction();
        return *txn_;
    }

    void commit() {
        if (txn_) {
            store_->commitTransaction(*txn_);
            txn_.reset();
        }
    }

    std::string db_path_;
    std::unique_ptr<test::TestSyncGraphDataStore> store_;
    std::optional<std::reference_wrapper<GraphSession>> txn_;
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
    auto adj1 = ValueCodec::encodeEdgeAdjacency(10, 20, 1);
    auto adj2 = ValueCodec::encodeEdgeAdjacency(30, 40, 1);
    auto adj3 = ValueCodec::encodeEdgeAdjacency(10, 50, 1);

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
        EdgeLabelId lid = 0;
        ValueCodec::decodeEdgeAdjacency(val, src, dst, lid);
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

    auto adj = ValueCodec::encodeEdgeAdjacency(10, 20, 1);
    EXPECT_TRUE(store_->checkUniqueConstraint(txn(), table, int64_t(42)));
    EXPECT_TRUE(store_->insertIndexEntry(txn(), table, int64_t(42), 1, adj));
    EXPECT_FALSE(store_->checkUniqueConstraint(txn(), table, int64_t(42)));
    commit();
}

TEST_F(IndexStoreTest, EdgeIndexScanRange) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj = ValueCodec::encodeEdgeAdjacency(0, 0, 1);
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

    auto adj1 = ValueCodec::encodeEdgeAdjacency(10, 20, 1);
    auto adj2 = ValueCodec::encodeEdgeAdjacency(10, 20, 1);
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

    auto adj = ValueCodec::encodeEdgeAdjacency(10, 20, 1);
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

// ==================== 变更表（delta）：编解码 + 重放 ====================

TEST(IndexDeltaCodecTest, RoundTripsPutAndDelete) {
    const auto put = encodeIndexDeltaValue(IndexDeltaOp::kPut, "payload");
    ASSERT_EQ(put.size(), 8u);
    auto decoded = decodeIndexDeltaValue(put);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->op, IndexDeltaOp::kPut);
    EXPECT_EQ(decoded->payload, "payload");

    const auto del = encodeIndexDeltaValue(IndexDeltaOp::kDelete);
    ASSERT_EQ(del.size(), 1u);
    auto decoded_del = decodeIndexDeltaValue(del);
    ASSERT_TRUE(decoded_del.has_value());
    EXPECT_EQ(decoded_del->op, IndexDeltaOp::kDelete);
    EXPECT_TRUE(decoded_del->payload.empty());
}

TEST(IndexDeltaCodecTest, RejectsEmptyAndUnknownOp) {
    EXPECT_FALSE(decodeIndexDeltaValue({}).has_value());
    EXPECT_FALSE(decodeIndexDeltaValue(std::string_view("\x7f", 1)).has_value());
}

namespace {

std::vector<PropertyValue> propValues(int64_t value) {
    return std::vector<PropertyValue>{PropertyValue{value}};
}

} // namespace

TEST_F(IndexStoreTest, DeltaEntryIsDedupedByKeyAndWins) {
    const auto index_table = vidxTableById(1);
    const auto delta_table = indexDeltaTable(1);
    ASSERT_TRUE(store_->createIndex(index_table));
    ASSERT_TRUE(store_->createIndex(delta_table));

    auto txn = store_->beginTransaction();
    EXPECT_TRUE(store_->putDeltaEntry(txn, delta_table, propValues(10), 1, /*is_delete=*/false, "v1"));
    // 同一 (key, entity) 覆盖写：表里仍只有一行，值是最后一次。
    EXPECT_TRUE(store_->putDeltaEntry(txn, delta_table, propValues(10), 1, /*is_delete=*/false, "v2"));
    EXPECT_TRUE(store_->commitTransaction(txn));

    auto replay_txn = store_->beginTransaction();
    std::string last_key;
    EXPECT_EQ(store_->replayDeltaBatch(replay_txn, index_table, delta_table, 1024, {}, last_key), 1u);
    EXPECT_TRUE(store_->commitTransaction(replay_txn));

    std::vector<std::pair<uint64_t, std::string>> hits;
    auto read_txn = store_->beginTransaction();
    store_->scanIndexEqualityWithValue(read_txn, index_table, PropertyValue{int64_t(10)},
                                       [&](uint64_t entity_id, std::string_view value) {
                                           hits.emplace_back(entity_id, std::string(value));
                                           return true;
                                       });
    EXPECT_TRUE(store_->commitTransaction(read_txn));
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].first, 1u);
    EXPECT_EQ(hits[0].second, "v2");
}

TEST_F(IndexStoreTest, ReplayAppliesPutAndDeleteAndDrainsDelta) {
    const auto index_table = vidxTableById(1);
    const auto delta_table = indexDeltaTable(1);
    ASSERT_TRUE(store_->createIndex(index_table));
    ASSERT_TRUE(store_->createIndex(delta_table));

    // 先放一条基础条目（模拟回填已完成的部分）
    auto base = store_->beginTransaction();
    ASSERT_TRUE(store_->insertIndexEntry(base, index_table, propValues(10), 1, "base"));
    ASSERT_TRUE(store_->commitTransaction(base));

    auto delta_txn = store_->beginTransaction();
    ASSERT_TRUE(store_->putDeltaEntry(delta_txn, delta_table, propValues(20), 2, false, "p20"));
    ASSERT_TRUE(store_->putDeltaEntry(delta_txn, delta_table, propValues(10), 1, /*is_delete=*/true));
    ASSERT_TRUE(store_->commitTransaction(delta_txn));

    auto replay_txn = store_->beginTransaction();
    std::string last_key;
    EXPECT_EQ(store_->replayDeltaBatch(replay_txn, index_table, delta_table, 1024, {}, last_key), 2u);
    EXPECT_TRUE(store_->commitTransaction(replay_txn));

    // 变更表已排空：再重放一批必须是 0 行（不能靠计数器判定）。
    auto empty_txn = store_->beginTransaction();
    std::string last_key2;
    EXPECT_EQ(store_->replayDeltaBatch(empty_txn, index_table, delta_table, 1024, {}, last_key2), 0u);
    EXPECT_TRUE(store_->commitTransaction(empty_txn));

    std::vector<uint64_t> old_hits;
    std::vector<std::pair<uint64_t, std::string>> new_hits;
    auto read_txn = store_->beginTransaction();
    store_->scanIndexEquality(read_txn, index_table, PropertyValue{int64_t(10)}, [&](uint64_t id) {
        old_hits.push_back(id);
        return true;
    });
    store_->scanIndexEqualityWithValue(read_txn, index_table, PropertyValue{int64_t(20)},
                                       [&](uint64_t id, std::string_view value) {
                                           new_hits.emplace_back(id, std::string(value));
                                           return true;
                                       });
    EXPECT_TRUE(store_->commitTransaction(read_txn));
    EXPECT_TRUE(old_hits.empty()) << "DEL 必须从索引表删除旧键";
    ASSERT_EQ(new_hits.size(), 1u);
    EXPECT_EQ(new_hits[0].first, 2u);
    EXPECT_EQ(new_hits[0].second, "p20");
}

TEST_F(IndexStoreTest, ReplayContinuationUsesLastKey) {
    const auto index_table = vidxTableById(2);
    const auto delta_table = indexDeltaTable(2);
    ASSERT_TRUE(store_->createIndex(index_table));
    ASSERT_TRUE(store_->createIndex(delta_table));

    auto delta_txn = store_->beginTransaction();
    for (int64_t value = 1; value <= 5; ++value)
        ASSERT_TRUE(store_->putDeltaEntry(delta_txn, delta_table, propValues(value * 10), 1, false, ""));
    ASSERT_TRUE(store_->commitTransaction(delta_txn));

    auto replay = [&](size_t max_rows, const std::string& start) {
        auto txn = store_->beginTransaction();
        std::string last;
        const size_t applied = store_->replayDeltaBatch(txn, index_table, delta_table, max_rows, start, last);
        EXPECT_TRUE(store_->commitTransaction(txn));
        return std::make_pair(applied, last);
    };

    const auto first = replay(2, {});
    EXPECT_EQ(first.first, 2u);
    EXPECT_FALSE(first.second.empty());
    const auto second = replay(2, first.second);
    EXPECT_EQ(second.first, 2u);
    EXPECT_FALSE(second.second.empty());
    const auto third = replay(2, second.second);
    EXPECT_EQ(third.first, 1u);
    const auto fourth = replay(2, third.second);
    EXPECT_EQ(fourth.first, 0u);
}

TEST_F(IndexStoreTest, DeltaWriteRollsBackWithTransaction) {
    const auto index_table = vidxTableById(3);
    const auto delta_table = indexDeltaTable(3);
    ASSERT_TRUE(store_->createIndex(index_table));
    ASSERT_TRUE(store_->createIndex(delta_table));

    auto txn = store_->beginTransaction();
    ASSERT_TRUE(store_->putDeltaEntry(txn, delta_table, propValues(42), 7, false, ""));
    store_->rollbackTransaction(txn);

    auto replay_txn = store_->beginTransaction();
    std::string last;
    EXPECT_EQ(store_->replayDeltaBatch(replay_txn, index_table, delta_table, 1024, {}, last), 0u);
    EXPECT_TRUE(store_->commitTransaction(replay_txn));
}
