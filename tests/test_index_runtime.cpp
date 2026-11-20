/// 索引运行时模型与注册表的单测（**不依赖 WiredTiger**）。
/// 覆盖设计 §4/§5/§10 的判定与准入协议（T1/T2 的纯逻辑部分）。

#include <gtest/gtest.h>

#include "common/schema/schema_compat.hpp"
#include "common/types/index_runtime.hpp"
#include "storage/index/index_runtime_factory.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/index/index_test_hooks.hpp"
#include "storage/session/schema_generation_tracker.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

using namespace eugraph;

namespace {

IndexRuntime::Descriptor makeDescriptor(uint32_t index_id, bool unique = false, bool is_edge = false,
                                        uint32_t label_id = 7) {
    IndexRuntime::Descriptor desc;
    desc.index_id = index_id;
    desc.label_id = label_id;
    desc.is_edge = is_edge;
    desc.unique = unique;
    desc.index_table = "vidx_" + std::to_string(index_id);
    desc.delta_table = "idx_delta_" + std::to_string(index_id);
    return desc;
}

} // namespace

// ==================== 纯状态模型 ====================

TEST(IndexRuntimeModelTest, PhaseTransitionsFollowDesign) {
    // 合法路径
    EXPECT_TRUE(indexPhaseTransitionAllowed(IndexPhase::kAbsent, IndexPhase::kScan));
    EXPECT_TRUE(indexPhaseTransitionAllowed(IndexPhase::kScan, IndexPhase::kCatchup));
    EXPECT_TRUE(indexPhaseTransitionAllowed(IndexPhase::kCatchup, IndexPhase::kFinalize));
    EXPECT_TRUE(indexPhaseTransitionAllowed(IndexPhase::kFinalize, IndexPhase::kPublic));
    EXPECT_TRUE(indexPhaseTransitionAllowed(IndexPhase::kFinalize, IndexPhase::kCatchup)); // 排空超时回退
    EXPECT_TRUE(indexPhaseTransitionAllowed(IndexPhase::kFinalize, IndexPhase::kError));
    // 非法/终态
    EXPECT_FALSE(indexPhaseTransitionAllowed(IndexPhase::kScan, IndexPhase::kPublic)); // 不能跳过追赶
    EXPECT_FALSE(indexPhaseTransitionAllowed(IndexPhase::kPublic, IndexPhase::kError));
    EXPECT_FALSE(indexPhaseTransitionAllowed(IndexPhase::kError, IndexPhase::kScan));
    EXPECT_FALSE(indexPhaseTransitionAllowed(IndexPhase::kScan, IndexPhase::kScan)); // 同态不是迁移
}

TEST(IndexRuntimeModelTest, LifecycleTransitionsFollowDesign) {
    EXPECT_TRUE(indexLifecycleTransitionAllowed(IndexLifecycle::kActive, IndexLifecycle::kDropping));
    EXPECT_TRUE(indexLifecycleTransitionAllowed(IndexLifecycle::kActive, IndexLifecycle::kPurging));
    EXPECT_TRUE(indexLifecycleTransitionAllowed(IndexLifecycle::kDropping, IndexLifecycle::kPendingPurge));
    EXPECT_TRUE(indexLifecycleTransitionAllowed(IndexLifecycle::kDropping, IndexLifecycle::kPurging));
    EXPECT_TRUE(indexLifecycleTransitionAllowed(IndexLifecycle::kPendingPurge, IndexLifecycle::kPurging));
    EXPECT_TRUE(indexLifecycleTransitionAllowed(IndexLifecycle::kPurging, IndexLifecycle::kPurged));
    EXPECT_FALSE(indexLifecycleTransitionAllowed(IndexLifecycle::kPurged, IndexLifecycle::kPurging));
    EXPECT_FALSE(indexLifecycleTransitionAllowed(IndexLifecycle::kActive, IndexLifecycle::kPendingPurge));
}

