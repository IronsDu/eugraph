#include "storage/kv/wiredtiger_store.hpp"

#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== Helpers ====================

namespace {

void setItem(WT_CURSOR* cursor, std::string_view data) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = data.data();
    item.size = data.size();
    cursor->set_key(cursor, &item);
}

void setValueItem(WT_CURSOR* cursor, std::string_view data) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = data.data();
    item.size = data.size();
    cursor->set_value(cursor, &item);
}

std::string getValueFromCursor(WT_CURSOR* cursor) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    cursor->get_value(cursor, &item);
    return std::string(static_cast<const char*>(item.data), item.size);
}

std::string getKeyFromCursor(WT_CURSOR* cursor) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    cursor->get_key(cursor, &item);
    return std::string(static_cast<const char*>(item.data), item.size);
}

} // anonymous namespace

// ==================== Lifecycle ====================

WiredTigerStore::~WiredTigerStore() {
    close();
}

bool WiredTigerStore::open(const std::string& db_path) {
    if (conn_)
        return true;

    std::error_code ec;
    std::filesystem::create_directories(db_path, ec);
    if (ec) {
        spdlog::error("Failed to create database directory: {}", ec.message());
        return false;
    }

    int ret = wiredtiger_open(db_path.c_str(), nullptr, "create", &conn_);
    if (ret != 0) {
        spdlog::error("Failed to open WiredTiger: error {}", ret);
        return false;
    }

    // Open default session
    ret = conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
    if (ret != 0) {
        spdlog::error("Failed to open default session: error {}", ret);
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
        return false;
    }

    // Create the KV table if not exists
    ret = defaultSession_->create(defaultSession_, TABLE_NAME, TABLE_CONFIG);
    if (ret != 0) {
        spdlog::error("Failed to create table: error {}", ret);
        defaultSession_->close(defaultSession_, nullptr);
        conn_->close(conn_, nullptr);
        defaultSession_ = nullptr;
        conn_ = nullptr;
        return false;
    }

    // Open default cursor
    ret = defaultSession_->open_cursor(defaultSession_, TABLE_NAME, nullptr, nullptr, &defaultCursor_);
    if (ret != 0) {
        spdlog::error("Failed to open default cursor: error {}", ret);
        defaultSession_->close(defaultSession_, nullptr);
        conn_->close(conn_, nullptr);
        defaultSession_ = nullptr;
        conn_ = nullptr;
        return false;
    }

    spdlog::info("Opened WiredTiger at: {}", db_path);
    return true;
}

void WiredTigerStore::close() {
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        for (auto& [h, ts] : txns_) {
            if (ts->cursor)
                ts->cursor->close(ts->cursor);
            if (ts->session)
                ts->session->rollback_transaction(ts->session, nullptr);
            if (ts->session)
                ts->session->close(ts->session, nullptr);
        }
        txns_.clear();
    }

    if (defaultCursor_) {
        defaultCursor_->close(defaultCursor_);
        defaultCursor_ = nullptr;
    }
    if (defaultSession_) {
        defaultSession_->close(defaultSession_, nullptr);
        defaultSession_ = nullptr;
    }
    if (conn_) {
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
        spdlog::info("Closed WiredTiger");
    }
}

bool WiredTigerStore::isOpen() const {
    return conn_ != nullptr;
}

// ==================== Basic KV ====================

bool WiredTigerStore::put(std::string_view key, std::string_view value) {
    if (!defaultCursor_)
        return false;

    setItem(defaultCursor_, key);
    setValueItem(defaultCursor_, value);
    int ret = defaultCursor_->insert(defaultCursor_);
    if (ret != 0) {
        spdlog::error("WiredTiger put failed: error {}", ret);
        return false;
    }
    return true;
}

std::optional<std::string> WiredTigerStore::get(std::string_view key) {
    if (!defaultCursor_)
        return std::nullopt;

    setItem(defaultCursor_, key);
    int ret = defaultCursor_->search(defaultCursor_);
    if (ret != 0)
        return std::nullopt;

    return getValueFromCursor(defaultCursor_);
}

