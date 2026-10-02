#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/wt_connection.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace eugraph {

/// Base class for WiredTiger-backed stores.
/// Provides shared WT connection management, session/cursor helpers,
/// transaction state, and table-level KV operations.
class WtStoreBase {
public:
    virtual ~WtStoreBase();

    WtStoreBase(const WtStoreBase&) = delete;
    WtStoreBase& operator=(const WtStoreBase&) = delete;

    bool isOpen() const {
        return conn_.operator bool();
    }

protected:
    WtStoreBase() = default;

    // Per-transaction state: own session + cursor cache
    struct TxnState {
        WtSession session;
        std::unordered_map<std::string, WtCursor> cursors;
    };

    /// Open WT connection and default session.
    /// extra_config is appended to the base WT config; when empty, the durable
    /// fsync commit default is applied.
    bool openConnection(const std::string& db_path, const std::string& extra_config = "");

    /// Close WT connection and default session.
    void closeConnection();

    /// Get WT_SESSION for a transaction (or default session).
    WT_SESSION* getSession(GraphTxnHandle txn);

    /// Open a cursor on a table.
    WtCursor openCursor(WT_SESSION* session, const std::string& table_name);

    /// Get a cached cursor for a transaction table (opens one on first use).
    WtCursor* getTxnCursor(GraphTxnHandle txn, WT_SESSION* session, const std::string& table_name);

    /// Table-level KV operations.
    bool tablePut(WT_SESSION* session, const std::string& table, std::string_view key, std::string_view value);
    /// Transaction-aware put that reuses a cursor cached in TxnState.
    bool tablePutTxn(GraphTxnHandle txn, WT_SESSION* session, const std::string& table, std::string_view key,
                     std::string_view value);
    std::optional<std::string> tableGet(WT_SESSION* session, const std::string& table, std::string_view key);
    bool tableDel(WT_SESSION* session, const std::string& table, std::string_view key);
    void tableScan(WT_SESSION* session, const std::string& table, std::string_view prefix,
                   const std::function<bool(std::string_view, std::string_view)>& callback);

    /// Close all cached cursors in a TxnState.
    void closeTxnCursors(TxnState* state);

    /// Ensure a global table exists (create if not). Uses its own short-lived session and takes
    /// `schemaMutex_`, so the DDL sequence (check exists -> create/drop) cannot interleave.
    bool ensureGlobalTable(const char* table_name);

    /// A session for one administrative/DDL operation: created on demand, closed when the returned
    /// wrapper dies. Never used on a per-row path (that would pay session open/close per call);
    /// high-frequency paths use `getSession(INVALID_GRAPH_TXN)` (the calling thread's session).
    /// Rationale and classification: docs/architecture/system-architecture.md ("目标设计：消除共享
    /// session").
    WtSession openAdminSession() {
        return WtSession(conn_.get());
    }

    WtConnection conn_;

    /// Serialises SCHEMA/LIFECYCLE sequences (create/drop table, checkpoint, close) -- i.e. business
    /// level exclusivity, NOT session memory. Every WT_SESSION is used by exactly one thread for its
    /// whole lifetime, so no session ever needs a lock; the KV primitives below therefore take none.
    mutable std::recursive_mutex schemaMutex_;

    /// Per-thread sessions for non-transactional access -- the ONLY session source for the KV
    /// primitives. WiredTiger sessions are not thread-safe; routing every thread through one
    /// shared session behind a mutex serialised the whole storage layer (8 threads: 0.65 M/s,
    /// 4.59 us/lookup versus 4.4-4.9 M/s, ~1.6 us/lookup with per-thread sessions).
    /// Handles of every per-thread session ever opened, so `closeThreadSessions()` can close
    /// them explicitly before the connection goes away. Not owning wrappers: a session's
    /// lifetime is bounded by the connection, never by the thread that opened it.
    std::mutex sessionPoolMutex_;
    std::vector<WT_SESSION*> sessionPool_;

    /// Session for the calling thread, created on first use. Owned by exactly this thread for its
    /// whole lifetime, so callers never take a lock on its behalf.
    WT_SESSION* threadSession();

    static uint64_t nextStoreId() {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    /// Wipe per-thread sessions (called while closing the connection) and bump this store's
    /// generation so every thread-local cache entry for THIS store is invalidated. Per-store, not
    /// process-wide: a process-wide counter would invalidate the cached session of every OTHER
    /// store too, so each thread would re-open a session on its next visit while the previous one
    /// stayed in that store's pool -- leaking sessions until WT reports "out of sessions".
    void closeThreadSessions();

    /// Starts at 0 and only ever grows; closing the store bumps it, so thread-local caches for
    /// THIS store are invalidated (their session has been closed).
    std::atomic<uint64_t> sessionGeneration_{0};

    /// Process-unique id, never reused. Thread-local session caches are keyed by this id rather
    /// than by the store's address: a dropped graph's store can be allocated at the very same
    /// address by the next graph, and an address-keyed cache would then match a stale entry and
    /// hand out a session belonging to the closed connection (that is a use-after-free, observed
    /// as a segfault inside WtCursor when creating a graph right after dropping one).
    const uint64_t sessionStoreId_ = nextStoreId();

    /// INVARIANT (docs/architecture/system-architecture.md, "目标设计：消除共享 session"):
    /// a `WT_SESSION` is used by exactly ONE thread for its whole lifetime, so
    /// `tableGet/tablePut/tableDel/tableScan` take NO lock. Their session comes from
    /// `getSession()`: a per-transaction session, or the calling thread's own session.
    /// Concurrency between business operations is handled by business-level locks
    /// (`schemaMutex_` for DDL/lifecycle), never by a session lock.
    /// Measured on 8 threads with the old shared session: 0.65 M/s at 4.59 us/lookup; with
    /// per-thread sessions: 4.4-4.9 M/s at ~1.6 us/lookup.

    std::mutex txnMutex_;
    std::unordered_map<GraphTxnHandle, std::unique_ptr<TxnState>> txns_;

public:
    /// Force a WT checkpoint (flush all committed data to disk).
    bool checkpoint();
};

} // namespace eugraph
