#include "storage/graph_store_impl.hpp"

#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== WiredTiger Helpers ====================

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

GraphStoreImpl::~GraphStoreImpl() {
    close();
}

bool GraphStoreImpl::open(const std::string& db_path) {
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

    ret = conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
    if (ret != 0) {
        spdlog::error("Failed to open default session: error {}", ret);
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
        return false;
    }

    // Create global tables
    if (!ensureGlobalTable(defaultSession_, TABLE_LABEL_REVERSE) ||
        !ensureGlobalTable(defaultSession_, TABLE_PK_FORWARD) ||
        !ensureGlobalTable(defaultSession_, TABLE_PK_REVERSE) ||
        !ensureGlobalTable(defaultSession_, TABLE_EDGE_INDEX) || !ensureGlobalTable(defaultSession_, TABLE_METADATA)) {
        close();
        return false;
    }

    spdlog::info("Opened WiredTiger graph store at: {}", db_path);
    return true;
}

void GraphStoreImpl::close() {
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
        spdlog::info("Closed WiredTiger graph store");
    }
}

bool GraphStoreImpl::isOpen() const {
    return conn_ != nullptr;
}

bool GraphStoreImpl::ensureGlobalTable(WT_SESSION* session, const char* table_name) {
    int ret = session->create(session, table_name, WT_TABLE_CONFIG);
    if (ret != 0) {
        spdlog::error("Failed to create table {}: error {}", table_name, ret);
        return false;
    }
    return true;
}

// ==================== DDL ====================

bool GraphStoreImpl::createLabel(LabelId label_id) {
    auto fwd = labelFwdTable(label_id);
    auto vprop = vpropTable(label_id);

    // Use a dedicated session for DDL to avoid holding table references on the default session
    WT_SESSION* ddl_session = nullptr;
    int ret = conn_->open_session(conn_, nullptr, nullptr, &ddl_session);
    if (ret != 0) {
        spdlog::error("Failed to open DDL session: error {}", ret);
        return false;
    }

    ret = ddl_session->create(ddl_session, fwd.c_str(), WT_TABLE_CONFIG);
    if (ret != 0) {
        spdlog::error("Failed to create label fwd table {}: error {}", fwd, ret);
        ddl_session->close(ddl_session, nullptr);
        return false;
    }

    ret = ddl_session->create(ddl_session, vprop.c_str(), WT_TABLE_CONFIG);
    if (ret != 0) {
        spdlog::error("Failed to create vprop table {}: error {}", vprop, ret);
        ddl_session->close(ddl_session, nullptr);
        return false;
    }

    ddl_session->close(ddl_session, nullptr);
    return true;
}

bool GraphStoreImpl::dropLabel(LabelId label_id) {
    auto fwd = labelFwdTable(label_id);
    auto vprop = vpropTable(label_id);

    // Close default session to release cached table handles, preventing EBUSY on drop
    if (defaultSession_) {
        defaultSession_->close(defaultSession_, nullptr);
        defaultSession_ = nullptr;
    }

    // Use a dedicated session for DDL drop
    WT_SESSION* ddl_session = nullptr;
    int ret = conn_->open_session(conn_, nullptr, nullptr, &ddl_session);
    if (ret != 0) {
        spdlog::error("Failed to open DDL session: error {}", ret);
        conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
        return false;
    }

    // Checkpoint to flush and release cached handles
    ddl_session->checkpoint(ddl_session, nullptr);

    ret = ddl_session->drop(ddl_session, fwd.c_str(), nullptr);
    if (ret != 0) {
        spdlog::error("Failed to drop label fwd table {}: error {}", fwd, ret);
        ddl_session->close(ddl_session, nullptr);
        conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
        return false;
    }

    ret = ddl_session->drop(ddl_session, vprop.c_str(), nullptr);
    if (ret != 0) {
        spdlog::error("Failed to drop vprop table {}: error {}", vprop, ret);
        ddl_session->close(ddl_session, nullptr);
        conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
        return false;
    }

    ddl_session->close(ddl_session, nullptr);

    // Reopen default session
    ret = conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
    if (ret != 0) {
        spdlog::error("Failed to reopen default session: error {}", ret);
        return false;
    }

    deletedLabels_.insert(label_id);
    return true;
}