TEST(IndexRuntimeModelTest, PlannerSelectableRequiresPublicActiveAndGeneration) {
    // PUBLIC+ACTIVE，且索引在事务开始前就可用 ⇒ 可选
    EXPECT_TRUE(indexPlannerSelectable(IndexPhase::kPublic, IndexLifecycle::kActive, 5, 5));
    EXPECT_TRUE(indexPlannerSelectable(IndexPhase::kPublic, IndexLifecycle::kActive, 4, 5));
    // 事务开始后才 PUBLIC ⇒ 不可选（"新索引 + 旧数据快照"漏行）
    EXPECT_FALSE(indexPlannerSelectable(IndexPhase::kPublic, IndexLifecycle::kActive, 6, 5));
    // 非 PUBLIC / 非 ACTIVE ⇒ 不可选
    EXPECT_FALSE(indexPlannerSelectable(IndexPhase::kScan, IndexLifecycle::kActive, 0, 5));
    EXPECT_FALSE(indexPlannerSelectable(IndexPhase::kCatchup, IndexLifecycle::kActive, 0, 5));
    EXPECT_FALSE(indexPlannerSelectable(IndexPhase::kError, IndexLifecycle::kActive, 0, 5));
    EXPECT_FALSE(indexPlannerSelectable(IndexPhase::kPublic, IndexLifecycle::kDropping, 1, 5));
    EXPECT_FALSE(indexPlannerSelectable(IndexPhase::kPublic, IndexLifecycle::kPendingPurge, 1, 5));
    EXPECT_FALSE(indexPlannerSelectable(IndexPhase::kPublic, IndexLifecycle::kPurging, 1, 5));
}

TEST(IndexRuntimeModelTest, WriteDestinationIsSingleAndNeverIndexBeforePublic) {
    using D = IndexWriteDestination;
    EXPECT_EQ(indexWriteDestination(IndexPhase::kScan, IndexLifecycle::kActive), D::kDelta);
    EXPECT_EQ(indexWriteDestination(IndexPhase::kCatchup, IndexLifecycle::kActive), D::kDelta);
    EXPECT_EQ(indexWriteDestination(IndexPhase::kFinalize, IndexLifecycle::kActive), D::kRetryLater);
    EXPECT_EQ(indexWriteDestination(IndexPhase::kPublic, IndexLifecycle::kActive), D::kIndexTable);
    EXPECT_EQ(indexWriteDestination(IndexPhase::kError, IndexLifecycle::kActive), D::kSkip);
    // DROPPING 期间仍维护（保护在飞长查询）
    EXPECT_EQ(indexWriteDestination(IndexPhase::kPublic, IndexLifecycle::kDropping), D::kIndexTable);
    EXPECT_EQ(indexWriteDestination(IndexPhase::kPublic, IndexLifecycle::kPendingPurge), D::kIndexTable);
    // PURGING 之后停止维护
    EXPECT_EQ(indexWriteDestination(IndexPhase::kPublic, IndexLifecycle::kPurging), D::kSkip);
    EXPECT_EQ(indexWriteDestination(IndexPhase::kPublic, IndexLifecycle::kPurged), D::kSkip);
}

TEST(IndexRuntimeModelTest, UniqueOnlyEnforcedAfterPublic) {
    EXPECT_TRUE(indexUniqueEnforced(IndexPhase::kPublic));
    EXPECT_FALSE(indexUniqueEnforced(IndexPhase::kScan));
    EXPECT_FALSE(indexUniqueEnforced(IndexPhase::kCatchup));
    EXPECT_FALSE(indexUniqueEnforced(IndexPhase::kFinalize));
    EXPECT_FALSE(indexUniqueEnforced(IndexPhase::kError));
}

// ==================== 准入协议 ====================

TEST(IndexRuntimeAdmissionTest, GuardCountsInflightAndReleasesOnReset) {
    auto runtime = std::make_shared<IndexRuntime>(makeDescriptor(1));
    ASSERT_EQ(runtime->inflight(), 0u);

    auto guard = runtime->tryEnter();
    ASSERT_TRUE(guard.has_value());
    EXPECT_EQ(runtime->inflight(), 1u);
    EXPECT_EQ(guard->indexId(), 1u);

    guard->reset();
    EXPECT_EQ(runtime->inflight(), 0u);
    EXPECT_FALSE(static_cast<bool>(*guard));
}

