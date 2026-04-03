#pragma once

#include "storage/kv/kv_engine.hpp"
#include <memory>
#include <mutex>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/transaction_db.h>
#include <unordered_map>

namespace eugraph {

class RocksDBStore : public IKVEngine {
public:
    RocksDBStore() = default;
    ~RocksDBStore() override;

    RocksDBStore(const RocksDBStore&) = delete;
    RocksDBStore& operator=(const RocksDBStore&) = delete;

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
    rocksdb::Transaction* getTxn(TxnHandle handle);
    void removeTxn(TxnHandle handle);

    std::unique_ptr<rocksdb::TransactionDB> txnDB_;
    std::mutex txnMutex_;
    std::unordered_map<TxnHandle, std::unique_ptr<rocksdb::Transaction>> txns_;
};

/// IScanCursor implementation backed by a RocksDB iterator.
/// Member order ensures iter_ is destroyed before upper_bound_str_.
class RocksDBScanCursor : public IKVEngine::IScanCursor {
public:
    RocksDBScanCursor(rocksdb::TransactionDB* db, std::string prefix);
    ~RocksDBScanCursor() override;

    RocksDBScanCursor(const RocksDBScanCursor&) = delete;
    RocksDBScanCursor& operator=(const RocksDBScanCursor&) = delete;

    bool valid() const override;
    std::string_view key() const override;
    std::string_view value() const override;
    void next() override;

private:
    std::string prefix_;
    std::string upper_bound_str_;
    rocksdb::Slice upper_bound_slice_;
    std::unique_ptr<rocksdb::Iterator> iter_;
    bool valid_ = false;
};

} // namespace eugraph