bool GraphStoreImpl::createEdgeLabel(EdgeLabelId edge_label_id) {
    auto etype = etypeTable(edge_label_id);
    auto eprop = epropTable(edge_label_id);

    // Use a dedicated session for DDL to avoid holding table references on the default session
    WT_SESSION* ddl_session = nullptr;
    int ret = conn_->open_session(conn_, nullptr, nullptr, &ddl_session);
    if (ret != 0) {
        spdlog::error("Failed to open DDL session: error {}", ret);
        return false;
    }

    ret = ddl_session->create(ddl_session, etype.c_str(), WT_TABLE_CONFIG);
    if (ret != 0) {
        spdlog::error("Failed to create etype table {}: error {}", etype, ret);
        ddl_session->close(ddl_session, nullptr);
        return false;
    }

    ret = ddl_session->create(ddl_session, eprop.c_str(), WT_TABLE_CONFIG);
    if (ret != 0) {
        spdlog::error("Failed to create eprop table {}: error {}", eprop, ret);
        ddl_session->close(ddl_session, nullptr);
        return false;
    }

    ddl_session->close(ddl_session, nullptr);
    return true;
}

bool GraphStoreImpl::dropEdgeLabel(EdgeLabelId edge_label_id) {
    auto etype = etypeTable(edge_label_id);
    auto eprop = epropTable(edge_label_id);

    // Close default session to release cached table handles, preventing EBUSY on drop
    if (defaultSession_) {
        defaultSession_->close(defaultSession_, nullptr);
        defaultSession_ = nullptr;
    }

    // Use a dedicated session for DDL drop
    WT_SESSION* ddl_session = nullptr;
    int ret = conn_->open_session(conn_, nullptr, nullptr, &ddl_session);
    if (ret != 0) {
        spdlog::error("Failed to open DDL session: error {}", ret);
        conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
        return false;
    }

    // Checkpoint to flush and release cached handles
    ddl_session->checkpoint(ddl_session, nullptr);

    ret = ddl_session->drop(ddl_session, etype.c_str(), nullptr);
    if (ret != 0) {
        spdlog::error("Failed to drop etype table {}: error {}", etype, ret);
        ddl_session->close(ddl_session, nullptr);
        conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
        return false;
    }

    ret = ddl_session->drop(ddl_session, eprop.c_str(), nullptr);
    if (ret != 0) {
        spdlog::error("Failed to drop eprop table {}: error {}", eprop, ret);
        ddl_session->close(ddl_session, nullptr);
        conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
        return false;
    }

    ddl_session->close(ddl_session, nullptr);

    // Reopen default session
    ret = conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
    if (ret != 0) {
        spdlog::error("Failed to reopen default session: error {}", ret);
        return false;
    }

    deletedEdgeLabels_.insert(edge_label_id);
    return true;
}

// ==================== Tombstone Filtering ====================

void GraphStoreImpl::setDeletedLabelIds(std::set<LabelId> ids) {
    deletedLabels_ = std::move(ids);
}

void GraphStoreImpl::setDeletedEdgeLabelIds(std::set<EdgeLabelId> ids) {
    deletedEdgeLabels_ = std::move(ids);
}

// ==================== Transaction ====================

GraphTxnHandle GraphStoreImpl::beginTransaction() {
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

bool GraphStoreImpl::commitTransaction(GraphTxnHandle txn) {
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
        spdlog::error("WiredTiger commit failed: error {}", ret);
        return false;
    }
    return true;
}

bool GraphStoreImpl::rollbackTransaction(GraphTxnHandle txn) {
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
        spdlog::error("WiredTiger rollback failed: error {}", ret);
        return false;
    }
    return true;
}

