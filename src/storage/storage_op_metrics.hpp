#pragma once

#include <atomic>
#include <cstdint>

namespace eugraph {

/// Per-statement WT cursor seek counters. The active counter object is
/// installed by AsyncGraphDataStore around each IO-thread dispatch; sync store
/// code only touches thread-local state, so no session->counter lookup is needed.
struct StorageOpCounters {
    std::atomic<uint64_t> wt_search{0};
    std::atomic<uint64_t> wt_search_near{0};
    std::atomic<uint64_t> wt_scan_rows{0};
    std::atomic<uint64_t> io_dispatches{0};
    std::atomic<uint64_t> prefetch_hits{0};
    std::atomic<uint64_t> prefetch_misses{0};

    /// When this is a per-operator counter, aggregate points at the statement
    /// counter. Counters are installed into thread-local storage by the
    /// operator wrapper; storage work on the IO thread increments both.
    StorageOpCounters* aggregate = nullptr;
};

inline thread_local StorageOpCounters* tls_storage_op_counters = nullptr;

class StorageOpMetricsScope {
public:
    explicit StorageOpMetricsScope(StorageOpCounters* counters) noexcept : previous_(tls_storage_op_counters) {
        tls_storage_op_counters = counters;
    }
    ~StorageOpMetricsScope() {
        tls_storage_op_counters = previous_;
    }
    StorageOpMetricsScope(const StorageOpMetricsScope&) = delete;
    StorageOpMetricsScope& operator=(const StorageOpMetricsScope&) = delete;

private:
    StorageOpCounters* previous_;
};

inline void recordWtSearch() noexcept {
    if (auto* counters = tls_storage_op_counters) {
        counters->wt_search.fetch_add(1, std::memory_order_relaxed);
        if (counters->aggregate)
            counters->aggregate->wt_search.fetch_add(1, std::memory_order_relaxed);
    }
}

inline void recordWtSearchNear() noexcept {
    if (auto* counters = tls_storage_op_counters) {
        counters->wt_search_near.fetch_add(1, std::memory_order_relaxed);
        if (counters->aggregate)
            counters->aggregate->wt_search_near.fetch_add(1, std::memory_order_relaxed);
    }
}

inline void recordWtScanRows(uint64_t rows) noexcept {
    if (auto* counters = tls_storage_op_counters) {
        counters->wt_scan_rows.fetch_add(rows, std::memory_order_relaxed);
        if (counters->aggregate)
            counters->aggregate->wt_scan_rows.fetch_add(rows, std::memory_order_relaxed);
    }
}

inline void recordIoDispatch(StorageOpCounters* counters) noexcept {
    if (!counters)
        return;
    counters->io_dispatches.fetch_add(1, std::memory_order_relaxed);
    if (counters->aggregate)
        counters->aggregate->io_dispatches.fetch_add(1, std::memory_order_relaxed);
}

inline void recordPrefetchHit() noexcept {
    if (auto* counters = tls_storage_op_counters) {
        counters->prefetch_hits.fetch_add(1, std::memory_order_relaxed);
        if (counters->aggregate)
            counters->aggregate->prefetch_hits.fetch_add(1, std::memory_order_relaxed);
    }
}

inline void recordPrefetchMiss() noexcept {
    if (auto* counters = tls_storage_op_counters) {
        counters->prefetch_misses.fetch_add(1, std::memory_order_relaxed);
        if (counters->aggregate)
            counters->aggregate->prefetch_misses.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace eugraph
