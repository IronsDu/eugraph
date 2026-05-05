#include <gtest/gtest.h>

#include "common/types/constants.hpp"
#include "storage/data/sync_graph_data_store.hpp"
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

    std::string db_path_;
    std::unique_ptr<SyncGraphDataStore> store_;
};

TEST_F(IndexStoreTest, CreateAndDropIndex) {
    auto table = vidxTable(1, 0);
    EXPECT_TRUE(store_->createIndex(table));
    EXPECT_TRUE(store_->dropIndex(table));
}

TEST_F(IndexStoreTest, InsertAndScanEquality) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    // Insert index entries
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(25), 1));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(30), 2));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(25), 3));

    // Scan for age = 25
    std::vector<uint64_t> results;
    auto txn = store_->beginTransaction();
    store_->scanIndexEquality(txn, table, int64_t(25), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], 1u);
    EXPECT_EQ(results[1], 3u);
}

TEST_F(IndexStoreTest, ScanRange) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(10), 1));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(20), 2));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(30), 3));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(40), 4));

    // Range scan: 15 < age < 35
    std::vector<uint64_t> results;
    auto txn = store_->beginTransaction();
    store_->scanIndexRange(txn, table, int64_t(15), int64_t(35), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], 2u);
    EXPECT_EQ(results[1], 3u);
}

TEST_F(IndexStoreTest, DeleteIndexEntry) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(25), 1));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(25), 2));

    // Delete one entry
    EXPECT_TRUE(store_->deleteIndexEntry(table, int64_t(25), 1));

    // Scan should only find entity 2
    std::vector<uint64_t> results;
    auto txn = store_->beginTransaction();
    store_->scanIndexEquality(txn, table, int64_t(25), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 2u);
}

TEST_F(IndexStoreTest, UniqueConstraint) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    // First insert — constraint satisfied
    EXPECT_TRUE(store_->checkUniqueConstraint(table, int64_t(42)));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(42), 1));

    // Second insert with same value — constraint violated
    EXPECT_FALSE(store_->checkUniqueConstraint(table, int64_t(42)));
}

TEST_F(IndexStoreTest, UniqueConstraintDifferentValues) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(10), 1));
    EXPECT_TRUE(store_->checkUniqueConstraint(table, int64_t(20)));
}

TEST_F(IndexStoreTest, StringIndexScan) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(table, std::string("alice"), 1));
    EXPECT_TRUE(store_->insertIndexEntry(table, std::string("bob"), 2));
    EXPECT_TRUE(store_->insertIndexEntry(table, std::string("alice"), 3));

    std::vector<uint64_t> results;
    auto txn = store_->beginTransaction();
    store_->scanIndexEquality(txn, table, std::string("alice"), [&](uint64_t entity_id) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
}

TEST_F(IndexStoreTest, DropAllEntries) {
    auto table = vidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(10), 1));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(20), 2));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(30), 3));

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
}

// ==================== Edge Index Tests ====================

TEST_F(IndexStoreTest, CreateAndDropEdgeIndex) {
    auto table = eidxTable(1, 0);
    EXPECT_TRUE(store_->createIndex(table));
    EXPECT_TRUE(store_->dropIndex(table));
}

TEST_F(IndexStoreTest, EdgeIndexInsertAndScanEquality) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    // Insert edge index entries with adjacency payloads
    auto adj1 = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    auto adj2 = ValueCodec::encodeEdgeAdjacency(30, 40, 0, 1);
    auto adj3 = ValueCodec::encodeEdgeAdjacency(10, 50, 0, 1);

    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(25), 1, adj1));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(30), 2, adj2));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(25), 3, adj3));

    // Scan for weight = 25 with values
    struct Result {
        uint64_t entity_id;
        VertexId src_id;
        VertexId dst_id;
    };
    std::vector<Result> results;
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
}

TEST_F(IndexStoreTest, EdgeIndexUniqueConstraint) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    EXPECT_TRUE(store_->checkUniqueConstraint(table, int64_t(42)));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(42), 1, adj));
    EXPECT_FALSE(store_->checkUniqueConstraint(table, int64_t(42)));
}

TEST_F(IndexStoreTest, EdgeIndexScanRange) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj = ValueCodec::encodeEdgeAdjacency(0, 0, 0, 1);
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(10), 1, adj));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(20), 2, adj));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(30), 3, adj));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(40), 4, adj));

    std::vector<uint64_t> results;
    auto txn = store_->beginTransaction();
    store_->scanIndexRangeWithValue(txn, table, int64_t(15), int64_t(35), [&](uint64_t entity_id, std::string_view) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], 2u);
    EXPECT_EQ(results[1], 3u);
}

TEST_F(IndexStoreTest, EdgeIndexDeleteEntry) {
    auto table = eidxTable(1, 0);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj1 = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    auto adj2 = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(25), 1, adj1));
    EXPECT_TRUE(store_->insertIndexEntry(table, int64_t(25), 2, adj2));

    EXPECT_TRUE(store_->deleteIndexEntry(table, int64_t(25), 1));

    std::vector<uint64_t> results;
    auto txn = store_->beginTransaction();
    store_->scanIndexEqualityWithValue(txn, table, int64_t(25), [&](uint64_t entity_id, std::string_view) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 2u);
}

TEST_F(IndexStoreTest, EdgeCompositeIndexInsertAndScan) {
    std::vector<uint16_t> prop_ids = {0, 1};
    auto table = eidxCompositeTable(1, prop_ids);
    ASSERT_TRUE(store_->createIndex(table));

    auto adj = ValueCodec::encodeEdgeAdjacency(10, 20, 0, 1);
    std::vector<PropertyValue> values = {int64_t(25), std::string("active")};
    EXPECT_TRUE(store_->insertIndexEntry(table, values, 1, adj));

    std::vector<uint64_t> results;
    auto txn = store_->beginTransaction();
    store_->scanIndexEqualityWithValue(txn, table, values, [&](uint64_t entity_id, std::string_view) {
        results.push_back(entity_id);
        return true;
    });
    store_->commitTransaction(txn);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u);
}