// ==================== Session/Cursor Helpers ====================

WT_SESSION* GraphStoreImpl::getSession(GraphTxnHandle txn) {
    if (txn == INVALID_GRAPH_TXN)
        return defaultSession_;
    std::lock_guard<std::mutex> lock(txnMutex_);
    auto it = txns_.find(txn);
    return it != txns_.end() ? it->second->session : nullptr;
}

WT_CURSOR* GraphStoreImpl::getCursor(WT_SESSION* session, const std::string& table_name) {
    // For transaction sessions, use per-txn cursor cache
    // For default session, open a new cursor each time (to avoid state conflicts)
    WT_CURSOR* cursor = nullptr;
    int ret = session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &cursor);
    if (ret != 0) {
        spdlog::error("Failed to open cursor on {}: error {}", table_name, ret);
        return nullptr;
    }
    return cursor;
}

void GraphStoreImpl::closeTxnCursors(TxnState* state) {
    if (!state)
        return;
    for (auto& [name, cursor] : state->cursors) {
        if (cursor)
            cursor->close(cursor);
    }
    state->cursors.clear();
}

bool GraphStoreImpl::tablePut(WT_SESSION* session, const std::string& table, std::string_view key,
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

std::optional<std::string> GraphStoreImpl::tableGet(WT_SESSION* session, const std::string& table,
                                                    std::string_view key) {
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

bool GraphStoreImpl::tableDel(WT_SESSION* session, const std::string& table, std::string_view key) {
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

void GraphStoreImpl::tableScan(WT_SESSION* session, const std::string& table, std::string_view prefix,
                               const std::function<bool(std::string_view, std::string_view)>& callback) {
    WT_CURSOR* cursor = getCursor(session, table);
    if (!cursor)
        return;

    std::string pfx(prefix);

    if (pfx.empty()) {
        // Full table scan
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
        // Prefix scan using search_near
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

// ==================== Vertex ====================

bool GraphStoreImpl::insertVertex(GraphTxnHandle txn, VertexId vid,
                                  std::span<const std::pair<LabelId, Properties>> label_props,
                                  const PropertyValue* pk_value) {
    auto session = getSession(txn);
    if (!session)
        return false;

    for (const auto& [label_id, props] : label_props) {
        // label_reverse: vertex -> labels
        auto lr_key = KeyCodec::encodeLabelReverseKey(vid, label_id);
        if (!tablePut(session, TABLE_LABEL_REVERSE, lr_key, {}))
            return false;

        // label_fwd_{id}: label -> vertices
        auto lf_key = KeyCodec::encodeLabelForwardKey(vid);
        if (!tablePut(session, labelFwdTable(label_id), lf_key, {}))
            return false;

        // vprop_{id}: vertex properties
        for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
            if (props[prop_id].has_value()) {
                auto vp_key = KeyCodec::encodeVPropKey(vid, prop_id);
                auto vp_val = ValueCodec::encode(props[prop_id].value());
                if (!tablePut(session, vpropTable(label_id), vp_key, vp_val))
                    return false;
            }
        }
    }

    // Primary key indexes
    if (pk_value) {
        auto pk_encoded = ValueCodec::encode(*pk_value);
        auto pf_key = KeyCodec::encodePkForwardKey(pk_encoded);
        if (!tablePut(session, TABLE_PK_FORWARD, pf_key, ValueCodec::encodeU64(vid)))
            return false;

        auto pr_key = KeyCodec::encodePkReverseKey(vid);
        if (!tablePut(session, TABLE_PK_REVERSE, pr_key, pk_encoded))
            return false;
    }

    return true;
}

bool GraphStoreImpl::deleteVertex(GraphTxnHandle txn, VertexId vid) {
    auto session = getSession(txn);
    if (!session)
        return false;

    // 1. Get all labels and delete label-related data
    auto labels = getVertexLabels(txn, vid);
    for (LabelId label_id : labels) {
        // Delete from label_reverse
        tableDel(session, TABLE_LABEL_REVERSE, KeyCodec::encodeLabelReverseKey(vid, label_id));

        // Delete from label_fwd_{id}
        tableDel(session, labelFwdTable(label_id), KeyCodec::encodeLabelForwardKey(vid));

        // Delete all properties from vprop_{id}
        auto prefix = KeyCodec::encodeVPropPrefix(vid);
        tableScan(session, vpropTable(label_id), prefix, [&](std::string_view key, std::string_view) {
            tableDel(session, vpropTable(label_id), key);
            return true;
        });
    }

    // 2. Delete primary key indexes
    auto pk_encoded = tableGet(session, TABLE_PK_REVERSE, KeyCodec::encodePkReverseKey(vid));
    if (pk_encoded) {
        tableDel(session, TABLE_PK_FORWARD, KeyCodec::encodePkForwardKey(*pk_encoded));
        tableDel(session, TABLE_PK_REVERSE, KeyCodec::encodePkReverseKey(vid));
    }

    return true;
}

// ==================== Vertex Properties ====================

std::optional<Properties> GraphStoreImpl::getVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id) {
    auto session = getSession(txn);
    if (!session)
        return std::nullopt;

    auto prefix = KeyCodec::encodeVPropPrefix(vid);
    Properties props;
    bool found_any = false;

    tableScan(session, vpropTable(label_id), prefix, [&](std::string_view key, std::string_view value) {
        auto [_, prop_id] = KeyCodec::decodeVPropKey(key);
        if (prop_id >= props.size())
            props.resize(prop_id + 1);
        props[prop_id] = ValueCodec::decode(value);
        found_any = true;
        return true;
    });

    if (!found_any)
        return std::nullopt;
    return props;
}

std::optional<PropertyValue> GraphStoreImpl::getVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id,
                                                               uint16_t prop_id) {
    auto session = getSession(txn);
    if (!session)
        return std::nullopt;

    auto key = KeyCodec::encodeVPropKey(vid, prop_id);
    auto val = tableGet(session, vpropTable(label_id), key);
    if (!val)
        return std::nullopt;
    return ValueCodec::decode(*val);
}

bool GraphStoreImpl::putVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id,
                                       const PropertyValue& value) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto key = KeyCodec::encodeVPropKey(vid, prop_id);
    return tablePut(session, vpropTable(label_id), key, ValueCodec::encode(value));
}

