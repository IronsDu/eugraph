#include "storage/wt_store_base.hpp"

#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== WT Item Helpers ====================

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

WtStoreBase::~WtStoreBase() {
    closeConnection();
}

void WtStoreBase::closeConnection() {
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        for (auto& [h, ts] : txns_) {
            closeTxnCursors(ts.get());
            if (ts->session) {
                ts->session->rollback_transaction(ts->session, nullptr);
                ts->session->close(ts->session, nullptr);
            }
        }
        txns_.clear();
    }

    if (defaultSession_) {
        defaultSession_->close(defaultSession_, nullptr);
        defaultSession_ = nullptr;
    }
    if (conn_) {
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
    }
}

bool WtStoreBase::openConnection(const std::string& db_path) {
    if (conn_)
        return true;

    std::error_code ec;
    std::filesystem::create_directories(db_path, ec);
    if (ec) {
        spdlog::error("Failed to create database directory: {}", ec.message());
        return false;
    }

    // Enable WAL with fsync on commit for crash durability.
    // transaction_sync=(enabled=true) is required: without it, log records are
    // only buffered in memory and flushed on checkpoint or clean shutdown.
    int ret = wiredtiger_open(db_path.c_str(), nullptr,
                              "create,log=(enabled=true),transaction_sync=(enabled=true,method=fsync)", &conn_);
    if (ret != 0) {
        spdlog::error("Failed to open WiredTiger: error {}", ret);
        return false;
    }

    ret = conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
    if (ret != 0) {
        spdlog::error("Failed to open default session: error {}", ret);
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
        return false;
    }

    return true;
}

// ==================== Session/Cursor Helpers ====================

WT_SESSION* WtStoreBase::getSession(GraphTxnHandle txn) {
    if (txn == INVALID_GRAPH_TXN)
        return defaultSession_;
    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txns_.find(txn);
    return it != txns_.end() ? it->second->session : nullptr;
}

WT_CURSOR* WtStoreBase::getCursor(WT_SESSION* session, const std::string& table_name) {
    WT_CURSOR* cursor = nullptr;
    int ret = session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor on {}: error {}", table_name, ret);
        return nullptr;
    }
    return cursor;
}

void WtStoreBase::closeTxnCursors(TxnState* state) {
    if (!state)
        return;
    for (auto& [name, cursor] : state->cursors) {
        if (cursor)
            cursor->close(cursor);
    }
    state->cursors.clear();
}

bool WtStoreBase::ensureGlobalTable(WT_SESSION* session, const char* table_name) {
    int ret = session->create(session, table_name, WT_TABLE_CONFIG);
    if (ret != 0) {
        spdlog::error("Failed to create table {}: error {}", table_name, ret);
        return false;
    }
    return true;
}

bool WtStoreBase::checkpoint() {
    if (!defaultSession_)
        return false;
    int ret = defaultSession_->checkpoint(defaultSession_, nullptr);
    if (ret != 0) {
        spdlog::error("Checkpoint failed: error {}", ret);
        return false;
    }
    return true;
}

// ==================== Table KV Operations ====================

bool WtStoreBase::tablePut(WT_SESSION* session, const std::string& table, std::string_view key,
                           std::string_view value) {
    WT_CURSOR* cursor = getCursor(session, table);
    if (!cursor)
        return false;

    setItem(cursor, key);
    setValueItem(cursor, value);
    int ret = cursor->insert(cursor);
    cursor->close(cursor);

    if (ret != 0) {
        spdlog::error("tablePut failed on {}: error {}", table, ret);
        return false;
    }
    return true;
}

std::optional<std::string> WtStoreBase::tableGet(WT_SESSION* session, const std::string& table, std::string_view key) {
    WT_CURSOR* cursor = getCursor(session, table);
    if (!cursor)
        return std::nullopt;

    setItem(cursor, key);
    int ret = cursor->search(cursor);
    if (ret != 0) {
        cursor->close(cursor);
        return std::nullopt;
    }

    std::string value = getValueFromCursor(cursor);
    cursor->close(cursor);
    return value;
}

bool WtStoreBase::tableDel(WT_SESSION* session, const std::string& table, std::string_view key) {
    WT_CURSOR* cursor = getCursor(session, table);
    if (!cursor)
        return false;

    setItem(cursor, key);
    int ret = cursor->remove(cursor);
    cursor->close(cursor);

    if (ret != 0 && ret != WT_NOTFOUND) {
        spdlog::error("tableDel failed on {}: error {}", table, ret);
        return false;
    }
    return true;
}

void WtStoreBase::tableScan(WT_SESSION* session, const std::string& table, std::string_view prefix,
                            const std::function<bool(std::string_view, std::string_view)>& callback) {
    WT_CURSOR* cursor = getCursor(session, table);
    if (!cursor)
        return;

    std::string pfx(prefix);

    if (pfx.empty()) {
        int ret = cursor->reset(cursor);
        if (ret != 0) {
            cursor->close(cursor);
            return;
        }
        ret = cursor->next(cursor);
        while (ret == 0) {
            std::string key = getKeyFromCursor(cursor);
            std::string value = getValueFromCursor(cursor);
            if (!callback(key, value))
                break;
            ret = cursor->next(cursor);
        }
    } else {
        setItem(cursor, prefix);
        int exact = 0;
        int ret = cursor->search_near(cursor, &exact);

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
    }

    cursor->close(cursor);
}

} // namespace eugraph
