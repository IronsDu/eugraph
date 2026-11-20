/// IndexWriteApplier 的集成单测（真实 WT 表 + 真实 GraphSession，不经过 DDL/Executor）。
/// 覆盖：BUILDING→delta、PUBLIC→索引表、unique 只在 PUBLIC 查、legacy 回退、
/// ERROR/PURGING 跳过，以及准入名额随事务结束释放。

#include <gtest/gtest.h>

#include "common/types/constants.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/index/index_maintenance.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/index/index_write_applier.hpp"
#include "storage/index/index_write_applier_sync.hpp"
#include "storage/io_scheduler.hpp"
#include "test_sync_store.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <folly/coro/BlockingWait.h>
#include <thread>

using namespace eugraph;
using namespace folly::coro;

namespace {

std::string makeTempDir(const std::string& suffix) {
    auto dir = std::filesystem::temp_directory_path() / ("eugraph_idx_applier_" + suffix);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

IndexRuntime::Descriptor makeDescriptor(IndexPhase phase, bool unique) {
    IndexRuntime::Descriptor desc;
    desc.index_id = 1;
    desc.label_id = 7;
    desc.is_edge = false;
    desc.unique = unique;
    desc.index_table = vidxTableById(1);
    desc.delta_table = indexDeltaTable(1);
    desc.initial_phase = phase;
    return desc;
}

IndexWriteItem makeItem(bool is_delete = false, bool unique = false) {
    IndexWriteItem item;
    item.index_id = 1;
    item.legacy_table = vidxTableById(1);
    item.values = {PropertyValue{int64_t(10)}};
    item.entity_id = 42;
    item.unique = unique;
    item.is_delete = is_delete;
    return item;
}

} // namespace

class IndexWriteApplierTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = makeTempDir(testing::UnitTest::GetInstance()->current_test_info()->name());
        store_ = std::make_unique<test::TestSyncGraphDataStore>();
        ASSERT_TRUE(store_->open(dir_));
        io_ = std::make_unique<IoScheduler>(1);
        registry_ = std::make_unique<IndexRuntimeRegistry>();
        ASSERT_TRUE(store_->createIndex(vidxTableById(1)));
        ASSERT_TRUE(store_->createIndex(indexDeltaTable(1)));
    }

    void TearDown() override {
        io_.reset();
        store_->close();
        std::filesystem::remove_all(dir_);
    }

    std::vector<uint64_t> scanIndex() {
        std::vector<uint64_t> ids;
        auto txn = store_->beginTransaction();
        store_->scanIndexEquality(txn, vidxTableById(1), PropertyValue{int64_t(10)}, [&](uint64_t id) {
            ids.push_back(id);
            return true;
        });
        EXPECT_TRUE(store_->commitTransaction(txn));
        return ids;
    }

    size_t replayDelta() {
        auto txn = store_->beginTransaction();
        std::string last;
        const size_t applied = store_->replayDeltaBatch(txn, vidxTableById(1), indexDeltaTable(1), 1024, {}, last);
        EXPECT_TRUE(store_->commitTransaction(txn));
        return applied;
    }

    std::string dir_;
    std::unique_ptr<test::TestSyncGraphDataStore> store_;
    std::unique_ptr<IoScheduler> io_;
    std::unique_ptr<IndexRuntimeRegistry> registry_;
};

TEST_F(IndexWriteApplierTest, BuildingWritesDeltaAndBindsAdmissionToTransaction) {
    auto runtime = registry_->createOrUpdate(makeDescriptor(IndexPhase::kScan, /*unique=*/false));
    ASSERT_NE(runtime, nullptr);

    auto txn = store_->beginTransaction();
    AsyncGraphDataStore async(*store_, *io_, txn.get());
    async.setIndexRegistry(registry_.get());

    const auto status = blockingWait(applyIndexWrite(async, makeItem()));
    EXPECT_EQ(status, IndexWriteStatus::kApplied);
    EXPECT_EQ(runtime->inflight(), 1u); // 名额挂在本事务上
    EXPECT_TRUE(txn.get().hasIndexAdmission(1));
    EXPECT_TRUE(scanIndex().empty()); // 构建期不写索引表
    ASSERT_TRUE(store_->commitTransaction(txn));
    EXPECT_EQ(runtime->inflight(), 0u); // commit 后释放

    EXPECT_EQ(replayDelta(), 1u); // 追赶后进索引
    EXPECT_EQ(scanIndex().size(), 1u);
}