bool GraphStoreImpl::putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id, const Properties& props) {
    auto session = getSession(txn);
    if (!session)
        return false;

    for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
        if (props[prop_id].has_value()) {
            auto key = KeyCodec::encodeVPropKey(vid, prop_id);
            if (!tablePut(session, vpropTable(label_id), key, ValueCodec::encode(props[prop_id].value())))
                return false;
        }
    }
    return true;
}

bool GraphStoreImpl::deleteVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto key = KeyCodec::encodeVPropKey(vid, prop_id);
    return tableDel(session, vpropTable(label_id), key);
}

// ==================== Vertex Labels ====================

LabelIdSet GraphStoreImpl::getVertexLabels(GraphTxnHandle txn, VertexId vid) {
    LabelIdSet labels;
    auto session = getSession(txn);
    if (!session)
        return labels;

    auto prefix = KeyCodec::encodeLabelReversePrefix(vid);
    tableScan(session, TABLE_LABEL_REVERSE, prefix, [&](std::string_view key, std::string_view) {
        auto [_, label_id] = KeyCodec::decodeLabelReverseKey(key);
        // Filter out deleted labels
        if (deletedLabels_.find(label_id) == deletedLabels_.end()) {
            labels.insert(label_id);
        }
        return true;
    });
    return labels;
}

