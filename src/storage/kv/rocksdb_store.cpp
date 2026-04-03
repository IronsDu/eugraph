#include "storage/kv/rocksdb_store.hpp"

#include <filesystem>
#include <spdlog/spdlog.h>

namespace eugraph {

RocksDBStore::~RocksDBStore() {
    close();
}

bool RocksDBStore::open(const std::string& db_path) {
    if (txnDB_)
        return true;

    std::error_code ec;
    std::filesystem::create_directories(db_path, ec);
    if (ec) {
        spdlog::error("Failed to create database directory: {}", ec.message());
        return false;
    }

    rocksdb::Options options;
    options.create_if_missing = true;

    rocksdb::TransactionDBOptions txnOpts;

    rocksdb::TransactionDB* rawDB = nullptr;
    auto status = rocksdb::TransactionDB::Open(options, txnOpts, db_path, &rawDB);
    if (!status.ok()) {
        spdlog::error("Failed to open RocksDB: {}", status.ToString());
        return false;
    }

    txnDB_.reset(rawDB);
    spdlog::info("Opened RocksDB at: {}", db_path);
    return true;
}

void RocksDBStore::close() {
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        for (auto& [h, txn] : txns_) {
            txn->Rollback();
        }
        txns_.clear();
    }

    if (txnDB_) {
        txnDB_.reset();
        spdlog::info("Closed RocksDB");
    }
}

bool RocksDBStore::isOpen() const {
    return txnDB_ != nullptr;
}

// ==================== Basic KV ====================

bool RocksDBStore::put(std::string_view key, std::string_view value) {
    if (!txnDB_)
        return false;
    auto s = txnDB_->Put(rocksdb::WriteOptions(), key, value);
    if (!s.ok()) {
        spdlog::error("RocksDB put failed: {}", s.ToString());
        return false;
    }
    return true;
}

std::optional<std::string> RocksDBStore::get(std::string_view key) {
    if (!txnDB_)
        return std::nullopt;
    std::string value;
    auto s = txnDB_->Get(rocksdb::ReadOptions(), key, &value);
    if (!s.ok())
        return std::nullopt;
    return value;
}

bool RocksDBStore::del(std::string_view key) {
    if (!txnDB_)
        return false;
    auto s = txnDB_->Delete(rocksdb::WriteOptions(), key);
    if (!s.ok()) {
        spdlog::error("RocksDB delete failed: {}", s.ToString());
        return false;
    }
    return true;
}

// ==================== Batch Write ====================

bool RocksDBStore::putBatch(const std::vector<std::pair<std::string, std::string>>& kv_pairs) {
    if (!txnDB_)
        return false;
    if (kv_pairs.empty())
        return true;

    rocksdb::WriteBatch batch;
    for (const auto& [k, v] : kv_pairs) {
        batch.Put(k, v);
    }
    auto s = txnDB_->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) {
        spdlog::error("RocksDB batch write failed: {}", s.ToString());
        return false;
    }
    return true;
}

// ==================== Prefix Scan ====================

std::vector<IKVEngine::KeyValuePair> RocksDBStore::prefixScan(std::string_view prefix) {
    std::vector<KeyValuePair> results;
    prefixScan(prefix, [&results](std::string_view key, std::string_view value) {
        results.push_back({std::string(key), std::string(value)});
        return true;
    });
    return results;
}

void RocksDBStore::prefixScan(std::string_view prefix,
                              const std::function<bool(std::string_view, std::string_view)>& callback) {
    if (!txnDB_)
        return;

    rocksdb::ReadOptions opts;
    opts.total_order_seek = true;

    // Compute upper bound: increment the last non-0xFF byte
    std::string ub;
    if (!prefix.empty()) {
        ub = std::string(prefix);
        bool carry = true;
        for (int i = static_cast<int>(ub.size()) - 1; i >= 0 && carry; --i) {
            auto& byte = ub[static_cast<size_t>(i)];
            if (static_cast<uint8_t>(byte) < 0xFF) {
                ++byte;
                carry = false;
            } else {
                byte = '\0';
            }
        }
        if (carry)
            ub.clear(); // all 0xFF, no upper bound
    }

    rocksdb::Slice upper;
    if (!ub.empty()) {
        upper = rocksdb::Slice(ub);
        opts.iterate_upper_bound = &upper;
    }

    auto iter = std::unique_ptr<rocksdb::Iterator>(txnDB_->NewIterator(opts));
    std::string pfx(prefix);
    iter->Seek(pfx);

    while (iter->Valid()) {
        auto key = iter->key();
        if (!key.starts_with(prefix))
            break;

        auto val = iter->value();
        if (!callback(key.ToStringView(), val.ToStringView()))
            break;

        iter->Next();
    }
}

// ==================== Transaction ====================

