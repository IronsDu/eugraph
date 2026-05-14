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
    bool openConnection(const std::string& db_path);

    /// Close WT connection and default session.
    void closeConnection();

    /// Get WT_SESSION for a transaction (or default session).
    WT_SESSION* getSession(GraphTxnHandle txn);

    /// Open a cursor on a table.
    WtCursor openCursor(WT_SESSION* session, const std::string& table_name);

    /// Table-level KV operations.
    bool tablePut(WT_SESSION* session, const std::string& table, std::string_view key, std::string_view value);
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

    std::mutex txnMutex_;
    std::unordered_map<GraphTxnHandle, std::unique_ptr<TxnState>> txns_;

public:
    /// Force a WT checkpoint (flush all committed data to disk).
    bool checkpoint();
};

} // namespace eugraph