bool GraphStoreImpl::addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto lr_key = KeyCodec::encodeLabelReverseKey(vid, label_id);
    if (!tablePut(session, TABLE_LABEL_REVERSE, lr_key, {}))
        return false;

    auto lf_key = KeyCodec::encodeLabelForwardKey(vid);
    return tablePut(session, labelFwdTable(label_id), lf_key, {});
}

bool GraphStoreImpl::removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) {
    auto session = getSession(txn);
    if (!session)
        return false;

    tableDel(session, TABLE_LABEL_REVERSE, KeyCodec::encodeLabelReverseKey(vid, label_id));
    tableDel(session, labelFwdTable(label_id), KeyCodec::encodeLabelForwardKey(vid));

    // Delete all properties from vprop_{id}
    auto prefix = KeyCodec::encodeVPropPrefix(vid);
    tableScan(session, vpropTable(label_id), prefix, [&](std::string_view key, std::string_view) {
        tableDel(session, vpropTable(label_id), key);
        return true;
    });

    return true;
}

// ==================== Primary Key ====================

std::optional<VertexId> GraphStoreImpl::getVertexIdByPk(const PropertyValue& pk_value) {
    auto pk_encoded = ValueCodec::encode(pk_value);
    auto key = KeyCodec::encodePkForwardKey(pk_encoded);
    auto val = tableGet(defaultSession_, TABLE_PK_FORWARD, key);
    if (!val)
        return std::nullopt;
    return ValueCodec::decodeU64(*val);
}

std::optional<PropertyValue> GraphStoreImpl::getPkByVertexId(VertexId vid) {
    auto key = KeyCodec::encodePkReverseKey(vid);
    auto val = tableGet(defaultSession_, TABLE_PK_REVERSE, key);
    if (!val)
        return std::nullopt;
    return ValueCodec::decode(*val);
}

// ==================== Label Index Scan ====================

void GraphStoreImpl::scanVerticesByLabel(GraphTxnHandle txn, LabelId label_id,
                                         const std::function<bool(VertexId)>& callback) {
    auto session = getSession(txn);
    if (!session)
        return;

    // Full table scan of label_fwd_{id} (all keys are vertex_ids)
    tableScan(session, labelFwdTable(label_id), {}, [&](std::string_view key, std::string_view) {
        VertexId vid = KeyCodec::decodeLabelForwardKey(key);
        return callback(vid);
    });
}

// ==================== Edge ====================

bool GraphStoreImpl::insertEdge(GraphTxnHandle txn, EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                                uint64_t seq, const Properties& props) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto edge_id_val = ValueCodec::encodeU64(eid);

    // edge_index: OUT entry
    KeyCodec::EdgeIndexKey out_key{src_id, Direction::OUT, label_id, dst_id, seq};
    if (!tablePut(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(out_key), edge_id_val))
        return false;

    // edge_index: IN entry
    KeyCodec::EdgeIndexKey in_key{dst_id, Direction::IN, label_id, src_id, seq};
    if (!tablePut(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(in_key), edge_id_val))
        return false;

    // etype_{id}: edge type index
    KeyCodec::EdgeTypeIndexKey type_key{src_id, dst_id, seq};
    if (!tablePut(session, etypeTable(label_id), KeyCodec::encodeEdgeTypeIndexKey(type_key), edge_id_val))
        return false;

    // eprop_{id}: edge properties
    for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
        if (props[prop_id].has_value()) {
            auto ep_key = KeyCodec::encodeEPropKey(eid, prop_id);
            auto ep_val = ValueCodec::encode(props[prop_id].value());
            if (!tablePut(session, epropTable(label_id), ep_key, ep_val))
                return false;
        }
    }

    return true;
}