TEST_F(IndexWriteApplierTest, PublicWritesIndexDirectlyAndDeltaStaysEmpty) {
    auto runtime = registry_->createOrUpdate(makeDescriptor(IndexPhase::kPublic, /*unique=*/false));

    auto txn = store_->beginTransaction();
    AsyncGraphDataStore async(*store_, *io_, txn.get());
    async.setIndexRegistry(registry_.get());
    EXPECT_EQ(blockingWait(applyIndexWrite(async, makeItem())), IndexWriteStatus::kApplied);
    EXPECT_EQ(runtime->inflight(), 0u); // PUBLIC 不需要准入
    ASSERT_TRUE(store_->commitTransaction(txn));

    EXPECT_EQ(scanIndex().size(), 1u);
    EXPECT_EQ(replayDelta(), 0u);
}

TEST_F(IndexWriteApplierTest, DeleteUsesSameDestination) {
    registry_->createOrUpdate(makeDescriptor(IndexPhase::kPublic, /*unique=*/false));

    auto insert_txn = store_->beginTransaction();
    AsyncGraphDataStore insert_store(*store_, *io_, insert_txn.get());
    insert_store.setIndexRegistry(registry_.get());
    ASSERT_EQ(blockingWait(applyIndexWrite(insert_store, makeItem())), IndexWriteStatus::kApplied);
    ASSERT_TRUE(store_->commitTransaction(insert_txn));
    ASSERT_EQ(scanIndex().size(), 1u);

    auto delete_txn = store_->beginTransaction();
    AsyncGraphDataStore delete_store(*store_, *io_, delete_txn.get());
    delete_store.setIndexRegistry(registry_.get());
    EXPECT_EQ(blockingWait(applyIndexWrite(delete_store, makeItem(/*is_delete=*/true))), IndexWriteStatus::kApplied);
    ASSERT_TRUE(store_->commitTransaction(delete_txn));
    EXPECT_TRUE(scanIndex().empty());
}

TEST_F(IndexWriteApplierTest, UniqueIsCheckedOnlyAfterPublic) {
    // 构建期：重复键都进 delta，不报冲突
    registry_->createOrUpdate(makeDescriptor(IndexPhase::kScan, /*unique=*/true));
    {
        auto txn = store_->beginTransaction();
        AsyncGraphDataStore async(*store_, *io_, txn.get());
        async.setIndexRegistry(registry_.get());
        EXPECT_EQ(blockingWait(applyIndexWrite(async, makeItem(false, /*unique=*/true))), IndexWriteStatus::kApplied);
        ASSERT_TRUE(store_->commitTransaction(txn));
    }
    EXPECT_EQ(replayDelta(), 1u); // 同一 (key,entity) 去重

    // PUBLIC 后：重复键被拒
    auto runtime = registry_->find(1);
    ASSERT_TRUE(runtime->transitionPhase(IndexPhase::kCatchup));
    ASSERT_TRUE(runtime->transitionPhase(IndexPhase::kFinalize));
    ASSERT_TRUE(runtime->transitionPhase(IndexPhase::kPublic));

    auto txn = store_->beginTransaction();
    AsyncGraphDataStore async(*store_, *io_, txn.get());
    async.setIndexRegistry(registry_.get());
    EXPECT_EQ(blockingWait(applyIndexWrite(async, makeItem(false, /*unique=*/true))),
              IndexWriteStatus::kUniqueConflict);
    ASSERT_TRUE(store_->commitTransaction(txn));
}

TEST_F(IndexWriteApplierTest, ErrorAndPurgingAreSkipped) {
    auto error_runtime = registry_->createOrUpdate(makeDescriptor(IndexPhase::kError, /*unique=*/false));
    ASSERT_NE(error_runtime, nullptr);
    auto txn = store_->beginTransaction();
    AsyncGraphDataStore async(*store_, *io_, txn.get());
    async.setIndexRegistry(registry_.get());
    EXPECT_EQ(blockingWait(applyIndexWrite(async, makeItem())), IndexWriteStatus::kSkipped);
    ASSERT_TRUE(store_->commitTransaction(txn));

    auto public_runtime = registry_->createOrUpdate([] {
        auto d = makeDescriptor(IndexPhase::kPublic, false);
        d.index_id = 2;
        d.index_table = vidxTableById(2);
        d.delta_table = indexDeltaTable(2);
        return d;
    }());
    ASSERT_TRUE(store_->createIndex(vidxTableById(2)));
    ASSERT_TRUE(public_runtime->transitionLifecycle(IndexLifecycle::kDropping));
    ASSERT_TRUE(public_runtime->transitionLifecycle(IndexLifecycle::kPurging));

    auto txn2 = store_->beginTransaction();
    AsyncGraphDataStore async2(*store_, *io_, txn2.get());
    async2.setIndexRegistry(registry_.get());
    auto item = makeItem();
    item.index_id = 2;
    item.legacy_table = vidxTableById(2);
    EXPECT_EQ(blockingWait(applyIndexWrite(async2, item)), IndexWriteStatus::kSkipped);
    ASSERT_TRUE(store_->commitTransaction(txn2));
}

