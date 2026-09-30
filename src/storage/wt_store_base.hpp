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

    /// Ensure a global table exists (create if not).
    bool ensureGlobalTable(WT_SESSION* session, const char* table_name);

    WtConnection conn_;
    WtSession defaultSession_;
    mutable std::recursive_mutex sessionMutex_; // protects defaultSession_ from concurrent use

    /// Per-thread sessions for non-transactional access. WiredTiger sessions are not
    /// thread-safe, and routing every thread through the single shared `defaultSession_`
    /// behind `sessionMutex_` serialised the whole storage layer: measured on 8 threads,
    /// per-lookup throughput fell from 1.64 to 0.65 M/s and CPU per lookup rose from
    /// 0.61 to 4.59 us. Each thread now owns a session, so the mutex is only taken for the
    /// (rare) shared-session paths.
    /// Handles of every per-thread session ever opened, so `closeThreadSessions()` can close
    /// them explicitly before the connection goes away. Not owning wrappers: a session's
    /// lifetime is bounded by the connection, never by the thread that opened it.
    std::mutex sessionPoolMutex_;
    std::vector<WT_SESSION*> sessionPool_;

    /// Session for the calling thread, created on first use. Never the shared session, so
    /// callers of this must not take `sessionMutex_` on its behalf.
    WT_SESSION* threadSession();
    /// Wipe per-thread sessions (called while closing the connection).
    void closeThreadSessions();

    /// NOTE: `tableGet/tablePut/tableDel/tableScan` deliberately do NOT take
    /// `sessionMutex_`. Their session always comes from `getSession()`, which returns a
    /// per-transaction session or a per-thread session -- never the shared
    /// `defaultSession_` -- so no cross-thread session is involved and the global lock
    /// would only serialise every storage operation process-wide. Measured on 8 threads
    /// before this change: per-lookup throughput 0.65 M/s and CPU 4.59 us/lookup; after:
    /// 4.41 M/s and 1.68 us/lookup. Callers that must use the shared session go through
    /// `ensureGlobalTable()`, which takes `sessionMutex_` itself.

    std::mutex txnMutex_;
    std::unordered_map<GraphTxnHandle, std::unique_ptr<TxnState>> txns_;

public:
    /// Force a WT checkpoint (flush all committed data to disk).
    bool checkpoint();
};

} // namespace eugraph