IKVEngine::TxnHandle RocksDBStore::beginTransaction() {
    if (!txnDB_)
        return INVALID_TXN;

    rocksdb::TransactionOptions txnOpts;
    auto txn = txnDB_->BeginTransaction(rocksdb::WriteOptions(), txnOpts);
    if (!txn) {
        spdlog::error("Failed to begin RocksDB transaction");
        return INVALID_TXN;
    }

    auto handle = static_cast<TxnHandle>(txn);
    std::lock_guard<std::mutex> lock(txnMutex_);
    txns_.emplace(handle, std::unique_ptr<rocksdb::Transaction>(txn));
    return handle;
}

bool RocksDBStore::commitTransaction(TxnHandle txn) {
    auto rocksTxn = getTxn(txn);
    if (!rocksTxn)
        return false;

    auto s = rocksTxn->Commit();
    removeTxn(txn);

    if (!s.ok()) {
        spdlog::error("RocksDB commit failed: {}", s.ToString());
        return false;
    }
    return true;
}

bool RocksDBStore::rollbackTransaction(TxnHandle txn) {
    auto rocksTxn = getTxn(txn);
    if (!rocksTxn)
        return false;

    auto s = rocksTxn->Rollback();
    removeTxn(txn);

    if (!s.ok()) {
        spdlog::error("RocksDB rollback failed: {}", s.ToString());
        return false;
    }
    return true;
}

bool RocksDBStore::put(TxnHandle txn, std::string_view key, std::string_view value) {
    auto rocksTxn = getTxn(txn);
    if (!rocksTxn)
        return false;

    auto s = rocksTxn->Put(key, value);
    if (!s.ok()) {
        spdlog::error("RocksDB txn put failed: {}", s.ToString());
        return false;
    }
    return true;
}

std::optional<std::string> RocksDBStore::get(TxnHandle txn, std::string_view key) {
    auto rocksTxn = getTxn(txn);
    if (!rocksTxn)
        return std::nullopt;

    std::string value;
    auto s = rocksTxn->Get(rocksdb::ReadOptions(), key, &value);
    if (!s.ok())
        return std::nullopt;
    return value;
}

bool RocksDBStore::del(TxnHandle txn, std::string_view key) {
    auto rocksTxn = getTxn(txn);
    if (!rocksTxn)
        return false;

    auto s = rocksTxn->Delete(key);
    if (!s.ok()) {
        spdlog::error("RocksDB txn delete failed: {}", s.ToString());
        return false;
    }
    return true;
}

// ==================== Private ====================

rocksdb::Transaction* RocksDBStore::getTxn(TxnHandle handle) {
    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txns_.find(handle);
    if (it == txns_.end())
        return nullptr;
    return it->second.get();
}

void RocksDBStore::removeTxn(TxnHandle handle) {
    std::lock_guard<std::mutex> lock(txnMutex_);
    txns_.erase(handle);
}

// ==================== Scan Cursor ====================

std::unique_ptr<IKVEngine::IScanCursor> RocksDBStore::createScanCursor(std::string_view prefix) {
    if (!txnDB_)
        return nullptr;
    return std::make_unique<RocksDBScanCursor>(txnDB_.get(), std::string(prefix));
}

namespace {

std::string computeUpperBound(std::string_view prefix) {
    std::string ub(prefix);
    bool carry = true;
    for (int i = static_cast<int>(ub.size()) - 1; i >= 0 && carry; --i) {
        auto& byte = ub[static_cast<size_t>(i)];
        if (static_cast<uint8_t>(byte) < 0xFF) {
            ++byte;
            carry = false;
        } else {
            byte = '\0';
        }
    }
    if (carry)
        ub.clear();
    return ub;
}

} // anonymous namespace

RocksDBScanCursor::RocksDBScanCursor(rocksdb::TransactionDB* db, std::string prefix) : prefix_(std::move(prefix)) {
    rocksdb::ReadOptions opts;
    opts.total_order_seek = true;

    upper_bound_str_ = computeUpperBound(prefix_);
    if (!upper_bound_str_.empty()) {
        upper_bound_slice_ = rocksdb::Slice(upper_bound_str_);
        opts.iterate_upper_bound = &upper_bound_slice_;
    }

    iter_.reset(db->NewIterator(opts));
    iter_->Seek(prefix_);

    valid_ = iter_->Valid();
    if (valid_) {
        auto k = iter_->key();
        valid_ = k.starts_with(prefix_);
    }
}

RocksDBScanCursor::~RocksDBScanCursor() = default;

bool RocksDBScanCursor::valid() const {
    return valid_;
}

std::string_view RocksDBScanCursor::key() const {
    return iter_->key().ToStringView();
}

std::string_view RocksDBScanCursor::value() const {
    return iter_->value().ToStringView();
}

void RocksDBScanCursor::next() {
    iter_->Next();
    valid_ = iter_->Valid();
    if (valid_) {
        auto k = iter_->key();
        valid_ = k.starts_with(prefix_);
    }
}

} // namespace eugraph
