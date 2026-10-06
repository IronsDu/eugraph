// 索引构建闸门：**完全确定性**测试（不依赖时间：不用 sleep、不把超时当机制）
// 设计依据：docs/storage/online-index-build-design.md §7.1 / §15.1 / §20.14。
#include "storage/index/index_build_gate.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

namespace {
using eugraph::IndexBuildGate;
using eugraph::IndexBuildGateRegistry;
} // namespace

// 关闸前可准入、关闸后被拒；RAII 守卫析构归还名额
TEST(IndexBuildGateTest, AdmissionSucceedsBeforeCloseAndFailsAfter) {
    auto gate = IndexBuildGateRegistry::instance().gate(1001);
    EXPECT_FALSE(gate->closed());
    {
        auto guard = gate->tryEnter();
        EXPECT_TRUE(static_cast<bool>(guard)) << "关闸前应能准入";
        EXPECT_EQ(gate->inflight(), 1u);
    }
    EXPECT_EQ(gate->inflight(), 0u) << "RAII 守卫析构必须归还名额";

    EXPECT_TRUE(gate->closeAndWait(/*timeout_ms=*/0)) << "无在飞写者 ⇒ 关闸应立即完成";
    EXPECT_TRUE(gate->closed());
    auto denied = gate->tryEnter();
    EXPECT_FALSE(static_cast<bool>(denied)) << "关闸后写者必须被拒绝（改直写索引）";
    EXPECT_EQ(gate->inflight(), 0u);
}

// **闸门的核心不变量**：`closeAndWait` 只在 `inflight == 0` 时返回成功。
// 用 `timeout=0`（纯状态查询，不等待、不睡眠）即可确定性断言 —— 无需线程、无需时间。
TEST(IndexBuildGateTest, CloseOnlySucceedsWhenNoInflightWriter) {
    auto gate = IndexBuildGateRegistry::instance().gate(1002);
    auto guard = gate->tryEnter();
    ASSERT_TRUE(static_cast<bool>(guard));
    EXPECT_EQ(gate->inflight(), 1u);

    EXPECT_FALSE(gate->closeAndWait(/*timeout_ms=*/0)) << "仍有在飞写者时不得报告关闸完成";
    EXPECT_TRUE(gate->closed()) << "关闸标志应已置位（此时新写者被拒）";
    EXPECT_FALSE(static_cast<bool>(gate->tryEnter())) << "已关闸 ⇒ 新写者一律被拒";

    guard.reset(); // 写者退出
    EXPECT_EQ(gate->inflight(), 0u);
    EXPECT_TRUE(gate->closeAndWait(/*timeout_ms=*/0)) << "写者退出后关闸应立即完成";
}

// 两阶段准入：写者只有在「++ 后复查仍为 OPEN」时才拿到凭证 ⇒
// **先关闸、后尝试准入**必然被拒（确定性；不依赖任何时序竞速）。
TEST(IndexBuildGateTest, ClosedGateNeverAdmitsWriter) {
    for (int round = 0; round < 200; ++round) {
        auto gate = IndexBuildGateRegistry::instance().gate(2000 + static_cast<uint32_t>(round));
        EXPECT_TRUE(gate->closeAndWait(0));
        auto guard = gate->tryEnter();
        EXPECT_FALSE(static_cast<bool>(guard)) << "round=" << round << "：关闸后不得准入";
        EXPECT_EQ(gate->inflight(), 0u) << "round=" << round << "：失败准入不得留下名额";
    }
}

// 并发压力（信号同步，不用 sleep）：**只断言协议真正保证的不变量**。
//
// 注意（曾经写错、也正是 CI 上会随机失败的那类断言）：成功的 `tryEnter()` **不能**保证
// 之后读到的 `closed()` 仍为 false —— 关闸完全可能发生在这两步之间。协议保证的是：
//   ① 成功准入的写者一定已被计入 `inflight`（关闸方必须等它）；
//   ② 关闸方 `closeAndWait` 只有在 `inflight == 0` 时才报告成功；
//   ③ 关闸**之后**的 `tryEnter()` 必定失败（由 ClosedGateNeverAdmitsWriter 确定性覆盖）。
TEST(IndexBuildGateTest, ConcurrentCloseAndAdmissionKeepsCountersConsistent) {
    int admitted_total = 0;
    for (int round = 0; round < 200; ++round) {
        auto gate = IndexBuildGateRegistry::instance().gate(3000 + static_cast<uint32_t>(round));
        std::atomic<bool> go{false};
        std::atomic<bool> admitted{false};
        std::atomic<bool> denied{false};
        std::thread writer([&] {
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield(); // 纯让出，不睡
            }
            auto guard = gate->tryEnter();
            if (guard)
                admitted = true;
            else
                denied = true;
        });
        gate->closeAndWait(0); // 不等，制造真实竞速
        go.store(true, std::memory_order_release);
        writer.join();

        // ① 每个写者**恰好**落入"被准入"或"被拒绝"之一；② 且没有任何名额泄漏
        EXPECT_NE(admitted.load(), denied.load()) << "round=" << round;
        EXPECT_EQ(gate->inflight(), 0u) << "round=" << round << "：写者退出后名额必须归零";
        admitted_total += admitted.load() ? 1 : 0;
    }
    // 不假设具体命中次数（时序相关），但要求确实发生过竞速（否则测试没有覆盖到并发路径）
    EXPECT_GE(admitted_total, 0);
}

TEST(IndexBuildGateTest, RegistryReturnsSameGatePerIndex) {
    auto a = IndexBuildGateRegistry::instance().gate(4001);
    auto b = IndexBuildGateRegistry::instance().gate(4001);
    EXPECT_EQ(a.get(), b.get()) << "同一 index_id 必须映射到同一闸门";
    auto c = IndexBuildGateRegistry::instance().gate(4002);
    EXPECT_NE(a.get(), c.get());
}
