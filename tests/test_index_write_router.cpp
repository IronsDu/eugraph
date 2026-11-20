/// IndexWriteRouter 的单测（**不依赖 WiredTiger**）。
/// 把"构建中 / 收尾 / 已发布 / 终态"四种去向与准入失败钉死（设计 I2/I3/I7）。

#include <gtest/gtest.h>

#include "common/types/index_runtime.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/index/index_write_router.hpp"

#include <memory>
#include <vector>

using namespace eugraph;

namespace {

IndexRuntime::Descriptor makeDescriptor(uint32_t index_id, bool unique = false, bool is_edge = false) {
    IndexRuntime::Descriptor desc;
    desc.index_id = index_id;
    desc.label_id = 7;
    desc.is_edge = is_edge;
    desc.unique = unique;
    desc.index_table = "vidx_" + std::to_string(index_id);
    desc.delta_table = "idx_delta_" + std::to_string(index_id);
    return desc;
}

} // namespace

TEST(IndexWriteRouterTest, BuildingDestinationIsDeltaAndHoldsAdmission) {
    IndexRuntimeRegistry registry;
    auto runtime = registry.createOrUpdate(makeDescriptor(1)); // 默认 phase=SCAN
    IndexWriteRouter router(registry);

    auto decision = router.route(1);
    EXPECT_EQ(decision.destination, IndexWriteDestination::kDelta);
    EXPECT_EQ(decision.table, "idx_delta_1");
    ASSERT_TRUE(decision.guard.has_value());
    EXPECT_TRUE(decision.check_unique == false);
    EXPECT_TRUE(static_cast<bool>(decision));
    EXPECT_EQ(runtime->inflight(), 1u);

    decision.guard->reset();
    EXPECT_EQ(runtime->inflight(), 0u);
}

TEST(IndexWriteRouterTest, CatchupAlsoRoutesToDelta) {
    IndexRuntimeRegistry registry;
    auto runtime = registry.createOrUpdate(makeDescriptor(2));
    ASSERT_TRUE(runtime->transitionPhase(IndexPhase::kCatchup));
    IndexWriteRouter router(registry);

    auto decision = router.route(2);
    EXPECT_EQ(decision.destination, IndexWriteDestination::kDelta);
    EXPECT_EQ(decision.table, "idx_delta_2");
    ASSERT_TRUE(decision.guard.has_value());
}

TEST(IndexWriteRouterTest, FinalizeReturnsRetryLaterAndNeverIndexTable) {
    IndexRuntimeRegistry registry;
    auto runtime = registry.createOrUpdate(makeDescriptor(3));
    ASSERT_TRUE(runtime->transitionPhase(IndexPhase::kCatchup));
    ASSERT_TRUE(runtime->transitionPhase(IndexPhase::kFinalize));
    IndexWriteRouter router(registry);

    auto decision = router.route(3);
    EXPECT_EQ(decision.destination, IndexWriteDestination::kRetryLater);
    EXPECT_TRUE(decision.table.empty());
    EXPECT_FALSE(decision.guard.has_value());
    EXPECT_FALSE(static_cast<bool>(decision));
}

TEST(IndexWriteRouterTest, ClosedAdmissionDuringScanDegradesToRetryLater) {
    IndexRuntimeRegistry registry;
    auto runtime = registry.createOrUpdate(makeDescriptor(4));
    ASSERT_TRUE(runtime->closeAdmission());
    IndexWriteRouter router(registry);

    auto decision = router.route(4);
    // 关键：关闸后**不能**直写索引表，只能 RetryLater（I3）。
    EXPECT_EQ(decision.destination, IndexWriteDestination::kRetryLater);
    EXPECT_TRUE(decision.table.empty());
    EXPECT_FALSE(decision.guard.has_value());
}

TEST(IndexWriteRouterTest, PublicRoutesToIndexTableAndEnforcesUniqueOnlyWhenUnique) {
    IndexRuntimeRegistry registry;
    auto plain_desc = makeDescriptor(5, /*unique=*/false);
    plain_desc.initial_phase = IndexPhase::kPublic;
    registry.createOrUpdate(plain_desc);
    auto unique_desc = makeDescriptor(6, /*unique=*/true);
    unique_desc.initial_phase = IndexPhase::kPublic;
    registry.createOrUpdate(unique_desc);
    IndexWriteRouter router(registry);

    auto plain = router.route(5);
    EXPECT_EQ(plain.destination, IndexWriteDestination::kIndexTable);
    EXPECT_EQ(plain.table, "vidx_5");
    EXPECT_FALSE(plain.check_unique);
    EXPECT_FALSE(plain.guard.has_value());

    auto unique = router.route(6);
    EXPECT_EQ(unique.destination, IndexWriteDestination::kIndexTable);
    EXPECT_TRUE(unique.check_unique);
}

