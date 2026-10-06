// 索引两维状态模型与句柄所有权（不依赖服务端/WiredTiger）
#include "common/types/index_state.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using eugraph::IndexBuildPhase;
using eugraph::IndexHandle;
using eugraph::IndexLifecycle;
using eugraph::IndexRuntimeState;
using eugraph::IndexState;

IndexRuntimeState make(IndexState durable, IndexLifecycle lifecycle = IndexLifecycle::ACTIVE) {
    IndexRuntimeState st;
    st.durable = durable;
    st.lifecycle = lifecycle;
    return st;
}

// 供 IndexHandle 使用的假索引对象（真实场景是 catalog 里的索引元数据）
struct FakeIndex {
    std::string name;
    IndexRuntimeState state;
};

auto stateOf = [](const FakeIndex& e) -> const IndexRuntimeState& { return e.state; };

} // namespace

// ==================== I1：读路径唯一判据 ====================

TEST(IndexRuntimeState, PlannerOnlyUsesActivePublic) {
    EXPECT_FALSE(make(IndexState::WRITE_ONLY).plannerUsable());                       // 构建中不可读
    EXPECT_TRUE(make(IndexState::PUBLIC).plannerUsable());                            // 唯一可读组合
    EXPECT_FALSE(make(IndexState::PUBLIC, IndexLifecycle::DROPPING).plannerUsable()); // 已下达删除 ⇒ 新计划不可选
    EXPECT_FALSE(make(IndexState::PUBLIC, IndexLifecycle::PENDING_PURGE).plannerUsable());
    EXPECT_FALSE(make(IndexState::PUBLIC, IndexLifecycle::PURGING).plannerUsable());
    EXPECT_FALSE(make(IndexState::ERROR).plannerUsable());
    EXPECT_FALSE(make(IndexState::DELETE_ONLY).plannerUsable());
}

// ==================== 写路径判据（P2 前与既有行为一致） ====================

TEST(IndexRuntimeState, WriteMaintainedMatchesLegacyBehaviourUntilDeltaLands) {
    EXPECT_TRUE(make(IndexState::WRITE_ONLY).writeMaintained()); // 构建中仍维护（否则构建期写入会丢）
    EXPECT_TRUE(make(IndexState::PUBLIC).writeMaintained());
    EXPECT_FALSE(make(IndexState::ERROR).writeMaintained());
    EXPECT_FALSE(make(IndexState::DELETE_ONLY).writeMaintained());
}

TEST(IndexRuntimeState, DroppingKeepsMaintainingForInFlightReaders) {
    // 关键：DROPPING/PENDING_PURGE 期间必须继续维护，否则在飞长查询会读到缺最新写入的索引
    EXPECT_TRUE(make(IndexState::PUBLIC, IndexLifecycle::DROPPING).writeMaintained());
    EXPECT_TRUE(make(IndexState::PUBLIC, IndexLifecycle::PENDING_PURGE).writeMaintained());
    EXPECT_FALSE(make(IndexState::PUBLIC, IndexLifecycle::PURGING).writeMaintained()); // 停止维护
    EXPECT_TRUE(make(IndexState::PUBLIC, IndexLifecycle::DROPPING).dataRetainedForReaders());
    EXPECT_FALSE(make(IndexState::PUBLIC, IndexLifecycle::PURGING).dataRetainedForReaders());
}

TEST(IndexRuntimeState, CancelledBuildStopsMaintaining) {
    auto st = make(IndexState::WRITE_ONLY);
    EXPECT_TRUE(st.buildInProgress());
    st.build_cancelled = true;
    EXPECT_FALSE(st.buildInProgress());
    EXPECT_FALSE(st.writeMaintained()); // 取消构建 ⇒ 写路径立即"不维护"（变更表停止增长）
}

// ==================== 生命周期迁移 ====================

TEST(IndexRuntimeState, LifecycleTransitions) {
    EXPECT_TRUE(make(IndexState::PUBLIC).canTransitionLifecycleTo(IndexLifecycle::DROPPING));
    EXPECT_FALSE(make(IndexState::PUBLIC).canTransitionLifecycleTo(IndexLifecycle::PURGING)); // 必须先 DROPPING
    EXPECT_TRUE(
        make(IndexState::PUBLIC, IndexLifecycle::DROPPING).canTransitionLifecycleTo(IndexLifecycle::PENDING_PURGE));
    EXPECT_TRUE(make(IndexState::PUBLIC, IndexLifecycle::DROPPING)
                    .canTransitionLifecycleTo(IndexLifecycle::PURGING)); // 使用者已归零，无需中转
    EXPECT_TRUE(
        make(IndexState::PUBLIC, IndexLifecycle::PENDING_PURGE).canTransitionLifecycleTo(IndexLifecycle::PURGING));
    EXPECT_FALSE(make(IndexState::PUBLIC, IndexLifecycle::PURGING)
                     .canTransitionLifecycleTo(IndexLifecycle::DROPPING)); // PURGING 是终态
}

// ==================== 句柄：shared_ptr 保证"句柄在 ⇒ 对象在" ====================

TEST(IndexHandleTest, AcquireRechecksVisibilityAndCanFail) {
    auto entry = std::make_shared<FakeIndex>(FakeIndex{"idx_a", make(IndexState::PUBLIC)});
    EXPECT_TRUE(IndexHandle<FakeIndex>::acquire(entry, stateOf).valid());

    auto building = std::make_shared<FakeIndex>(FakeIndex{"idx_b", make(IndexState::WRITE_ONLY)});
    EXPECT_FALSE(IndexHandle<FakeIndex>::acquire(building, stateOf).valid()); // 构建中 ⇒ 不可用，调用方须重新计划

    auto dropping = std::make_shared<FakeIndex>(FakeIndex{"idx_c", make(IndexState::PUBLIC, IndexLifecycle::DROPPING)});
    EXPECT_FALSE(IndexHandle<FakeIndex>::acquire(dropping, stateOf).valid()); // 已下达删除 ⇒ 新计划不选它

    EXPECT_FALSE(IndexHandle<FakeIndex>::acquire(nullptr, stateOf).valid());
}

TEST(IndexHandleTest, HandleKeepsEntryAliveAfterCatalogDropsIt) {
    IndexHandle<FakeIndex> handle;
    {
        auto entry = std::make_shared<FakeIndex>(FakeIndex{"idx_a", make(IndexState::PUBLIC)});
        handle = IndexHandle<FakeIndex>::acquire(entry, stateOf);
        ASSERT_TRUE(handle.valid());
        EXPECT_EQ(entry.use_count(), 2); // 句柄持有一份引用
    } // catalog 侧引用在此消失（模拟 DROP 把条目从表里移除）
    // 句柄仍有效且可安全读取 —— 这正是"不用裸指针"要买到的性质
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.entry().name, "idx_a");
    EXPECT_TRUE(handle.entry().state.plannerUsable());
    handle.reset();
    EXPECT_FALSE(handle.valid());
}
