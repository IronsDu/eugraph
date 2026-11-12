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

/// Owning companion of `tls_storage_op_counters`.
///
/// SCOPE CONTRACT: this TLS is for **strictly nested, synchronous** scopes on
/// the storage IO thread -- `StorageOpMetricsScope` is constructed and destroyed
/// inside one dispatched lambda, never across a coroutine suspension, and the
/// thread-local is only read by the sync store below it. Do NOT install it from
/// coroutine code that can suspend: a coroutine resumes on a different compute
/// thread and can outlive its statement, so a saved raw pointer there would be
/// restored onto an unrelated thread and dangle (that crashed PROFILE +
/// concurrent queries, #310). The owning companion keeps the raw mirror valid
/// even if the scope's caller drops its reference first; it costs one refcount
/// pair per dispatch, not per WT call.
///
/// Compute-side "current operator counters" do NOT use TLS: they live in
/// `StorageProfileState`, which is owned per statement (see QueryContext).
inline thread_local std::shared_ptr<StorageOpCounters> tls_storage_op_counters_owner;

/// Per-statement holder for the counters a storage call issued *right now*
/// should be charged to: the traced operator installs its own counters while it
/// runs and restores the caller's when it suspends. A statement executes on one
/// coroutine at a time, so this needs no TLS, no atomics and cannot leak across
/// statements or threads -- which is exactly what the old thread-local design
/// got wrong (#310).
struct StorageProfileState {
    std::shared_ptr<StorageOpCounters> current;
};

class StorageOpMetricsScope {
public:
    explicit StorageOpMetricsScope(StorageOpCounters* counters) noexcept
        : previous_(tls_storage_op_counters), previous_owner_(tls_storage_op_counters_owner) {
        tls_storage_op_counters = counters;
        tls_storage_op_counters_owner.reset();
    }
    /// Preferred form whenever the counters are heap-owned (all PROFILE paths):
    /// the scope then guarantees the counter object outlives the install.
    explicit StorageOpMetricsScope(std::shared_ptr<StorageOpCounters> counters) noexcept
        : previous_(tls_storage_op_counters), previous_owner_(tls_storage_op_counters_owner) {
        tls_storage_op_counters = counters.get();
        tls_storage_op_counters_owner = std::move(counters);
    }
    ~StorageOpMetricsScope() {
        tls_storage_op_counters = previous_;
        tls_storage_op_counters_owner = std::move(previous_owner_);
    }
    StorageOpMetricsScope(const StorageOpMetricsScope&) = delete;
    StorageOpMetricsScope& operator=(const StorageOpMetricsScope&) = delete;

private:
    StorageOpCounters* previous_;
    std::shared_ptr<StorageOpCounters> previous_owner_;
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

inline void recordPrefetchHit(StorageOpCounters* counters) noexcept {
    if (counters != nullptr) {
        counters->prefetch_hits.fetch_add(1, std::memory_order_relaxed);
        if (counters->aggregate)
            counters->aggregate->prefetch_hits.fetch_add(1, std::memory_order_relaxed);
    }
}

inline void recordPrefetchMiss(StorageOpCounters* counters) noexcept {
    if (counters != nullptr) {
        counters->prefetch_misses.fetch_add(1, std::memory_order_relaxed);
        if (counters->aggregate)
            counters->aggregate->prefetch_misses.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace eugraph