TEST(IndexRuntimeAdmissionTest, ClosedAdmissionRejectsNewWriters) {
    auto runtime = std::make_shared<IndexRuntime>(makeDescriptor(1));
    ASSERT_TRUE(runtime->admissionOpen());
    EXPECT_TRUE(runtime->closeAdmission());
    EXPECT_FALSE(runtime->admissionOpen());
    EXPECT_FALSE(runtime->tryEnter().has_value());

    runtime->reopenAdmission();
    EXPECT_TRUE(runtime->admissionOpen());
    EXPECT_TRUE(runtime->tryEnter().has_value());
}

TEST(IndexRuntimeAdmissionTest, CloseAndWaitBlocksUntilGuardReleased) {
    auto runtime = std::make_shared<IndexRuntime>(makeDescriptor(1));
    auto guard = runtime->tryEnter();
    ASSERT_TRUE(guard.has_value());

    // 在另一个线程里关闸等待：只有 guard 释放后才可能返回 true。
    // 不断言"等了多久"，只断言"释放前不可能成功、释放后成功"。
    auto closer = std::async(std::launch::async, [runtime] { return runtime->closeAndWait(2000); });
    guard->reset();
    EXPECT_TRUE(closer.get());
    EXPECT_EQ(runtime->inflight(), 0u);
}

TEST(IndexRuntimeAdmissionTest, CloseAndWaitTimesOutWhileWriterAlive) {
    auto runtime = std::make_shared<IndexRuntime>(makeDescriptor(1));
    auto guard = runtime->tryEnter();
    ASSERT_TRUE(guard.has_value());
    EXPECT_FALSE(runtime->closeAndWait(0));
    EXPECT_EQ(runtime->inflight(), 1u);
}

TEST(IndexRuntimeAdmissionTest, NoWriterSneaksPastCloseUnderContention) {
    auto runtime = std::make_shared<IndexRuntime>(makeDescriptor(1));

    constexpr int kThreads = 4;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<bool> closed_observed{false};
    std::atomic<uint64_t> acquired_total{0};
    std::atomic<uint64_t> acquired_after_close{0};

    std::vector<std::thread> writers;
    writers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        writers.emplace_back([&] {
            // 先做一次准入，确保线程真的跑起来（不靠 sleep 等线程启动）。
            if (auto guard = runtime->tryEnter()) {
                acquired_total.fetch_add(1, std::memory_order_relaxed);
                guard->reset();
            }
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int round = 0; round < 200; ++round) {
                auto guard = runtime->tryEnter();
                if (!guard)
                    continue;
                acquired_total.fetch_add(1, std::memory_order_relaxed);
                // closeAndWait 返回后仍在准入成功 ⇒ 协议被破坏（I5）。
                if (closed_observed.load(std::memory_order_acquire))
                    acquired_after_close.fetch_add(1, std::memory_order_relaxed);
                guard->reset();
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < kThreads)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);

    // closeAndWait 返回 true 的唯一承诺：此刻 inflight==0（I5）。
    ASSERT_TRUE(runtime->closeAndWait(2000));
    closed_observed.store(true, std::memory_order_release);

    for (auto& t : writers)
        t.join();

    EXPECT_EQ(runtime->inflight(), 0u);
    EXPECT_EQ(acquired_after_close.load(), 0u);    // 关闸后不可能再准入
    EXPECT_GT(acquired_total.load(), 0u);          // 至少发生过准入（屏障保证）
    EXPECT_FALSE(runtime->tryEnter().has_value()); // 关闸状态是终态，需显式 reopen
}

