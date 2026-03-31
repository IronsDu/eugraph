#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <functional>

#include <wiredtiger.h>

namespace eugraph {

// RAII wrapper around WiredTiger connection, session, and cursor operations.
// Provides synchronous KV operations: put, get, del, prefix scan.
class WiredTigerStore {
public:
    explicit WiredTigerStore(const std::string& db_path);
    ~WiredTigerStore();

    WiredTigerStore(const WiredTigerStore&) = delete;
    WiredTigerStore& operator=(const WiredTigerStore&) = delete;

    // Open the database connection. Must be called before any other operation.
    bool open();

    // Close the database connection.
    void close();

    bool isOpen() const { return conn_ != nullptr; }

    // ==================== Basic KV Operations ====================

    bool put(std::string_view key, std::string_view value);
    std::optional<std::string> get(std::string_view key);
    bool del(std::string_view key);

    // ==================== Batch Operations ====================

    // Put multiple key-value pairs in a single transaction.
    bool putBatch(const std::vector<std::pair<std::string, std::string>>& kv_pairs);

    // ==================== Prefix Scan ====================

    struct KeyValuePair {
        std::string key;
        std::string value;
    };

    // Scan all key-value pairs with the given prefix.
    // Returns results as a vector.
    std::vector<KeyValuePair> prefixScan(std::string_view prefix);

    // Scan with callback (avoids loading all results into memory).
    // Callback returns false to stop iteration.
    void prefixScan(std::string_view prefix,
                    const std::function<bool(std::string_view key, std::string_view value)>& callback);

    // ==================== Transaction Support ====================

    // Begin a transaction. Returns a session pointer used for subsequent txn ops.
    WT_SESSION* beginTransaction();

    // Commit a transaction.
    bool commitTransaction(WT_SESSION* session);

    // Rollback a transaction.
    bool rollbackTransaction(WT_SESSION* session);

    // Transaction-scoped KV operations (use the txn session).
    bool put(WT_SESSION* session, std::string_view key, std::string_view value);
    std::optional<std::string> get(WT_SESSION* session, std::string_view key);
    bool del(WT_SESSION* session, std::string_view key);

private:
    std::string db_path_;
    WT_CONNECTION* conn_ = nullptr;
    WT_SESSION* default_session_ = nullptr;

    bool ensureOpen();
};

} // namespace eugraph