bool GraphStoreImpl::deleteEdge(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                                uint64_t seq) {
    auto session = getSession(txn);
    if (!session)
        return false;

    // Delete edge_index entries
    KeyCodec::EdgeIndexKey out_key{src_id, Direction::OUT, label_id, dst_id, seq};
    tableDel(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(out_key));

    KeyCodec::EdgeIndexKey in_key{dst_id, Direction::IN, label_id, src_id, seq};
    tableDel(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(in_key));

    // Delete etype_{id}
    KeyCodec::EdgeTypeIndexKey type_key{src_id, dst_id, seq};
    tableDel(session, etypeTable(label_id), KeyCodec::encodeEdgeTypeIndexKey(type_key));

    // Delete eprop_{id}
    auto prefix = KeyCodec::encodeEPropPrefix(eid);
    tableScan(session, epropTable(label_id), prefix, [&](std::string_view key, std::string_view) {
        tableDel(session, epropTable(label_id), key);
        return true;
    });

    return true;
}

// ==================== Edge Properties ====================

std::optional<Properties> GraphStoreImpl::getEdgeProperties(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid) {
    auto session = getSession(txn);
    if (!session)
        return std::nullopt;

    auto prefix = KeyCodec::encodeEPropPrefix(eid);
    Properties props;
    bool found_any = false;

    tableScan(session, epropTable(label_id), prefix, [&](std::string_view key, std::string_view value) {
        auto [_, prop_id] = KeyCodec::decodeEPropKey(key);
        if (prop_id >= props.size())
            props.resize(prop_id + 1);
        props[prop_id] = ValueCodec::decode(value);
        found_any = true;
        return true;
    });

    if (!found_any)
        return std::nullopt;
    return props;
}

std::optional<PropertyValue> GraphStoreImpl::getEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid,
                                                             uint16_t prop_id) {
    auto session = getSession(txn);
    if (!session)
        return std::nullopt;

    auto key = KeyCodec::encodeEPropKey(eid, prop_id);
    auto val = tableGet(session, epropTable(label_id), key);
    if (!val)
        return std::nullopt;
    return ValueCodec::decode(*val);
}

bool GraphStoreImpl::putEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                                     const PropertyValue& value) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto key = KeyCodec::encodeEPropKey(eid, prop_id);
    return tablePut(session, epropTable(label_id), key, ValueCodec::encode(value));
}

bool GraphStoreImpl::deleteEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto key = KeyCodec::encodeEPropKey(eid, prop_id);
    return tableDel(session, epropTable(label_id), key);
}

// ==================== Edge Traversal ====================

void GraphStoreImpl::scanEdges(GraphTxnHandle txn, VertexId vid, Direction direction,
                               std::optional<EdgeLabelId> label_filter,
                               const std::function<bool(const EdgeIndexEntry&)>& callback) {
    auto session = getSession(txn);
    if (!session)
        return;

    std::string prefix;
    if (label_filter) {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction, *label_filter);
    } else {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction);
    }

    tableScan(session, TABLE_EDGE_INDEX, prefix, [&](std::string_view key, std::string_view value) {
        auto decoded = KeyCodec::decodeEdgeIndexKey(key);
        // Filter deleted edge labels
        if (deletedEdgeLabels_.count(decoded.edge_label_id))
            return true;
        EdgeIndexEntry entry{decoded.neighbor_id, decoded.edge_label_id, decoded.seq, ValueCodec::decodeU64(value)};
        return callback(entry);
    });
}

void GraphStoreImpl::scanEdgesByType(GraphTxnHandle txn, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                                     std::optional<VertexId> dst_filter,
                                     const std::function<bool(const EdgeTypeIndexEntry&)>& callback) {
    auto session = getSession(txn);
    if (!session)
        return;

    std::string prefix;
    if (src_filter && dst_filter) {
        prefix = KeyCodec::encodeEdgeTypeIndexPrefix(*src_filter, *dst_filter);
    } else if (src_filter) {
        prefix = KeyCodec::encodeEdgeTypeIndexPrefix(*src_filter);
    }
    // Empty prefix means full table scan

    tableScan(session, etypeTable(label_id), prefix, [&](std::string_view key, std::string_view value) {
        auto decoded = KeyCodec::decodeEdgeTypeIndexKey(key);
        EdgeTypeIndexEntry entry{decoded.src_vertex_id, decoded.dst_vertex_id, decoded.seq,
                                 ValueCodec::decodeU64(value)};
        return callback(entry);
    });
}

