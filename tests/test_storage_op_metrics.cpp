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

// ==================== Owning TLS scope (PROFILE + concurrency, #310) ====================
//
// The traced-operator scope is a coroutine scope: it installs at a resume and
// restores at a suspend, and a coroutine can resume on a different compute
// thread (or be destroyed after its statement's QueryContext is gone). A bare
// thread-local pointer restored there used to dangle; the owning companion
// keeps the raw mirror valid by construction.

TEST(StorageOpMetricsTest, SharedScopeKeepsCountersAliveWhileInstalled) {
    StorageOpCounters* raw = nullptr;
    std::weak_ptr<StorageOpCounters> weak;
    {
        auto owner = std::make_shared<StorageOpCounters>();
        weak = owner;
        StorageOpMetricsScope scope(owner);
        raw = tls_storage_op_counters;
        ASSERT_NE(raw, nullptr);
        EXPECT_EQ(tls_storage_op_counters_owner.get(), raw) << "owner TLS must mirror the raw pointer";

        // The scope must be enough to keep the counters alive: this is the
        // property a queued dispatch / cross-thread restore relies on.
        owner.reset();
        EXPECT_FALSE(weak.expired()) << "scope must own the counters it installed";
        recordWtSearch();
        EXPECT_EQ(raw->wt_search.load(std::memory_order_relaxed), 1u);
    }
    EXPECT_TRUE(weak.expired()) << "scope must release its reference on exit";
    EXPECT_EQ(tls_storage_op_counters, nullptr);
    EXPECT_FALSE(tls_storage_op_counters_owner);
}

TEST(StorageOpMetricsTest, SharedScopeRestoresPreviousOwner) {
    auto outer = std::make_shared<StorageOpCounters>();
    auto inner = std::make_shared<StorageOpCounters>();

    StorageOpMetricsScope outer_scope(outer);
    EXPECT_EQ(tls_storage_op_counters_owner, outer);
    {
        StorageOpMetricsScope inner_scope(inner);
        EXPECT_EQ(tls_storage_op_counters_owner, inner);
        EXPECT_EQ(tls_storage_op_counters, inner.get());
        recordWtSearch();
    }
    EXPECT_EQ(tls_storage_op_counters, outer.get()) << "raw mirror must follow the restored owner";
    EXPECT_EQ(tls_storage_op_counters_owner, outer);
    recordWtSearch();
    EXPECT_EQ(outer->wt_search.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(inner->wt_search.load(std::memory_order_relaxed), 1u);
}
