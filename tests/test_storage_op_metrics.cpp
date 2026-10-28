#include <gtest/gtest.h>

#include "storage/storage_op_metrics.hpp"

#include <atomic>
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