// ==================== Statistics ====================

uint64_t GraphStoreImpl::countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) {
    uint64_t count = 0;
    scanVerticesByLabel(txn, label_id, [&](VertexId) {
        ++count;
        return true;
    });
    return count;
}

uint64_t GraphStoreImpl::countDegree(GraphTxnHandle txn, VertexId vid, Direction direction,
                                     std::optional<EdgeLabelId> label_filter) {
    uint64_t count = 0;
    scanEdges(txn, vid, direction, label_filter, [&](const EdgeIndexEntry&) {
        ++count;
        return true;
    });
    return count;
}

// ==================== Scan Cursor Factories ====================

std::unique_ptr<IGraphStore::IVertexScanCursor> GraphStoreImpl::createVertexScanCursor(GraphTxnHandle txn,
                                                                                       LabelId label_id) {
    auto session = getSession(txn);
    if (!session)
        return nullptr;

    return std::make_unique<VertexScanCursorImpl>(session, labelFwdTable(label_id));
}

std::unique_ptr<IGraphStore::IEdgeScanCursor>
GraphStoreImpl::createEdgeScanCursor(GraphTxnHandle txn, VertexId vid, Direction direction,
                                     std::optional<EdgeLabelId> label_filter) {
    auto session = getSession(txn);
    if (!session)
        return nullptr;

    std::string prefix;
    if (label_filter) {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction, *label_filter);
    } else {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction);
    }

    return std::make_unique<EdgeScanCursorImpl>(session, prefix);
}

std::unique_ptr<IGraphStore::IEdgeTypeScanCursor>
GraphStoreImpl::createEdgeTypeScanCursor(GraphTxnHandle txn, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                                         std::optional<VertexId> dst_filter) {
    auto session = getSession(txn);
    if (!session)
        return nullptr;

    std::string prefix;
    if (src_filter && dst_filter) {
        prefix = KeyCodec::encodeEdgeTypeIndexPrefix(*src_filter, *dst_filter);
    } else if (src_filter) {
        prefix = KeyCodec::encodeEdgeTypeIndexPrefix(*src_filter);
    }

    return std::make_unique<EdgeTypeScanCursorImpl>(session, etypeTable(label_id), prefix);
}

// ==================== VertexScanCursorImpl ====================

VertexScanCursorImpl::VertexScanCursorImpl(WT_SESSION* session, const std::string& table_name) : session_(session) {
    int ret = session_->open_cursor(session_, table_name.c_str(), nullptr, nullptr, &cursor_);
    if (ret != 0) {
        spdlog::error("Failed to open vertex scan cursor on {}: error {}", table_name, ret);
        return;
    }

    // Position at first entry
    ret = cursor_->next(cursor_);
    if (ret == 0) {
        readCurrent();
    }
}

VertexScanCursorImpl::~VertexScanCursorImpl() {
    if (cursor_)
        cursor_->close(cursor_);
}

void VertexScanCursorImpl::readCurrent() {
    currentKey_ = getKeyFromCursor(cursor_);
    current_vid_ = KeyCodec::decodeLabelForwardKey(currentKey_);
    valid_ = true;
}

bool VertexScanCursorImpl::valid() const {
    return valid_;
}

VertexId VertexScanCursorImpl::vertexId() const {
    return current_vid_;
}