bool WiredTigerStore::del(std::string_view key) {
    if (!defaultCursor_)
        return false;

    setItem(defaultCursor_, key);
    int ret = defaultCursor_->remove(defaultCursor_);
    if (ret != 0 && ret != WT_NOTFOUND) {
        spdlog::error("WiredTiger delete failed: error {}", ret);
        return false;
    }
    return true;
}

// ==================== Batch Write ====================

bool WiredTigerStore::putBatch(const std::vector<std::pair<std::string, std::string>>& kv_pairs) {
    if (!defaultSession_)
        return false;
    if (kv_pairs.empty())
        return true;

    // Use a temporary cursor on the default session for batch writes
    WT_CURSOR* cursor = nullptr;
    int ret = defaultSession_->open_cursor(defaultSession_, TABLE_NAME, nullptr, nullptr, &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor for batch write: error {}", ret);
        return false;
    }

    for (const auto& [k, v] : kv_pairs) {
        setItem(cursor, k);
        setValueItem(cursor, v);
        ret = cursor->insert(cursor);
        if (ret != 0) {
            spdlog::error("WiredTiger batch put failed: error {}", ret);
            cursor->close(cursor);
            return false;
        }
    }

    cursor->close(cursor);
    return true;
}

// ==================== Prefix Scan ====================

std::vector<IKVEngine::KeyValuePair> WiredTigerStore::prefixScan(std::string_view prefix) {
    std::vector<KeyValuePair> results;
    prefixScan(prefix, [&results](std::string_view key, std::string_view value) {
        results.push_back({std::string(key), std::string(value)});
        return true;
    });
    return results;
}

void WiredTigerStore::prefixScan(std::string_view prefix,
                                 const std::function<bool(std::string_view, std::string_view)>& callback) {
    if (!defaultSession_)
        return;

    WT_CURSOR* cursor = nullptr;
    int ret = defaultSession_->open_cursor(defaultSession_, TABLE_NAME, nullptr, nullptr, &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor for scan: error {}", ret);
        return;
    }

    std::string pfx(prefix);

    // Use search_near to position at or near the prefix
    setItem(cursor, prefix);
    int exact = 0;
    ret = cursor->search_near(cursor, &exact);

    // If search_near lands before the prefix range, advance
    if (ret != 0 || exact < 0) {
        ret = cursor->next(cursor);
    }

    while (ret == 0) {
        std::string key = getKeyFromCursor(cursor);
        if (!key.starts_with(prefix))
            break;

        std::string value = getValueFromCursor(cursor);
        if (!callback(key, value))
            break;

        ret = cursor->next(cursor);
    }

    cursor->close(cursor);
}

// ==================== Scan Cursor ====================

std::unique_ptr<IKVEngine::IScanCursor> WiredTigerStore::createScanCursor(std::string_view prefix) {
    if (!defaultSession_)
        return nullptr;
    return std::make_unique<WiredTigerScanCursor>(defaultSession_, std::string(prefix));
}

// ==================== Transaction ====================

IKVEngine::TxnHandle WiredTigerStore::beginTransaction() {
    if (!conn_)
        return INVALID_TXN;

    auto txnSession = std::make_unique<TxnSession>();

    int ret = conn_->open_session(conn_, nullptr, nullptr, &txnSession->session);
    if (ret != 0) {
        spdlog::error("Failed to open transaction session: error {}", ret);
        return INVALID_TXN;
    }

    ret = txnSession->session->begin_transaction(txnSession->session, "isolation=snapshot");
    if (ret != 0) {
        spdlog::error("Failed to begin transaction: error {}", ret);
        txnSession->session->close(txnSession->session, nullptr);
        return INVALID_TXN;
    }

    ret = txnSession->session->open_cursor(txnSession->session, TABLE_NAME, nullptr, nullptr, &txnSession->cursor);
    if (ret != 0) {
        spdlog::error("Failed to open transaction cursor: error {}", ret);
        txnSession->session->rollback_transaction(txnSession->session, nullptr);
        txnSession->session->close(txnSession->session, nullptr);
        return INVALID_TXN;
    }

    auto handle = static_cast<TxnHandle>(txnSession.get());
    std::lock_guard<std::mutex> lock(txnMutex_);
    txns_.emplace(handle, std::move(txnSession));
    return handle;
}

