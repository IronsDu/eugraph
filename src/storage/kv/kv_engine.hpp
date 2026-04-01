#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <functional>

namespace eugraph {

// Abstract KV storage engine interface.
// Provides the minimal set of KV operations required by the graph storage layer.
  // Implementations: RocksDBStore, WiredTigerStore (future).
class IKVEngine {
public:
    virtual ~IKVEngine() = default;

    // ==================== Lifecycle ====================
    virtual bool open(const std::string& db_path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // ==================== Basic KV ====================
    virtual bool put(std::string_view key, std::string_view value) = 0;
    virtual std::optional<std::string> get(std::string_view key) = 0;
    virtual bool del(std::string_view key) = 0;
    // ==================== Batch Write ====================
    virtual bool putBatch(
        const std::vector<std::pair<std::string, std::string>>& kv_pairs) = 0;

    // ==================== Prefix Scan ====================
    struct KeyValuePair {
        std::string key;
        std::string value;
    };

    virtual std::vector<KeyValuePair> prefixScan(std::string_view prefix) = 0;
    virtual void prefixScan(
        std::string_view prefix,
        const std::function<bool(std::string_view key, std::string_view value)>& callback) = 0;

    // ==================== Transaction ====================
    using TxnHandle = void*;
    static constexpr TxnHandle INVALID_TXN = nullptr;

    virtual TxnHandle beginTransaction() = 0;
    virtual bool commitTransaction(TxnHandle txn) = 0;
    virtual bool rollbackTransaction(TxnHandle txn) = 0;
    // Transaction-scoped KV operations
    virtual bool put(TxnHandle txn, std::string_view key, std::string_view value) = 0;
    virtual std::optional<std::string> get(TxnHandle txn, std::string_view key) = 0;
    virtual bool del(TxnHandle txn, std::string_view key) = 0;
};

} // namespace eugraph
