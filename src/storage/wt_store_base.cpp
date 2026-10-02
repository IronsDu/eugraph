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
    // Idempotent: the destructor calls this too, and `close()` may already have been called
    // explicitly. Without the guard the code below would open a session on a dead connection
    // (and close already-closed sessions a second time).
    if (!conn_)
        return;
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
    {
        // One short-lived session for the final checkpoint: it cannot outlive this call, and no
        // other thread uses it. `schemaMutex_` keeps it mutually exclusive with a periodic
        // checkpoint that may be in flight (lifecycle exclusivity, not session memory).
        std::lock_guard<std::recursive_mutex> lock(schemaMutex_);
        WtSession session(conn_.get());
        if (session) {
            auto t0 = std::chrono::steady_clock::now();
            int ret = session.get()->checkpoint(session.get(), nullptr);
            auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            if (ret != 0) {
                spdlog::warn("Checkpoint before close failed: error {} ({}ms)", ret, ms);
            } else {
                spdlog::info("Checkpoint completed ({}ms)", ms);
            }
        }
    }
    // Per-thread sessions must be closed before the connection.
    closeThreadSessions();
    conn_.close();
}

bool WtStoreBase::openConnection(const std::string& db_path, const std::string& extra_config) {
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
    std::string wt_config = "create,log=(enabled=true)";
    if (extra_config.empty()) {
        wt_config += ",transaction_sync=(enabled=true,method=fsync)";
    } else {
        wt_config += ",";
        wt_config += extra_config;
    }

    if (!conn_.open(db_path, wt_config.c_str())) {
        return false;
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    spdlog::info("Opened WT connection at {} ({}ms)", db_path, ms);

    return true;
}

// ==================== Session/Cursor Helpers ====================

WT_SESSION* WtStoreBase::threadSession() {
    // One session per thread. The thread-local entry deliberately holds no ownership and has
    // no destructor that closes the session: a thread can outlive the connection, and closing
    // a session after its connection is undefined behaviour. Sessions are released when the
    // connection is torn down (closeThreadSessions() closes them just before conn_.close()),
    // i.e. at store/process teardown -- never at thread exit.
    // One cached session PER (thread, store) pair -- not a single slot: a thread routinely
    // alternates between several stores (a graph's data store, its meta store, the process-wide
    // catalog store, ...), and a single-slot cache would then open a brand-new session on every
    // switch and push it into this store's pool. Across a long run that grows the pool without
    // bound until WT refuses to open more sessions (which used to surface as a null session and a
    // segfault in WtCursor). The map is tiny (a handful of stores per thread) and the hot path is
    // one hash lookup, i.e. nanoseconds against the ~0.6us of a lookup.
    struct TlsEntry {
        uint64_t epoch = 0;
        WT_SESSION* session = nullptr;
    };
    // Keyed by the store's process-unique id (NOT its address: addresses are recycled, and a new
    // store at a dead store's address must never inherit its -- closed -- session).
    thread_local std::unordered_map<uint64_t, TlsEntry> tls_sessions;

    const uint64_t epoch = sessionGeneration_.load(std::memory_order_relaxed);
    auto it = tls_sessions.find(sessionStoreId_);
    if (it != tls_sessions.end() && it->second.epoch == epoch && it->second.session != nullptr)
        return it->second.session; // hot path: no lock, no thread-id lookup

    // Slow path: first call on this thread for this store, or the store was closed and re-created.
    WtSession session = conn_.openSession();
    if (!session) {
        // Never hand out a null session: callers would dereference it (see the null guards in the
        // KV primitives). Logged because reaching it means the connection is closing or the
        // process is out of sessions -- both worth knowing about.
        spdlog::error("threadSession: failed to open a session for store {}", static_cast<const void*>(this));
        return nullptr;
    }
    WT_SESSION* raw = session.release();
    {
        std::lock_guard<std::mutex> lock(sessionPoolMutex_);
        sessionPool_.push_back(raw);
    }
    if (tls_sessions.size() > 256)
        tls_sessions.clear(); // bound per-thread growth over a long run (rare; forces re-open)
    tls_sessions[sessionStoreId_] = TlsEntry{epoch, raw};
    return raw;
}

void WtStoreBase::closeThreadSessions() {
    // Invalidate this store's thread-local entries first, then close its sessions. Scoped to this
    // store on purpose: a process-wide bump would evict other stores' cached sessions as well,
    // and those would be re-opened on the next visit while the old ones stayed pooled -- i.e. a
    // session leak that ends in WT's "out of sessions".
    sessionGeneration_.fetch_add(1, std::memory_order_relaxed);
    std::vector<WT_SESSION*> sessions;
    {
        std::lock_guard<std::mutex> lock(sessionPoolMutex_);
        sessions.swap(sessionPool_);
    }
    for (WT_SESSION* session : sessions) {
        if (session)
            session->close(session, nullptr);
    }
}

WT_SESSION* WtStoreBase::getSession(GraphTxnHandle txn) {
    if (txn == INVALID_GRAPH_TXN)
        return threadSession();
    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txns_.find(txn);
    return it != txns_.end() ? it->second->session.get() : nullptr;
}

WtCursor WtStoreBase::openCursor(WT_SESSION* session, const std::string& table_name) {
    return WtCursor(session, table_name);
}

WtCursor* WtStoreBase::getTxnCursor(GraphTxnHandle txn, WT_SESSION* session, const std::string& table_name) {
    if (txn == INVALID_GRAPH_TXN)
        return nullptr;

    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txns_.find(txn);
    if (it == txns_.end())
        return nullptr;

    auto& cursors = it->second->cursors;
    auto cur = cursors.find(table_name);
    if (cur != cursors.end()) {
        return cur->second ? &cur->second : nullptr;
    }

    auto [inserted, _] = cursors.emplace(std::piecewise_construct, std::forward_as_tuple(table_name),
                                         std::forward_as_tuple(session, table_name));
    if (!inserted->second) {
        cursors.erase(inserted);
        return nullptr;
    }
    return &inserted->second;
}

bool WtStoreBase::tablePutTxn(GraphTxnHandle txn, WT_SESSION* session, const std::string& table, std::string_view key,
                              std::string_view value) {
    WtCursor* cursor = getTxnCursor(txn, session, table);
    if (!cursor)
        return tablePut(session, table, key, value);

    setItem(cursor->get(), key);
    setValueItem(cursor->get(), value);
    int ret = cursor->get()->insert(cursor->get());

    if (ret != 0) {
        spdlog::error("tablePutTxn failed on {}: error {}", table, ret);
        return false;
    }
    return true;
}

void WtStoreBase::closeTxnCursors(TxnState* state) {
    if (!state)
        return;
    state->cursors.clear();
}

bool WtStoreBase::ensureGlobalTable(const char* table_name) {
    if (!conn_)
        return false;
    // Business-level exclusivity: without it, two interleaved create/drop sequences could each
    // observe "table exists" and still end up with the table missing. The lock guards the
    // SEQUENCE, not a session.
    std::lock_guard<std::recursive_mutex> lock(schemaMutex_);
    WtSession session(conn_.get());
    if (!session) {
        spdlog::error("Failed to open a session for creating table {}", table_name);
        return false;
    }
    int ret = session.get()->create(session.get(), table_name, WT_TABLE_CONFIG);
    if (ret != 0 && ret != EBUSY) {
        spdlog::error("Failed to create table {}: error {}", table_name, ret);
        return false;
    }
    return true;
}

bool WtStoreBase::checkpoint() {
    if (!conn_)
        return false;
    std::lock_guard<std::recursive_mutex> lock(schemaMutex_);
    WtSession session(conn_.get());
    if (!session)
        return false;
    int ret = session.get()->checkpoint(session.get(), nullptr);
    if (ret != 0) {
        spdlog::error("Checkpoint failed: error {}", ret);
        return false;
    }
    return true;
}

// ==================== Table KV Operations ====================

bool WtStoreBase::tablePut(WT_SESSION* session, const std::string& table, std::string_view key,
                           std::string_view value) {
    if (session == nullptr) {
        spdlog::error("tablePut: null session");
        return false;
    }
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
    if (session == nullptr) {
        spdlog::error("tableGet: null session");
        return std::nullopt;
    }
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
    if (session == nullptr) {
        spdlog::error("tableDel: null session");
        return false;
    }
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
    if (session == nullptr) {
        spdlog::error("tableScan: null session");
        return;
    }
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
