#include "storage/meta/sync_graph_meta_store.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== Lifecycle ====================

SyncGraphMetaStore::~SyncGraphMetaStore() {
    close();
}

bool SyncGraphMetaStore::open(const std::string& db_path) {
    if (!openConnection(db_path))
        return false;

    if (!ensureGlobalTable(defaultSession_, TABLE_METADATA)) {
        close();
        return false;
    }

    spdlog::info("Opened meta store at: {}", db_path);
    return true;
}

void SyncGraphMetaStore::close() {
    closeConnection();
    spdlog::info("Closed meta store");
}

bool SyncGraphMetaStore::isOpen() const {
    return WtStoreBase::isOpen();
}

// ==================== Transaction ====================

GraphTxnHandle SyncGraphMetaStore::beginTransaction() {
    if (!conn_)
        return INVALID_GRAPH_TXN;

    auto ts = std::make_unique<TxnState>();
    int ret = conn_->open_session(conn_, nullptr, nullptr, &ts->session);
    if (ret != 0) {
        spdlog::error("Failed to open transaction session: error {}", ret);
        return INVALID_GRAPH_TXN;
    }

    ret = ts->session->begin_transaction(ts->session, "isolation=snapshot");
    if (ret != 0) {
        spdlog::error("Failed to begin transaction: error {}", ret);
        ts->session->close(ts->session, nullptr);
        return INVALID_GRAPH_TXN;
    }

    auto handle = static_cast<GraphTxnHandle>(ts.get());
    std::lock_guard<std::mutex> lock(txnMutex_);
    txns_.emplace(handle, std::move(ts));
    return handle;
}

bool SyncGraphMetaStore::commitTransaction(GraphTxnHandle txn) {
    TxnState* ts = nullptr;
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        auto it = txns_.find(txn);
        if (it == txns_.end())
            return false;
        ts = it->second.get();
    }

    int ret = ts->session->commit_transaction(ts->session, nullptr);
    closeTxnCursors(ts);
    ts->session->close(ts->session, nullptr);

    std::lock_guard<std::mutex> lock(txnMutex_);
    txns_.erase(txn);

    if (ret != 0) {
        spdlog::error("Meta store commit failed: error {}", ret);
        return false;
    }
    return true;
}

bool SyncGraphMetaStore::rollbackTransaction(GraphTxnHandle txn) {
    TxnState* ts = nullptr;
    {
        std::lock_guard<std::mutex> lock(txnMutex_);
        auto it = txns_.find(txn);
        if (it == txns_.end())
            return false;
        ts = it->second.get();
    }

    int ret = ts->session->rollback_transaction(ts->session, nullptr);
    closeTxnCursors(ts);
    ts->session->close(ts->session, nullptr);

    std::lock_guard<std::mutex> lock(txnMutex_);
    txns_.erase(txn);

    if (ret != 0) {
        spdlog::error("Meta store rollback failed: error {}", ret);
        return false;
    }
    return true;
}

// ==================== Metadata Raw KV ====================

bool SyncGraphMetaStore::metadataPut(std::string_view key, std::string_view value) {
    return tablePut(defaultSession_, TABLE_METADATA, key, value);
}

std::optional<std::string> SyncGraphMetaStore::metadataGet(std::string_view key) {
    return tableGet(defaultSession_, TABLE_METADATA, key);
}

void SyncGraphMetaStore::metadataScan(std::string_view prefix,
                                      const std::function<bool(std::string_view, std::string_view)>& callback) {
    tableScan(defaultSession_, TABLE_METADATA, prefix, callback);
}

// ==================== DDL ====================

bool SyncGraphMetaStore::createLabel(LabelId /*label_id*/) {
    // Metadata-level DDL: no additional tables needed in meta store.
    // Metadata persistence is handled by metadataPut.
    return true;
}

bool SyncGraphMetaStore::dropLabel(LabelId /*label_id*/) {
    // Metadata-level DDL: no additional cleanup in meta store.
    return true;
}

bool SyncGraphMetaStore::createEdgeLabel(EdgeLabelId /*edge_label_id*/) {
    return true;
}

bool SyncGraphMetaStore::dropEdgeLabel(EdgeLabelId /*edge_label_id*/) {
    return true;
}

} // namespace eugraph