TEST(IndexRuntimeAdmissionTest, InflightReturnsToZeroAfterManyTransactions) {
    auto runtime = std::make_shared<IndexRuntime>(makeDescriptor(1));

    // 逐个事务：guard 在循环作用域结束时释放 ⇒ inflight 必须回到 0。
    for (int i = 0; i < 1000; ++i) {
        auto guard = runtime->tryEnter();
        ASSERT_TRUE(guard.has_value());
    }
    EXPECT_EQ(runtime->inflight(), 0u);
    EXPECT_TRUE(runtime->waitDrained(0));

    // 1000 个并发事务（守卫同时存活）⇒ inflight==1000，全部释放后归零。
    std::vector<IndexWriteGuard> guards;
    guards.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        auto guard = runtime->tryEnter();
        ASSERT_TRUE(guard.has_value());
        guards.push_back(std::move(*guard));
    }
    EXPECT_EQ(runtime->inflight(), 1000u);
    EXPECT_FALSE(runtime->waitDrained(0));
    guards.clear();
    EXPECT_EQ(runtime->inflight(), 0u);
    EXPECT_TRUE(runtime->waitDrained(0));
}

// ==================== 读凭证 ====================

TEST(IndexRuntimeReadTest, ReadHandlesFollowLifecycle) {
    auto desc = makeDescriptor(3);
    desc.initial_phase = IndexPhase::kPublic;
    auto runtime = std::make_shared<IndexRuntime>(desc);

    auto handle = runtime->acquireRead();
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(runtime->readers(), 1u);
    handle->reset();
    EXPECT_EQ(runtime->readers(), 0u);

    // DROPPING / PENDING_PURGE 仍允许（数据保留给在飞查询）
    ASSERT_TRUE(runtime->transitionLifecycle(IndexLifecycle::kDropping));
    EXPECT_TRUE(runtime->acquireRead().has_value());
    ASSERT_TRUE(runtime->transitionLifecycle(IndexLifecycle::kPendingPurge));
    EXPECT_TRUE(runtime->acquireRead().has_value());
    // PURGING 之后拒绝
    ASSERT_TRUE(runtime->transitionLifecycle(IndexLifecycle::kPurging));
    EXPECT_FALSE(runtime->acquireRead().has_value());
}

TEST(IndexRuntimeReadTest, BuildingIndexHasNoReadHandle) {
    auto runtime = std::make_shared<IndexRuntime>(makeDescriptor(4));
    EXPECT_EQ(runtime->phase(), IndexPhase::kScan);
    EXPECT_FALSE(runtime->acquireRead().has_value());
}

// ==================== 注册表 ====================

TEST(IndexRuntimeRegistryTest, CreateIsIdempotentAndRejectsZero) {
    IndexRuntimeRegistry registry;
    EXPECT_EQ(registry.createOrUpdate(makeDescriptor(0)), nullptr);

    auto first = registry.createOrUpdate(makeDescriptor(5));
    ASSERT_NE(first, nullptr);
    auto second = registry.createOrUpdate(makeDescriptor(5));
    EXPECT_EQ(first, second); // 同一个对象
    EXPECT_EQ(registry.size(), 1u);
    EXPECT_EQ(registry.find(6), nullptr);
}

TEST(IndexRuntimeRegistryTest, ForLabelFiltersByEdgeFlag) {
    IndexRuntimeRegistry registry;
    registry.createOrUpdate(makeDescriptor(1, false, false, 7));
    registry.createOrUpdate(makeDescriptor(2, false, true, 7));
    registry.createOrUpdate(makeDescriptor(3, false, false, 8));

    auto vertex = registry.forLabel(7, false);
    ASSERT_EQ(vertex.size(), 1u);
    EXPECT_EQ(vertex[0]->indexId(), 1u);

    auto edge = registry.forLabel(7, true);
    ASSERT_EQ(edge.size(), 1u);
    EXPECT_EQ(edge[0]->indexId(), 2u);

    EXPECT_TRUE(registry.forLabel(9, false).empty());
}

TEST(IndexRuntimeRegistryTest, PlannerSelectableHonorsGenerationAndLifecycle) {
    IndexRuntimeRegistry registry;
    auto desc = makeDescriptor(10);
    desc.initial_phase = IndexPhase::kPublic;
    desc.public_generation = 5;
    registry.createOrUpdate(desc);

    EXPECT_TRUE(registry.plannerSelectable(10, 5));
    EXPECT_FALSE(registry.plannerSelectable(10, 4)); // 事务早于索引可用
    EXPECT_FALSE(registry.plannerSelectable(11, 5)); // 不存在

    auto runtime = registry.find(10);
    ASSERT_TRUE(runtime->transitionLifecycle(IndexLifecycle::kDropping));
    EXPECT_FALSE(registry.plannerSelectable(10, 5)); // DROP 后新计划不可选
}

