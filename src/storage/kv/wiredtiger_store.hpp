#pragma once

#include "storage/kv/kv_engine.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <wiredtiger.h>

namespace eugraph {

class WiredTigerStore : public IKVEngine {
public:
    WiredTigerStore() = default;
    ~WiredTigerStore() override;

    WiredTigerStore(const WiredTigerStore&) = delete;
    WiredTigerStore& operator=(const WiredTigerStore&) = delete;

    // ==================== Lifecycle ====================

    bool open(const std::string& db_path) override;
    void close() override;
    bool isOpen() const override;

    // ==================== Basic KV ====================

    bool put(std::string_view key, std::string_view value) override;
    std::optional<std::string> get(std::string_view key) override;
    bool del(std::string_view key) override;

    // ==================== Batch Write ====================

    bool putBatch(const std::vector<std::pair<std::string, std::string>>& kv_pairs) override;

    // ==================== Prefix Scan ====================

    std::vector<KeyValuePair> prefixScan(std::string_view prefix) override;
    void prefixScan(std::string_view prefix,
                    const std::function<bool(std::string_view key, std::string_view value)>& callback) override;

    // ==================== Scan Cursor ====================

    std::unique_ptr<IScanCursor> createScanCursor(std::string_view prefix) override;

    // ==================== Transaction ====================

    TxnHandle beginTransaction() override;
    bool commitTransaction(TxnHandle txn) override;
    bool rollbackTransaction(TxnHandle txn) override;
    bool put(TxnHandle txn, std::string_view key, std::string_view value) override;
    std::optional<std::string> get(TxnHandle txn, std::string_view key) override;
    bool del(TxnHandle txn, std::string_view key) override;

private:
    // Internal session wrapper for transaction support
    struct TxnSession {
        WT_SESSION* session = nullptr;
        WT_CURSOR* cursor = nullptr;
    };

    TxnSession* getTxnSession(TxnHandle handle);
    void removeTxnSession(TxnHandle handle);

    WT_CONNECTION* conn_ = nullptr;
    // Default session for non-transactional operations
    WT_SESSION* defaultSession_ = nullptr;
    WT_CURSOR* defaultCursor_ = nullptr;

    std::mutex txnMutex_;
    std::unordered_map<TxnHandle, std::unique_ptr<TxnSession>> txns_;

    static constexpr const char* TABLE_NAME = "table:eugraph_kv";
    static constexpr const char* TABLE_CONFIG = "key_format=u,value_format=u";
};

/// IScanCursor implementation backed by a WiredTiger cursor.
class WiredTigerScanCursor : public IKVEngine::IScanCursor {
public:
    WiredTigerScanCursor(WT_SESSION* session, std::string prefix);
    ~WiredTigerScanCursor() override;

    WiredTigerScanCursor(const WiredTigerScanCursor&) = delete;
    WiredTigerScanCursor& operator=(const WiredTigerScanCursor&) = delete;

    bool valid() const override;
    std::string_view key() const override;
    std::string_view value() const override;
    void next() override;

private:
    void advance();
    void readCurrent();

    std::string prefix_;
    WT_SESSION* session_ = nullptr;
    WT_CURSOR* cursor_ = nullptr;
    bool valid_ = false;
    // Current key/value held as strings since WT_ITEM data is only valid until next operation
    std::string currentKey_;
    std::string currentValue_;
};

} // namespace eugraph
