#include <gtest/gtest.h>

#include "storage/data/sync_graph_data_store.hpp"
#include "storage/kv/index_key_codec.hpp"
#include "common/types/constants.hpp"

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