TEST(IndexRuntimeRegistryTest, EraseRequiresPurged) {
    IndexRuntimeRegistry registry;
    auto runtime = registry.createOrUpdate(makeDescriptor(12));
    ASSERT_NE(runtime, nullptr);
    EXPECT_FALSE(registry.erase(12));
    ASSERT_TRUE(runtime->transitionLifecycle(IndexLifecycle::kPurging));
    EXPECT_FALSE(registry.erase(12));
    ASSERT_TRUE(runtime->transitionLifecycle(IndexLifecycle::kPurged));
    EXPECT_TRUE(registry.erase(12));
    EXPECT_EQ(registry.find(12), nullptr);
}

TEST(IndexRuntimeRegistryTest, RegistriesAreIsolated) {
    IndexRuntimeRegistry a;
    IndexRuntimeRegistry b;
    a.createOrUpdate(makeDescriptor(1));
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(b.size(), 0u); // 每图一份，互不影响
}

// ==================== 测试钩子 ====================

TEST(IndexTestHooksTest, InstallInvokeAndClear) {
    std::atomic<int> calls{0};
    std::string seen_point;
    uint64_t seen_index = 0;
    index_hooks::install([&](std::string_view point, uint64_t index_id) {
        calls.fetch_add(1);
        seen_point = std::string(point);
        seen_index = index_id;
    });
    EXPECT_TRUE(index_hooks::hasHandler());
    index_hooks::point("scan.after_read_props", 42);
    EXPECT_EQ(calls.load(), 1);
    EXPECT_EQ(seen_point, "scan.after_read_props");
    EXPECT_EQ(seen_index, 42u);

    index_hooks::clear();
    EXPECT_FALSE(index_hooks::hasHandler());
    index_hooks::point("scan.after_read_props", 42);
    EXPECT_EQ(calls.load(), 1); // 未安装时 no-op
}

// ==================== 定义 → 运行时描述 ====================

TEST(IndexRuntimeFactoryTest, MapsPersistedDefinitionToRuntimeDescriptor) {
    LabelDef::IndexDef def;
    def.index_id = 42;
    def.name = "idx_person_name";
    def.unique = true;
    def.state = IndexState::PUBLIC;

    auto vertex = makeIndexRuntimeDescriptor(def, /*label_id=*/7, /*is_edge=*/false);
    EXPECT_EQ(vertex.index_id, 42u);
    EXPECT_EQ(vertex.label_id, 7u);
    EXPECT_FALSE(vertex.is_edge);
    EXPECT_TRUE(vertex.unique);
    EXPECT_EQ(vertex.index_table, vidxTableById(42));
    EXPECT_EQ(vertex.delta_table, indexDeltaTable(42));
    EXPECT_EQ(vertex.initial_phase, IndexPhase::kPublic);
    EXPECT_EQ(vertex.initial_lifecycle, IndexLifecycle::kActive);

    def.prop_ids = {1, 2};
    auto edge = makeIndexRuntimeDescriptor(def, /*label_id=*/9, /*is_edge=*/true);
    EXPECT_TRUE(edge.is_edge);
    EXPECT_EQ(edge.index_table, eidxCompositeTable(9, {1, 2}));
    EXPECT_EQ(edge.delta_table, indexDeltaTable(42));

    // 未 PUBLIIC 的持久化状态（WRITE_ONLY 会被启动恢复转 ERROR）⇒ 默认 phase=ERROR
    def.state = IndexState::WRITE_ONLY;
    EXPECT_EQ(makeIndexRuntimeDescriptor(def, 9, true).initial_phase, IndexPhase::kError);
}

// ==================== 空索引护栏（纯判定） ====================