bool WiredTigerStore::commitTransaction(TxnHandle txn) {
    auto ts = getTxnSession(txn);
    if (!ts)
        return false;

    int ret = ts->session->commit_transaction(ts->session, nullptr);
    removeTxnSession(txn);

    if (ret != 0) {
        spdlog::error("WiredTiger commit failed: error {}", ret);
        return false;
    }
    return true;
}

bool WiredTigerStore::rollbackTransaction(TxnHandle txn) {
    auto ts = getTxnSession(txn);
    if (!ts)
        return false;

    int ret = ts->session->rollback_transaction(ts->session, nullptr);
    removeTxnSession(txn);

    if (ret != 0) {
        spdlog::error("WiredTiger rollback failed: error {}", ret);
        return false;
    }
    return true;
}

bool WiredTigerStore::put(TxnHandle txn, std::string_view key, std::string_view value) {
    auto ts = getTxnSession(txn);
    if (!ts || !ts->cursor)
        return false;

    setItem(ts->cursor, key);
    setValueItem(ts->cursor, value);
    int ret = ts->cursor->insert(ts->cursor);
    if (ret != 0) {
        spdlog::error("WiredTiger txn put failed: error {}", ret);
        return false;
    }
    return true;
}

std::optional<std::string> WiredTigerStore::get(TxnHandle txn, std::string_view key) {
    auto ts = getTxnSession(txn);
    if (!ts || !ts->cursor)
        return std::nullopt;

    setItem(ts->cursor, key);
    int ret = ts->cursor->search(ts->cursor);
    if (ret != 0)
        return std::nullopt;

    return getValueFromCursor(ts->cursor);
}

bool WiredTigerStore::del(TxnHandle txn, std::string_view key) {
    auto ts = getTxnSession(txn);
    if (!ts || !ts->cursor)
        return false;

    setItem(ts->cursor, key);
    int ret = ts->cursor->remove(ts->cursor);
    if (ret != 0 && ret != WT_NOTFOUND) {
        spdlog::error("WiredTiger txn delete failed: error {}", ret);
        return false;
    }
    return true;
}

// ==================== Private ====================

WiredTigerStore::TxnSession* WiredTigerStore::getTxnSession(TxnHandle handle) {
    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txns_.find(handle);
    if (it == txns_.end())
        return nullptr;
    return it->second.get();
}

void WiredTigerStore::removeTxnSession(TxnHandle handle) {
    std::lock_guard<std::mutex> lock(txnMutex_);
    txns_.erase(handle);
}

// ==================== WiredTigerScanCursor ====================

WiredTigerScanCursor::WiredTigerScanCursor(WT_SESSION* session, std::string prefix)
    : prefix_(std::move(prefix)), session_(session) {
    int ret = session_->open_cursor(session_, "table:eugraph_kv", nullptr, nullptr, &cursor_);
    if (ret != 0) {
        spdlog::error("Failed to open scan cursor: error {}", ret);
        return;
    }

    // Position at or near the prefix
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = prefix_.data();
    item.size = prefix_.size();
    cursor_->set_key(cursor_, &item);

    int exact = 0;
    ret = cursor_->search_near(cursor_, &exact);

    if (ret != 0 || exact < 0) {
        ret = cursor_->next(cursor_);
    }

    if (ret == 0) {
        readCurrent();
    }
}

WiredTigerScanCursor::~WiredTigerScanCursor() {
    if (cursor_)
        cursor_->close(cursor_);
}

void WiredTigerScanCursor::readCurrent() {
    WT_ITEM key_item, val_item;
    std::memset(&key_item, 0, sizeof(key_item));
    std::memset(&val_item, 0, sizeof(val_item));
    cursor_->get_key(cursor_, &key_item);
    cursor_->get_value(cursor_, &val_item);
    currentKey_ = std::string(static_cast<const char*>(key_item.data), key_item.size);
    currentValue_ = std::string(static_cast<const char*>(val_item.data), val_item.size);
    valid_ = currentKey_.starts_with(prefix_);
}

bool WiredTigerScanCursor::valid() const {
    return valid_;
}

std::string_view WiredTigerScanCursor::key() const {
    return currentKey_;
}

std::string_view WiredTigerScanCursor::value() const {
    return currentValue_;
}

void WiredTigerScanCursor::next() {
    int ret = cursor_->next(cursor_);
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

} // namespace eugraph
