#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <wiredtiger.h>

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
        return conn_ != nullptr;
    }

protected:
    WtStoreBase() = default;

    // Per-transaction state: own session + cursor cache
    struct TxnState {
        WT_SESSION* session = nullptr;
        std::unordered_map<std::string, WT_CURSOR*> cursors;
    };

    /// Open WT connection and default session.
    bool openConnection(const std::string& db_path);

    /// Close WT connection and default session.
    void closeConnection();

    /// Get WT_SESSION for a transaction (or default session).
    WT_SESSION* getSession(GraphTxnHandle txn);

    /// Open a cursor on a table.
    WT_CURSOR* getCursor(WT_SESSION* session, const std::string& table_name);

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

    WT_CONNECTION* conn_ = nullptr;
    WT_SESSION* defaultSession_ = nullptr;

    std::mutex txnMutex_;
    std::unordered_map<GraphTxnHandle, std::unique_ptr<TxnState>> txns_;
};

} // namespace eugraph
