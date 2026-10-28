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
    if (auto* counters = tls_storage_op_counters)
        counters->wt_search.fetch_add(1, std::memory_order_relaxed);
}

inline void recordWtSearchNear() noexcept {
    if (auto* counters = tls_storage_op_counters)
        counters->wt_search_near.fetch_add(1, std::memory_order_relaxed);
}

} // namespace eugraph
