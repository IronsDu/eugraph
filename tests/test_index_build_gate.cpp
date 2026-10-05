// 索引构建闸门：准入线性化 / 关闸等在飞写者 / RAII 释放（设计 §7.1、§15.1、§20.14）
#include "storage/index/index_build_gate.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {
using eugraph::IndexBuildGate;
using eugraph::IndexBuildGateRegistry;
} // namespace

TEST(IndexBuildGateTest, AdmissionSucceedsBeforeCloseAndFailsAfter) {
    auto gate = IndexBuildGateRegistry::instance().gate(1001);
    EXPECT_FALSE(gate->closed());
    {
        auto guard = gate->tryEnter();
        EXPECT_TRUE(static_cast<bool>(guard)) << "关闸前应能准入";
        EXPECT_EQ(gate->inflight(), 1u);
    }
    EXPECT_EQ(gate->inflight(), 0u) << "RAII 守卫析构必须归还名额";

    EXPECT_TRUE(gate->closeAndWait(10000));
    EXPECT_TRUE(gate->closed());
    auto denied = gate->tryEnter();
    EXPECT_FALSE(static_cast<bool>(denied)) << "关闸后写者必须被拒绝（改直写索引）";
    EXPECT_EQ(gate->inflight(), 0u);
}

TEST(IndexBuildGateTest, CloseWaitsForInflightWriters) {
    auto gate = IndexBuildGateRegistry::instance().gate(1002);
    std::atomic<bool> writer_entered{false};
    std::atomic<bool> may_exit{false};

    std::thread writer([&] {
        auto guard = gate->tryEnter();
        ASSERT_TRUE(static_cast<bool>(guard));
        writer_entered = true;
        while (!may_exit)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });

    while (!writer_entered)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // 关闸必须**等待**在飞写者：先超时返回 false
    EXPECT_FALSE(gate->closeAndWait(50)) << "在飞写者未退出时不应提前返回成功";
    may_exit = true;
    writer.join();
    // 写者已退出 ⇒ 再等必定成功（且 inflight 归零）
    EXPECT_TRUE(gate->closeAndWait(30000));
    EXPECT_EQ(gate->inflight(), 0u);
}

TEST(IndexBuildGateTest, TwoPhaseAdmissionNeverLetsWriterStraddleClose) {
    auto gate = IndexBuildGateRegistry::instance().gate(1003);
    std::atomic<int> admitted_after_close{0};
    std::atomic<int> repeated{0};

    // 反复"关闸 → 尝试准入 → 计数"：任何在关闸后成功准入的写者都会破坏不变量
    for (int round = 0; round < 200; ++round) {
        auto g = IndexBuildGateRegistry::instance().gate(2000 + round); // 每轮新索引，避免累积
        std::thread t([&] {
            auto guard = g->tryEnter();
            if (guard && g->closed())
                ++admitted_after_close; // 准入成功却在关闸后 ⇒ 违反线性化
        });
        g->closeAndWait(30000);
        t.join();
        ++repeated;
    }
    EXPECT_EQ(repeated, 200);
    EXPECT_EQ(admitted_after_close, 0) << "两阶段准入必须保证：准入成功者不可能观察到已关闸";
}

TEST(IndexBuildGateTest, RegistryReturnsSameGatePerIndex) {
    auto a = IndexBuildGateRegistry::instance().gate(3001);
    auto b = IndexBuildGateRegistry::instance().gate(3001);
    EXPECT_EQ(a.get(), b.get()) << "同一 index_id 必须映射到同一闸门";
    auto c = IndexBuildGateRegistry::instance().gate(3002);
    EXPECT_NE(a.get(), c.get());
}
