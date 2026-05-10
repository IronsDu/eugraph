#include "storage/wt_store_base.hpp"

#include <cerrno>
#include <chrono>
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
                ts->session.get()->rollback_transaction(ts->session.get(), nullptr);
            }
        }
        txns_.clear();
    }
    if (defaultSession_) {
        auto t0 = std::chrono::steady_clock::now();
        int ret = defaultSession_.get()->checkpoint(defaultSession_.get(), nullptr);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (ret != 0) {
            spdlog::warn("Checkpoint before close failed: error {} ({}ms)", ret, ms);
        } else {
            spdlog::info("Checkpoint completed ({}ms)", ms);
        }
    }
    defaultSession_ = WtSession{};
    conn_.close();
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

    auto t0 = std::chrono::steady_clock::now();

    // Enable WAL with fsync on commit for crash durability.
    // transaction_sync=(enabled=true) is required: without it, log records are
    // only buffered in memory and flushed on checkpoint or clean shutdown.
    if (!conn_.open(db_path, "create,log=(enabled=true),transaction_sync=(enabled=true,method=fsync)")) {
        return false;
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    spdlog::info("Opened WT connection at {} ({}ms)", db_path, ms);

    defaultSession_ = conn_.openSession();
    if (!defaultSession_) {
        spdlog::error("Failed to open default session");
        conn_.close();
        return false;
    }

    return true;
}

// ==================== Session/Cursor Helpers ====================

WT_SESSION* WtStoreBase::getSession(GraphTxnHandle txn) {
    if (txn == INVALID_GRAPH_TXN)
        return defaultSession_.get();
    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txns_.find(txn);
    return it != txns_.end() ? it->second->session.get() : nullptr;
}

WtCursor WtStoreBase::openCursor(WT_SESSION* session, const std::string& table_name) {
    return WtCursor(session, table_name);
}

void WtStoreBase::closeTxnCursors(TxnState* state) {
    if (!state)
        return;
    state->cursors.clear();
}

bool WtStoreBase::ensureGlobalTable(WT_SESSION* session, const char* table_name) {
    int ret = session->create(session, table_name, WT_TABLE_CONFIG);
    if (ret != 0 && ret != EBUSY) {
        spdlog::error("Failed to create table {}: error {}", table_name, ret);
        return false;
    }
    return true;
}

bool WtStoreBase::checkpoint() {
    if (!defaultSession_)
        return false;
    int ret = defaultSession_.get()->checkpoint(defaultSession_.get(), nullptr);
    if (ret != 0) {
        spdlog::error("Checkpoint failed: error {}", ret);
        return false;
    }
    return true;
}

// ==================== Table KV Operations ====================

bool WtStoreBase::tablePut(WT_SESSION* session, const std::string& table, std::string_view key,
                           std::string_view value) {
    auto cursor = openCursor(session, table);
    if (!cursor)
        return false;

    setItem(cursor.get(), key);
    setValueItem(cursor.get(), value);
    int ret = cursor.get()->insert(cursor.get());

    if (ret != 0) {
        spdlog::error("tablePut failed on {}: error {}", table, ret);
        return false;
    }
    return true;
}

std::optional<std::string> WtStoreBase::tableGet(WT_SESSION* session, const std::string& table, std::string_view key) {
    auto cursor = openCursor(session, table);
    if (!cursor)
        return std::nullopt;

    setItem(cursor.get(), key);
    int ret = cursor.get()->search(cursor.get());
    if (ret != 0) {
        return std::nullopt;
    }

    return getValueFromCursor(cursor.get());
}

bool WtStoreBase::tableDel(WT_SESSION* session, const std::string& table, std::string_view key) {
    auto cursor = openCursor(session, table);
    if (!cursor)
        return false;

    setItem(cursor.get(), key);
    int ret = cursor.get()->remove(cursor.get());

    if (ret != 0 && ret != WT_NOTFOUND) {
        spdlog::error("tableDel failed on {}: error {}", table, ret);
        return false;
    }
    return true;
}

void WtStoreBase::tableScan(WT_SESSION* session, const std::string& table, std::string_view prefix,
                            const std::function<bool(std::string_view, std::string_view)>& callback) {
    auto cursor = openCursor(session, table);
    if (!cursor)
        return;

    auto* c = cursor.get();
    std::string pfx(prefix);

    if (pfx.empty()) {
        int ret = c->reset(c);
        if (ret != 0)
            return;
        ret = c->next(c);
        while (ret == 0) {
            std::string key = getKeyFromCursor(c);
            std::string value = getValueFromCursor(c);
            if (!callback(key, value))
                break;
            ret = c->next(c);
        }
    } else {
        setItem(c, prefix);
        int exact = 0;
        int ret = c->search_near(c, &exact);

        if (ret != 0 || exact < 0) {
            ret = c->next(c);
        }

        while (ret == 0) {
            std::string key = getKeyFromCursor(c);
            if (!key.starts_with(prefix))
                break;

            std::string value = getValueFromCursor(c);
            if (!callback(key, value))
                break;

            ret = c->next(c);
        }
    }
}

} // namespace eugraph
