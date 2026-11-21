#include "storage/stats/graph_stats_store.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace eugraph;

TEST(GraphStatsStoreTest, ConcurrentDeltasDoNotLoseUpdates) {
    GraphStatsStore store;
    constexpr int kThreads = 8;
    constexpr int kIters = 20000;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            StatsDelta d;
            d.vertex_delta[1] = 1;
            for (int i = 0; i < kIters; ++i)
                store.applyCommitted(d);
        });
    }
    for (auto& t : threads)
        t.join();

    auto snap = store.snapshot();
    ASSERT_NE(snap, nullptr);
    ASSERT_EQ(snap->labels.count(1), 1u);
    EXPECT_EQ(snap->labels.at(1).vertices, kThreads * kIters);
}

TEST(GraphStatsStoreTest, DeltaCanBeNegativeAndMultiSlot) {
    GraphStatsStore store;
    StatsDelta add;
    add.vertex_delta[1] = 10;
    add.vertex_delta[2] = 3;
    add.edge_delta[7] = 4;
    add.index_delta[9] = 100;
    store.applyCommitted(add);

    StatsDelta remove;
    remove.vertex_delta[1] = -4;
    remove.edge_delta[7] = -1;
    remove.index_delta[9] = -20;
    store.applyCommitted(remove);

    auto snap = store.snapshot();
    EXPECT_EQ(snap->labels.at(1).vertices, 6);
    EXPECT_EQ(snap->labels.at(2).vertices, 3);
    EXPECT_EQ(snap->edge_labels.at(7).edges, 3);
    EXPECT_EQ(snap->indexes.at(9).rows, 80);
    EXPECT_TRUE(snap->indexes.at(9).ready);
}

TEST(GraphStatsStoreTest, SnapshotIsImmutableAndEpochAdvances) {
    GraphStatsStore store;
    StatsDelta d;
    d.vertex_delta[1] = 5;
    store.applyCommitted(d);
    auto first = store.snapshot();
    EXPECT_EQ(first->labels.at(1).vertices, 5);
    EXPECT_EQ(first->epoch, 1u);

    d.vertex_delta[1] = 2;
    store.applyCommitted(d);
    auto second = store.snapshot();
    EXPECT_EQ(second->labels.at(1).vertices, 7);
    EXPECT_EQ(second->epoch, 2u);
    // 旧快照不被后续更新修改。
    EXPECT_EQ(first->labels.at(1).vertices, 5);
}
