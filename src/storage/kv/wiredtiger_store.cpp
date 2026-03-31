#include "storage/kv/wiredtiger_store.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <cstring>

namespace eugraph {

static WT_ITEM makeItem(const void* data, size_t size) {
    WT_ITEM item{};
    item.data = data;
    item.size = size;
    return item;
}

WiredTigerStore::WiredTigerStore(const std::string& db_path)
    : db_path_(db_path) {}

WiredTigerStore::~WiredTigerStore() {
    close();
}

bool WiredTigerStore::open() {
    if (conn_) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(db_path_, ec);
    if (ec) {
        spdlog::error("Failed to create database directory: {}", ec.message());
        return false;
    }

    int ret = wiredtiger_open(db_path_.c_str(), nullptr, "create", &conn_);
    if (ret != 0) {
        spdlog::error("Failed to open WiredTiger connection: {}", wiredtiger_strerror(ret));
        return false;
    }

    ret = conn_->open_session(conn_, nullptr, nullptr, &default_session_);
    if (ret != 0) {
        spdlog::error("Failed to open default session: {}", wiredtiger_strerror(ret));
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
        return false;
    }

    ret = default_session_->create(
        default_session_, "table:kv",
        "key_format=u,value_format=u");
    if (ret != 0) {
        spdlog::error("Failed to create KV table: {}", wiredtiger_strerror(ret));
    }

    spdlog::info("Opened WiredTiger database at: {}", db_path_);
    return true;
}

void WiredTigerStore::close() {
    if (default_session_) {
        default_session_->close(default_session_, nullptr);
        default_session_ = nullptr;
    }
    if (conn_) {
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
        spdlog::info("Closed WiredTiger database");
    }
}

bool WiredTigerStore::ensureOpen() {
    if (!conn_) {
        spdlog::error("Database not open");
        return false;
    }
    return true;
}

// ==================== Basic KV Operations ====================

bool WiredTigerStore::put(std::string_view key, std::string_view value) {
    if (!ensureOpen()) return false;

    WT_CURSOR* cursor = nullptr;
    int ret = default_session_->open_cursor(
        default_session_, "table:kv", nullptr, "overwrite", &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor: {}", wiredtiger_strerror(ret));
        return false;
    }

    WT_ITEM key_item = makeItem(key.data(), key.size());
    WT_ITEM val_item = makeItem(value.data(), value.size());

    cursor->set_key(cursor, &key_item);
    cursor->set_value(cursor, &val_item);

    ret = cursor->insert(cursor);
    cursor->close(cursor);

    if (ret != 0) {
        spdlog::error("Failed to put key: {}", wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

std::optional<std::string> WiredTigerStore::get(std::string_view key) {
    if (!ensureOpen()) return std::nullopt;

    WT_CURSOR* cursor = nullptr;
    int ret = default_session_->open_cursor(
        default_session_, "table:kv", nullptr, nullptr, &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor: {}", wiredtiger_strerror(ret));
        return std::nullopt;
    }

    WT_ITEM key_item = makeItem(key.data(), key.size());
    cursor->set_key(cursor, &key_item);

    ret = cursor->search(cursor);
    if (ret != 0) {
        cursor->close(cursor);
        if (ret == WT_NOTFOUND) {
            return std::nullopt;
        }
        spdlog::error("Failed to get key: {}", wiredtiger_strerror(ret));
        return std::nullopt;
    }

    WT_ITEM val_item;
    std::memset(&val_item, 0, sizeof(val_item));
    cursor->get_value(cursor, &val_item);

    std::string result(static_cast<const char*>(val_item.data), val_item.size);
    cursor->close(cursor);
    return result;
}

bool WiredTigerStore::del(std::string_view key) {
    if (!ensureOpen()) return false;

    WT_CURSOR* cursor = nullptr;
    int ret = default_session_->open_cursor(
        default_session_, "table:kv", nullptr, "overwrite", &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor: {}", wiredtiger_strerror(ret));
        return false;
    }

    WT_ITEM key_item = makeItem(key.data(), key.size());
    cursor->set_key(cursor, &key_item);

    ret = cursor->remove(cursor);
    cursor->close(cursor);

    if (ret != 0 && ret != WT_NOTFOUND) {
        spdlog::error("Failed to delete key: {}", wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

// ==================== Batch Operations ====================

bool WiredTigerStore::putBatch(const std::vector<std::pair<std::string, std::string>>& kv_pairs) {
    if (!ensureOpen()) return false;
    if (kv_pairs.empty()) return true;

    WT_SESSION* session = beginTransaction();
    if (!session) return false;

    WT_CURSOR* cursor = nullptr;
    int ret = session->open_cursor(session, "table:kv", nullptr, "overwrite", &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor: {}", wiredtiger_strerror(ret));
        rollbackTransaction(session);
        return false;
    }

    for (const auto& [k, v] : kv_pairs) {
        WT_ITEM key_item = makeItem(k.data(), k.size());
        WT_ITEM val_item = makeItem(v.data(), v.size());

        cursor->set_key(cursor, &key_item);
        cursor->set_value(cursor, &val_item);

        ret = cursor->insert(cursor);
        if (ret != 0) {
            spdlog::error("Failed to insert in batch: {}", wiredtiger_strerror(ret));
            cursor->close(cursor);
            rollbackTransaction(session);
            return false;
        }
    }

    cursor->close(cursor);
    return commitTransaction(session);
}

// ==================== Prefix Scan ====================

std::vector<WiredTigerStore::KeyValuePair> WiredTigerStore::prefixScan(std::string_view prefix) {
    std::vector<KeyValuePair> results;
    prefixScan(prefix, [&results](std::string_view key, std::string_view value) {
        results.push_back({std::string(key), std::string(value)});
        return true;
    });
    return results;
}

void WiredTigerStore::prefixScan(std::string_view prefix,
                                  const std::function<bool(std::string_view, std::string_view)>& callback) {
    if (!ensureOpen()) return;

    WT_CURSOR* cursor = nullptr;
    int ret = default_session_->open_cursor(
        default_session_, "table:kv", nullptr, "raw", &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor for scan: {}", wiredtiger_strerror(ret));
        return;
    }

    WT_ITEM key_item = makeItem(prefix.data(), prefix.size());
    cursor->set_key(cursor, &key_item);

    int exact = 0;
    ret = cursor->search_near(cursor, &exact);
    if (ret == WT_NOTFOUND) {
        cursor->close(cursor);
        return;
    }
    if (ret != 0) {
        spdlog::error("search_near failed: {}", wiredtiger_strerror(ret));
        cursor->close(cursor);
        return;
    }

    while (true) {
        WT_ITEM k, v;
        std::memset(&k, 0, sizeof(k));
        std::memset(&v, 0, sizeof(v));

        ret = cursor->get_key(cursor, &k);
        if (ret != 0) break;

        std::string_view current_key(static_cast<const char*>(k.data), k.size);

        if (current_key.size() < prefix.size() ||
            current_key.substr(0, prefix.size()) != prefix) {
            break;
        }

        ret = cursor->get_value(cursor, &v);
        if (ret != 0) break;

        std::string_view current_val(static_cast<const char*>(v.data), v.size);
        if (!callback(current_key, current_val)) {
            break;
        }

        ret = cursor->next(cursor);
        if (ret != 0) break;
    }

    cursor->close(cursor);
}

// ==================== Transaction Support ====================

WT_SESSION* WiredTigerStore::beginTransaction() {
    if (!ensureOpen()) return nullptr;

    WT_SESSION* session = nullptr;
    int ret = conn_->open_session(conn_, nullptr, "isolation=snapshot", &session);
    if (ret != 0) {
        spdlog::error("Failed to open transaction session: {}", wiredtiger_strerror(ret));
        return nullptr;
    }

    ret = session->begin_transaction(session, "isolation=snapshot");
    if (ret != 0) {
        spdlog::error("Failed to begin transaction: {}", wiredtiger_strerror(ret));
        session->close(session, nullptr);
        return nullptr;
    }

    return session;
}

bool WiredTigerStore::commitTransaction(WT_SESSION* session) {
    if (!session) return false;

    int ret = session->commit_transaction(session, nullptr);
    session->close(session, nullptr);

    if (ret != 0) {
        spdlog::error("Failed to commit transaction: {}", wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

bool WiredTigerStore::rollbackTransaction(WT_SESSION* session) {
    if (!session) return false;

    int ret = session->rollback_transaction(session, nullptr);
    session->close(session, nullptr);

    if (ret != 0) {
        spdlog::error("Failed to rollback transaction: {}", wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

// ==================== Transaction-scoped KV Operations ====================

bool WiredTigerStore::put(WT_SESSION* session, std::string_view key, std::string_view value) {
    if (!session) return false;

    WT_CURSOR* cursor = nullptr;
    int ret = session->open_cursor(session, "table:kv", nullptr, "overwrite", &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor: {}", wiredtiger_strerror(ret));
        return false;
    }

    WT_ITEM key_item = makeItem(key.data(), key.size());
    WT_ITEM val_item = makeItem(value.data(), value.size());

    cursor->set_key(cursor, &key_item);
    cursor->set_value(cursor, &val_item);

    ret = cursor->insert(cursor);
    cursor->close(cursor);

    if (ret != 0) {
        spdlog::error("Failed to put in transaction: {}", wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

std::optional<std::string> WiredTigerStore::get(WT_SESSION* session, std::string_view key) {
    if (!session) return std::nullopt;

    WT_CURSOR* cursor = nullptr;
    int ret = session->open_cursor(session, "table:kv", nullptr, nullptr, &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor: {}", wiredtiger_strerror(ret));
        return std::nullopt;
    }

    WT_ITEM key_item = makeItem(key.data(), key.size());
    cursor->set_key(cursor, &key_item);

    ret = cursor->search(cursor);
    if (ret != 0) {
        cursor->close(cursor);
        if (ret == WT_NOTFOUND) return std::nullopt;
        spdlog::error("Failed to get in transaction: {}", wiredtiger_strerror(ret));
        return std::nullopt;
    }

    WT_ITEM val_item;
    std::memset(&val_item, 0, sizeof(val_item));
    cursor->get_value(cursor, &val_item);

    std::string result(static_cast<const char*>(val_item.data), val_item.size);
    cursor->close(cursor);
    return result;
}

bool WiredTigerStore::del(WT_SESSION* session, std::string_view key) {
    if (!session) return false;

    WT_CURSOR* cursor = nullptr;
    int ret = session->open_cursor(session, "table:kv", nullptr, "overwrite", &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor: {}", wiredtiger_strerror(ret));
        return false;
    }

    WT_ITEM key_item = makeItem(key.data(), key.size());
    cursor->set_key(cursor, &key_item);

    ret = cursor->remove(cursor);
    cursor->close(cursor);

    if (ret != 0 && ret != WT_NOTFOUND) {
        spdlog::error("Failed to delete in transaction: {}", wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

} // namespace eugraph