TEST(IndexRuntimeModelTest, EmptyIndexGuardOnlyFailsWhenScannedButNothingEmitted) {
    EXPECT_FALSE(indexShouldFailEmpty(/*scanned=*/0, /*emitted=*/0)); // 合法空索引
    EXPECT_TRUE(indexShouldFailEmpty(/*scanned=*/5, /*emitted=*/0));  // 有数据但没产出 ⇒ 事故
    EXPECT_FALSE(indexShouldFailEmpty(/*scanned=*/5, /*emitted=*/1));
}

// ==================== schema 代数 pin 计数 ====================

TEST(SchemaGenerationTrackerTest, PinUnpinAndWait) {
    SchemaGenerationTracker tracker;
    EXPECT_EQ(tracker.minActiveGeneration(), UINT64_MAX);
    EXPECT_TRUE(tracker.waitNoPinsOlderThan(1, 0));

    tracker.pin(5);
    tracker.pin(5);
    tracker.pin(7);
    EXPECT_EQ(tracker.minActiveGeneration(), 5u);
    EXPECT_EQ(tracker.pinCount(), 3u);
    EXPECT_FALSE(tracker.waitNoPinsOlderThan(7, 0));

    tracker.unpin(5);
    EXPECT_FALSE(tracker.waitNoPinsOlderThan(7, 0)); // 还有一个 5
    tracker.unpin(5);
    EXPECT_TRUE(tracker.waitNoPinsOlderThan(7, 0)); // 只剩 7
    tracker.unpin(7);
    EXPECT_EQ(tracker.pinCount(), 0u);
    EXPECT_EQ(tracker.minActiveGeneration(), UINT64_MAX);
}

// ==================== 提交期 schema 兼容性（纯判定） ====================

TEST(SchemaCompatTest, CommitDecisionUsesVersionAndTouchedSet) {
    // 版本未变 ⇒ 无论 touched 什么都放行。
    EXPECT_TRUE(commitSchemaCompatible(/*pin=*/3, /*current=*/3, /*touches_removed=*/true));
    // 版本变了但没碰被删元素 ⇒ 放行（元素级判定，避免误杀）。
    EXPECT_TRUE(commitSchemaCompatible(/*pin=*/3, /*current=*/4, /*touches_removed=*/false));
    // 版本变了且碰了被删元素 ⇒ 中止。
    EXPECT_FALSE(commitSchemaCompatible(/*pin=*/3, /*current=*/4, /*touches_removed=*/true));
}

TEST(SchemaCompatTest, ChangeLogOnlyCountsRemovalsAfterPin) {
    SchemaChangeLog log;
    log.recordRemoval(/*compat_version=*/2, SchemaElementRef::property(/*label=*/1, /*prop=*/7));

    const std::vector<SchemaElementRef> touched_prop = {SchemaElementRef::property(1, 7)};
    const std::vector<SchemaElementRef> touched_other = {SchemaElementRef::property(1, 8)};

    EXPECT_FALSE(log.touchesRemovedSince(touched_prop, /*pin_compat=*/2)); // pin 已包含该变更
    EXPECT_TRUE(log.touchesRemovedSince(touched_prop, /*pin_compat=*/1));  // pin 早于移除
    EXPECT_FALSE(log.touchesRemovedSince(touched_other, /*pin_compat=*/1));

    log.recordRemoval(/*compat_version=*/3, SchemaElementRef::label(/*label=*/2));
    const std::vector<SchemaElementRef> touched_label = {SchemaElementRef::label(2)};
    EXPECT_TRUE(log.touchesRemovedSince(touched_label, /*pin_compat=*/1));
    EXPECT_FALSE(log.touchesRemovedSince(touched_label, /*pin_compat=*/3));
    EXPECT_EQ(log.size(), 2u);
}

TEST(SchemaCompatTest, ElementIdentityIncludesKind) {
    EXPECT_TRUE(SchemaElementRef::label(1) == SchemaElementRef::label(1));
    EXPECT_FALSE(SchemaElementRef::label(1) == SchemaElementRef::property(1, 0));
    EXPECT_FALSE(SchemaElementRef::property(1, 2) == SchemaElementRef::property(1, 3));
    EXPECT_FALSE(SchemaElementRef::index(1, 1) == SchemaElementRef::label(1));
}