TEST_F(IndexWriteApplierTest, LegacyFallbackWithoutRegistryWritesIndexTable) {
    auto txn = store_->beginTransaction();
    AsyncGraphDataStore async(*store_, *io_, txn.get()); // 不注入 registry
    EXPECT_EQ(blockingWait(applyIndexWrite(async, makeItem())), IndexWriteStatus::kApplied);
    ASSERT_TRUE(store_->commitTransaction(txn));

    EXPECT_EQ(scanIndex().size(), 1u);
    EXPECT_EQ(replayDelta(), 0u);
}

// ==================== 同步路径（批量装载） ====================

TEST_F(IndexWriteApplierTest, SyncBuildingRoutesToDeltaAndBindsGuard) {
    auto runtime = registry_->createOrUpdate(makeDescriptor(IndexPhase::kScan, /*unique=*/false));
    store_->setIndexRegistry(registry_.get());

    auto txn = store_->beginTransaction();
    EXPECT_EQ(applyIndexWriteSync(*store_, txn.get(), registry_.get(), makeItem()), IndexWriteStatus::kApplied);
    EXPECT_EQ(runtime->inflight(), 1u);
    EXPECT_TRUE(txn.get().hasIndexAdmission(1));
    ASSERT_TRUE(store_->commitTransaction(txn));
    EXPECT_EQ(runtime->inflight(), 0u);

    EXPECT_EQ(replayDelta(), 1u);
    EXPECT_EQ(scanIndex().size(), 1u);
}

TEST_F(IndexWriteApplierTest, SyncPublicWritesIndexTable) {
    registry_->createOrUpdate(makeDescriptor(IndexPhase::kPublic, /*unique=*/false));
    store_->setIndexRegistry(registry_.get());

    auto txn = store_->beginTransaction();
    EXPECT_EQ(applyIndexWriteSync(*store_, txn.get(), registry_.get(), makeItem()), IndexWriteStatus::kApplied);
    ASSERT_TRUE(store_->commitTransaction(txn));

    EXPECT_EQ(scanIndex().size(), 1u);
    EXPECT_EQ(replayDelta(), 0u);
}

TEST_F(IndexWriteApplierTest, BatchHelperUsesRegistryRouting) {
    auto runtime = registry_->createOrUpdate(makeDescriptor(IndexPhase::kScan, /*unique=*/false));
    store_->setIndexRegistry(registry_.get());

    std::vector<VertexIndexEntry> entries;
    VertexIndexEntry entry;
    entry.table = vidxTableById(1);
    entry.values = {PropertyValue{int64_t(10)}};
    entry.vid = 42;
    entry.unique = false;
    entry.index_id = 1;
    entries.push_back(entry);

    auto txn = store_->beginTransaction();
    EXPECT_EQ(insertVertexIndexEntries(*store_, txn.get(), entries), 0);
    EXPECT_EQ(runtime->inflight(), 1u); // 批量事务持有准入名额
    ASSERT_TRUE(store_->commitTransaction(txn));
    EXPECT_EQ(runtime->inflight(), 0u);

    EXPECT_EQ(replayDelta(), 1u); // 构建期写入落在 delta
    EXPECT_EQ(scanIndex().size(), 1u);
}

TEST_F(IndexWriteApplierTest, SyncLegacyWithoutRegistryWritesIndexTable) {
    auto txn = store_->beginTransaction();
    EXPECT_EQ(applyIndexWriteSync(*store_, txn.get(), nullptr, makeItem()), IndexWriteStatus::kApplied);
    ASSERT_TRUE(store_->commitTransaction(txn));

    EXPECT_EQ(scanIndex().size(), 1u);
    EXPECT_EQ(replayDelta(), 0u);
}

