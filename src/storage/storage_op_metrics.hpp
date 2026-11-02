#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace eugraph {

/// Per-statement WT cursor seek counters. The active counter object is
/// installed by AsyncGraphDataStore around each IO-thread dispatch; sync store
/// code only touches thread-local state, so no session->counter lookup is needed.
struct StorageOpCounters : std::enable_shared_from_this<StorageOpCounters> {
    std::atomic<uint64_t> wt_search{0};
    std::atomic<uint64_t> wt_search_near{0};
    std::atomic<uint64_t> wt_scan_rows{0};
    std::atomic<uint64_t> io_dispatches{0};
    std::atomic<uint64_t> prefetch_hits{0};
    std::atomic<uint64_t> prefetch_misses{0};

    /// When this is a per-operator counter, aggregate owns the statement
    /// counter. Durable ownership keeps the statement counters alive while an
    /// IO dispatch that captured them is still queued after query cancellation.
    std::shared_ptr<StorageOpCounters> aggregate;
};

/// Pin counters for an async hop: returns a shared_ptr owning `raw` when the
/// object is shared_ptr-owned (per-operator / profiling counters), so a queued
/// IO task keeps it alive after a cancelled query drops its own reference.
/// Returns empty for non-shared (stack/test) counters; callers then keep the
/// raw pointer as before.
inline std::shared_ptr<StorageOpCounters> pinStorageOpCounters(StorageOpCounters* raw) {
    if (raw == nullptr)
        return nullptr;
    return raw->weak_from_this().lock();
}

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