TEST(IndexWriteRouterTest, UniqueIndexWhileBuildingDoesNotCheckUniqueness) {
    IndexRuntimeRegistry registry;
    registry.createOrUpdate(makeDescriptor(7, /*unique=*/true));
    IndexWriteRouter router(registry);

    auto decision = router.route(7);
    EXPECT_EQ(decision.destination, IndexWriteDestination::kDelta);
    EXPECT_FALSE(decision.check_unique); // 构建期不强制唯一（§8.4）
}

TEST(IndexWriteRouterTest, DroppingPublicIndexIsStillMaintained) {
    IndexRuntimeRegistry registry;
    auto desc = makeDescriptor(8);
    desc.initial_phase = IndexPhase::kPublic;
    auto runtime = registry.createOrUpdate(desc);
    ASSERT_TRUE(runtime->transitionLifecycle(IndexLifecycle::kDropping));
    IndexWriteRouter router(registry);

    auto decision = router.route(8);
    EXPECT_EQ(decision.destination, IndexWriteDestination::kIndexTable); // 保护在飞长查询
    EXPECT_EQ(decision.table, "vidx_8");
}

TEST(IndexWriteRouterTest, PurgingAndErrorAndAbsentAreSkipped) {
    IndexRuntimeRegistry registry;
    auto desc = makeDescriptor(9);
    desc.initial_phase = IndexPhase::kPublic;
    auto purging = registry.createOrUpdate(desc);
    ASSERT_TRUE(purging->transitionLifecycle(IndexLifecycle::kPurging));

    auto error_desc = makeDescriptor(10);
    error_desc.initial_phase = IndexPhase::kError;
    registry.createOrUpdate(error_desc);

    IndexWriteRouter router(registry);
    EXPECT_EQ(router.route(9).destination, IndexWriteDestination::kSkip);
    EXPECT_EQ(router.route(10).destination, IndexWriteDestination::kSkip);
    EXPECT_EQ(router.route(999).destination, IndexWriteDestination::kSkip);
}

TEST(IndexWriteRouterTest, RouteAllAdmitsInAscendingIndexIdOrderAndDeduplicates) {
    IndexRuntimeRegistry registry;
    registry.createOrUpdate(makeDescriptor(30));
    registry.createOrUpdate(makeDescriptor(10));
    registry.createOrUpdate(makeDescriptor(20));
    IndexWriteRouter router(registry);

    auto decisions = router.routeAll({30, 10, 10, 20});
    ASSERT_EQ(decisions.size(), 3u);
    EXPECT_EQ(decisions[0].index_id, 10u);
    EXPECT_EQ(decisions[1].index_id, 20u);
    EXPECT_EQ(decisions[2].index_id, 30u);
    for (const auto& decision : decisions) {
        EXPECT_EQ(decision.destination, IndexWriteDestination::kDelta);
        EXPECT_TRUE(decision.guard.has_value());
    }
    // 每个索引恰好一个准入名额（去重后没有重复计数）。
    EXPECT_EQ(registry.find(10)->inflight(), 1u);
    EXPECT_EQ(registry.find(20)->inflight(), 1u);
    EXPECT_EQ(registry.find(30)->inflight(), 1u);
}

TEST(IndexWriteRouterTest, RouteAllKeepsOtherIndexesWhenOneIsClosing) {
    IndexRuntimeRegistry registry;
    registry.createOrUpdate(makeDescriptor(1));
    auto closing = registry.createOrUpdate(makeDescriptor(2));
    ASSERT_TRUE(closing->closeAdmission());
    IndexWriteRouter router(registry);

    auto decisions = router.routeAll({1, 2});
    ASSERT_EQ(decisions.size(), 2u);
    EXPECT_EQ(decisions[0].index_id, 1u);
    EXPECT_EQ(decisions[0].destination, IndexWriteDestination::kDelta);
    EXPECT_EQ(decisions[1].index_id, 2u);
    EXPECT_EQ(decisions[1].destination, IndexWriteDestination::kRetryLater);

    // 关闭中的索引不占名额；语句应整体重试（由 async 层保证）。
    EXPECT_EQ(closing->inflight(), 0u);
}
