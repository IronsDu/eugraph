#include <gtest/gtest.h>

#include "storage/storage_op_metrics.hpp"

#include <atomic>
#include <memory>
#include <thread>

using namespace eugraph;

TEST(StorageOpMetricsTest, ScopeRecordsAndRestoresPrevious) {
    StorageOpCounters first;
    StorageOpCounters second;

    {
        StorageOpMetricsScope scope(&first);
        recordWtSearch();
        recordWtSearchNear();
        {
            StorageOpMetricsScope nested(&second);
            recordWtSearch();
            recordWtSearch();
            recordWtSearchNear();
        }
        // Nested scope must restore `first`, not leave `second` installed.
        recordWtSearch();
    }

    EXPECT_EQ(first.wt_search.load(std::memory_order_relaxed), 2u);
    EXPECT_EQ(first.wt_search_near.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(second.wt_search.load(std::memory_order_relaxed), 2u);
    EXPECT_EQ(second.wt_search_near.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(tls_storage_op_counters, nullptr);
}

TEST(StorageOpMetricsTest, RecordsAreNoOpsWithoutScope) {
    tls_storage_op_counters = nullptr;
    recordWtSearch();
    recordWtSearchNear();
    EXPECT_EQ(tls_storage_op_counters, nullptr);
}

TEST(StorageOpMetricsTest, CountersAreThreadLocal) {
    StorageOpCounters counters;
    std::atomic<bool> other_thread_saw_null{false};

    {
        StorageOpMetricsScope scope(&counters);
        std::thread other([&] {
            // The installing thread's scope must not leak into another thread.
            other_thread_saw_null.store(tls_storage_op_counters == nullptr, std::memory_order_relaxed);
            recordWtSearch();
        });
        other.join();

        EXPECT_TRUE(other_thread_saw_null.load(std::memory_order_relaxed));
        EXPECT_EQ(counters.wt_search.load(std::memory_order_relaxed), 0u);
        recordWtSearch();
        EXPECT_EQ(counters.wt_search.load(std::memory_order_relaxed), 1u);
    }

    EXPECT_EQ(tls_storage_op_counters, nullptr);
}

TEST(StorageOpMetricsTest, AggregateOwnsStatementCounters) {
    auto statement = std::make_shared<StorageOpCounters>();
    std::weak_ptr<StorageOpCounters> weak = statement;
    {
        auto op = std::make_shared<StorageOpCounters>();
        op->aggregate = statement;
        // A cancelled query may drop its statement counters while operator
        // records still reference them through `aggregate`.
        statement.reset();
        EXPECT_FALSE(weak.expired()) << "per-operator counters must own the statement counters";
    }
    EXPECT_TRUE(weak.expired());
}

TEST(StorageOpMetricsTest, PinnedCountersSurviveOwnerDropUntilTaskRuns) {
    auto counters = std::make_shared<StorageOpCounters>();
    std::weak_ptr<StorageOpCounters> weak = counters;
    std::atomic<bool> release{false};
    std::atomic<bool> ran{false};

    auto pinned = pinStorageOpCounters(counters.get());
    ASSERT_NE(pinned, nullptr);
    std::thread pending([pinned = std::move(pinned), &release, &ran] {
        while (!release.load(std::memory_order_acquire))
            std::this_thread::yield();
        StorageOpMetricsScope scope(pinned.get());
        recordWtSearch();
        ran.store(true, std::memory_order_release);
    });

    // Query cancellation destroys the owner while the IO task is still queued.
    counters.reset();
    EXPECT_FALSE(weak.expired()) << "a queued dispatch must keep its counters alive";
    EXPECT_FALSE(ran.load());

    release.store(true, std::memory_order_release);
    pending.join();
    EXPECT_TRUE(ran.load());
    EXPECT_TRUE(weak.expired());
}

TEST(StorageOpMetricsTest, PinningNonSharedCountersReturnsNull) {
    StorageOpCounters stack;
    EXPECT_EQ(pinStorageOpCounters(&stack), nullptr);
    EXPECT_EQ(pinStorageOpCounters(nullptr), nullptr);
}