void VertexScanCursorImpl::next() {
    int ret = cursor_->next(cursor_);
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

// ==================== EdgeScanCursorImpl ====================

EdgeScanCursorImpl::EdgeScanCursorImpl(WT_SESSION* session, std::string_view prefix)
    : session_(session), prefix_(prefix) {
    int ret = session_->open_cursor(session_, TABLE_EDGE_INDEX, nullptr, nullptr, &cursor_);
    if (ret != 0) {
        spdlog::error("Failed to open edge scan cursor: error {}", ret);
        return;
    }

    // Position using search_near
    setItem(cursor_, prefix_);
    int exact = 0;
    ret = cursor_->search_near(cursor_, &exact);

    if (ret != 0 || exact < 0) {
        ret = cursor_->next(cursor_);
    }

    if (ret == 0) {
        readCurrent();
    }
}

EdgeScanCursorImpl::~EdgeScanCursorImpl() {
    if (cursor_)
        cursor_->close(cursor_);
}

void EdgeScanCursorImpl::readCurrent() {
    currentKey_ = getKeyFromCursor(cursor_);
    if (!currentKey_.starts_with(prefix_)) {
        valid_ = false;
        return;
    }

    currentValue_ = getValueFromCursor(cursor_);
    auto decoded = KeyCodec::decodeEdgeIndexKey(currentKey_);
    current_ = {decoded.neighbor_id, decoded.edge_label_id, decoded.seq, ValueCodec::decodeU64(currentValue_)};
    valid_ = true;
}

bool EdgeScanCursorImpl::valid() const {
    return valid_;
}

const IGraphStore::EdgeIndexEntry& EdgeScanCursorImpl::entry() const {
    return current_;
}

void EdgeScanCursorImpl::next() {
    int ret = cursor_->next(cursor_);
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

// ==================== EdgeTypeScanCursorImpl ====================

EdgeTypeScanCursorImpl::EdgeTypeScanCursorImpl(WT_SESSION* session, const std::string& table_name,
                                               std::string_view prefix)
    : session_(session), prefix_(prefix) {
    int ret = session_->open_cursor(session_, table_name.c_str(), nullptr, nullptr, &cursor_);
    if (ret != 0) {
        spdlog::error("Failed to open edge type scan cursor on {}: error {}", table_name, ret);
        return;
    }

    if (prefix_.empty()) {
        // Full table scan
        ret = cursor_->next(cursor_);
    } else {
        setItem(cursor_, prefix_);
        int exact = 0;
        ret = cursor_->search_near(cursor_, &exact);
        if (ret != 0 || exact < 0) {
            ret = cursor_->next(cursor_);
        }
    }

    if (ret == 0) {
        readCurrent();
    }
}

EdgeTypeScanCursorImpl::~EdgeTypeScanCursorImpl() {
    if (cursor_)
        cursor_->close(cursor_);
}

void EdgeTypeScanCursorImpl::readCurrent() {
    currentKey_ = getKeyFromCursor(cursor_);
    if (!prefix_.empty() && !currentKey_.starts_with(prefix_)) {
        valid_ = false;
        return;
    }

    currentValue_ = getValueFromCursor(cursor_);
    auto decoded = KeyCodec::decodeEdgeTypeIndexKey(currentKey_);
    current_ = {decoded.src_vertex_id, decoded.dst_vertex_id, decoded.seq, ValueCodec::decodeU64(currentValue_)};
    valid_ = true;
}

bool EdgeTypeScanCursorImpl::valid() const {
    return valid_;
}

const IGraphStore::EdgeTypeIndexEntry& EdgeTypeScanCursorImpl::entry() const {
    return current_;
}

void EdgeTypeScanCursorImpl::next() {
    int ret = cursor_->next(cursor_);
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

// ==================== Metadata Raw KV ====================

bool GraphStoreImpl::metadataPut(std::string_view key, std::string_view value) {
    return tablePut(defaultSession_, TABLE_METADATA, key, value);
}

std::optional<std::string> GraphStoreImpl::metadataGet(std::string_view key) {
    return tableGet(defaultSession_, TABLE_METADATA, key);
}

void GraphStoreImpl::metadataScan(std::string_view prefix,
                                  const std::function<bool(std::string_view, std::string_view)>& callback) {
    tableScan(defaultSession_, TABLE_METADATA, prefix, callback);
}

} // namespace eugraph