TEST_F(IndexWriteApplierTest, AdmittedTransactionContinuesAfterGateCloses) {
    auto runtime = registry_->createOrUpdate(makeDescriptor(IndexPhase::kScan, /*unique=*/false));
    auto txn = store_->beginTransaction();
    AsyncGraphDataStore async(*store_, *io_, txn.get());
    async.setIndexRegistry(registry_.get());

    ASSERT_EQ(blockingWait(applyIndexWrite(async, makeItem())), IndexWriteStatus::kApplied);
    ASSERT_TRUE(runtime->closeAdmission()); // 模拟 FINALIZE 关闸

    // 同一事务已持有该索引准入：后续写入必须继续写 delta，不能等闸门（否则
    // FINALIZE 等事务、事务等 FINALIZE 死锁）。
    auto second = makeItem();
    second.entity_id = 43;
    EXPECT_EQ(blockingWait(applyIndexWrite(async, second)), IndexWriteStatus::kApplied);
    EXPECT_EQ(runtime->inflight(), 1u);

    ASSERT_TRUE(store_->commitTransaction(txn));
    EXPECT_EQ(runtime->inflight(), 0u);
    EXPECT_EQ(replayDelta(), 2u);
}

TEST_F(IndexWriteApplierTest, UnadmittedWriterWaitsForGateReopen) {
    registry_->createOrUpdate(makeDescriptor(IndexPhase::kScan, /*unique=*/false));
    auto runtime = registry_->find(1);
    ASSERT_TRUE(runtime->closeAdmission());

    auto txn = store_->beginTransaction();
    AsyncGraphDataStore async(*store_, *io_, txn.get());
    async.setIndexRegistry(registry_.get());

    std::atomic<bool> done{false};
    IndexWriteStatus status = IndexWriteStatus::kRetryLater;
    std::thread waiter([&] {
        status = blockingWait(applyIndexWrite(async, makeItem()));
        done.store(true);
    });
    // 超过旧的 1s 超时：新实现必须继续等待，而不是返回 RetryLater。
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_FALSE(done.load());
    runtime->reopenAdmission();
    waiter.join();
    EXPECT_TRUE(done.load());
    EXPECT_EQ(status, IndexWriteStatus::kApplied);
    ASSERT_TRUE(store_->commitTransaction(txn));
}

TEST_F(IndexWriteApplierTest, NewIndexDuringAdmittedTransactionFailsFastWithoutDeadlock) {
    auto runtime1 = registry_->createOrUpdate(makeDescriptor(IndexPhase::kScan, /*unique=*/false));
    ASSERT_NE(runtime1, nullptr);

    IndexRuntime::Descriptor second_desc = makeDescriptor(IndexPhase::kScan, /*unique=*/false);
    second_desc.index_id = 2;
    second_desc.index_table = vidxTableById(2);
    second_desc.delta_table = indexDeltaTable(2);
    ASSERT_TRUE(store_->createIndex(vidxTableById(2)));
    ASSERT_TRUE(store_->createIndex(indexDeltaTable(2)));
    auto runtime2 = registry_->createOrUpdate(second_desc);
    ASSERT_NE(runtime2, nullptr);

    auto txn = store_->beginTransaction();
    AsyncGraphDataStore async(*store_, *io_, txn.get());
    async.setIndexRegistry(registry_.get());
    ASSERT_EQ(blockingWait(applyIndexWrite(async, makeItem())), IndexWriteStatus::kApplied);
    ASSERT_TRUE(runtime2->closeAdmission());

    auto second = makeItem();
    second.index_id = 2;
    second.legacy_table = vidxTableById(2);
    second.entity_id = 44;
    // 已持有 index 1 的准入 ⇒ 遇到 index 2 关闸必须快速失败，不能等（否则死锁）。
    EXPECT_EQ(blockingWait(applyIndexWrite(async, second)), IndexWriteStatus::kRetryLater);
    ASSERT_TRUE(store_->commitTransaction(txn));
    EXPECT_EQ(runtime1->inflight(), 0u);
}

TEST_F(IndexWriteApplierTest, SyncBatchContinuesAfterGateClosesWhenAdmitted) {
    auto runtime = registry_->createOrUpdate(makeDescriptor(IndexPhase::kScan, /*unique=*/false));
    auto txn = store_->beginTransaction();
    ASSERT_EQ(applyIndexWriteSync(*store_, txn.get(), registry_.get(), makeItem()), IndexWriteStatus::kApplied);
    ASSERT_TRUE(runtime->closeAdmission());

    auto second = makeItem();
    second.entity_id = 43;
    EXPECT_EQ(applyIndexWriteSync(*store_, txn.get(), registry_.get(), second), IndexWriteStatus::kApplied);
    ASSERT_TRUE(store_->commitTransaction(txn));
    EXPECT_EQ(replayDelta(), 2u);
}
