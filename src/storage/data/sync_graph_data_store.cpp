#include "storage/data/sync_graph_data_store.hpp"

#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace eugraph {

// ==================== WT Item Helpers (local) ====================

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

SyncGraphDataStore::~SyncGraphDataStore() {
    close();
}

bool SyncGraphDataStore::open(const std::string& db_path) {
    if (!openConnection(db_path))
        return false;

    if (!ensureGlobalTable(defaultSession_, TABLE_LABEL_REVERSE) ||
        !ensureGlobalTable(defaultSession_, TABLE_PK_FORWARD) ||
        !ensureGlobalTable(defaultSession_, TABLE_PK_REVERSE) ||
        !ensureGlobalTable(defaultSession_, TABLE_EDGE_INDEX)) {
        close();
        return false;
    }

    spdlog::info("Opened data store at: {}", db_path);
    return true;
}

void SyncGraphDataStore::close() {
    closeConnection();
    spdlog::info("Closed data store");
}

bool SyncGraphDataStore::isOpen() const {
    return WtStoreBase::isOpen();
}

// ==================== Transaction ====================

GraphTxnHandle SyncGraphDataStore::beginTransaction() {
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

bool SyncGraphDataStore::commitTransaction(GraphTxnHandle txn) {
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

bool SyncGraphDataStore::rollbackTransaction(GraphTxnHandle txn) {
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

// ==================== DDL ====================

bool SyncGraphDataStore::createLabel(LabelId label_id) {
    auto fwd = labelFwdTable(label_id);
    auto vprop = vpropTable(label_id);

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

bool SyncGraphDataStore::dropLabel(LabelId label_id) {
    auto fwd = labelFwdTable(label_id);
    auto vprop = vpropTable(label_id);

    if (defaultSession_) {
        defaultSession_->close(defaultSession_, nullptr);
        defaultSession_ = nullptr;
    }

    WT_SESSION* ddl_session = nullptr;
    int ret = conn_->open_session(conn_, nullptr, nullptr, &ddl_session);
    if (ret != 0) {
        spdlog::error("Failed to open DDL session: error {}", ret);
        conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
        return false;
    }

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

    ret = conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
    if (ret != 0) {
        spdlog::error("Failed to reopen default session: error {}", ret);
        return false;
    }

    return true;
}

bool SyncGraphDataStore::createEdgeLabel(EdgeLabelId edge_label_id) {
    auto etype = etypeTable(edge_label_id);
    auto eprop = epropTable(edge_label_id);

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

bool SyncGraphDataStore::dropEdgeLabel(EdgeLabelId edge_label_id) {
    auto etype = etypeTable(edge_label_id);
    auto eprop = epropTable(edge_label_id);

    if (defaultSession_) {
        defaultSession_->close(defaultSession_, nullptr);
        defaultSession_ = nullptr;
    }

    WT_SESSION* ddl_session = nullptr;
    int ret = conn_->open_session(conn_, nullptr, nullptr, &ddl_session);
    if (ret != 0) {
        spdlog::error("Failed to open DDL session: error {}", ret);
        conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
        return false;
    }

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

    ret = conn_->open_session(conn_, nullptr, nullptr, &defaultSession_);
    if (ret != 0) {
        spdlog::error("Failed to reopen default session: error {}", ret);
        return false;
    }

    return true;
}

// ==================== Vertex ====================

bool SyncGraphDataStore::insertVertex(GraphTxnHandle txn, VertexId vid,
                                      std::span<const std::pair<LabelId, Properties>> label_props,
                                      const PropertyValue* pk_value) {
    auto session = getSession(txn);
    if (!session)
        return false;

    for (const auto& [label_id, props] : label_props) {
        auto lr_key = KeyCodec::encodeLabelReverseKey(vid, label_id);
        if (!tablePut(session, TABLE_LABEL_REVERSE, lr_key, {}))
            return false;

        auto lf_key = KeyCodec::encodeLabelForwardKey(vid);
        if (!tablePut(session, labelFwdTable(label_id), lf_key, {}))
            return false;

        for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
            if (props[prop_id].has_value()) {
                auto vp_key = KeyCodec::encodeVPropKey(vid, prop_id);
                auto vp_val = ValueCodec::encode(props[prop_id].value());
                if (!tablePut(session, vpropTable(label_id), vp_key, vp_val))
                    return false;
            }
        }
    }

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

bool SyncGraphDataStore::deleteVertex(GraphTxnHandle txn, VertexId vid) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto labels = getVertexLabels(txn, vid);
    for (LabelId label_id : labels) {
        tableDel(session, TABLE_LABEL_REVERSE, KeyCodec::encodeLabelReverseKey(vid, label_id));
        tableDel(session, labelFwdTable(label_id), KeyCodec::encodeLabelForwardKey(vid));

        auto prefix = KeyCodec::encodeVPropPrefix(vid);
        tableScan(session, vpropTable(label_id), prefix, [&](std::string_view key, std::string_view) {
            tableDel(session, vpropTable(label_id), key);
            return true;
        });
    }

    auto pk_encoded = tableGet(session, TABLE_PK_REVERSE, KeyCodec::encodePkReverseKey(vid));
    if (pk_encoded) {
        tableDel(session, TABLE_PK_FORWARD, KeyCodec::encodePkForwardKey(*pk_encoded));
        tableDel(session, TABLE_PK_REVERSE, KeyCodec::encodePkReverseKey(vid));
    }

    return true;
}

// ==================== Vertex Properties ====================

std::optional<Properties> SyncGraphDataStore::getVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id) {
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

std::optional<PropertyValue> SyncGraphDataStore::getVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id,
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

bool SyncGraphDataStore::putVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id,
                                           const PropertyValue& value) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto key = KeyCodec::encodeVPropKey(vid, prop_id);
    return tablePut(session, vpropTable(label_id), key, ValueCodec::encode(value));
}

bool SyncGraphDataStore::putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id, const Properties& props) {
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

bool SyncGraphDataStore::deleteVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto key = KeyCodec::encodeVPropKey(vid, prop_id);
    return tableDel(session, vpropTable(label_id), key);
}

// ==================== Vertex Labels ====================

LabelIdSet SyncGraphDataStore::getVertexLabels(GraphTxnHandle txn, VertexId vid) {
    LabelIdSet labels;
    auto session = getSession(txn);
    if (!session)
        return labels;

    auto prefix = KeyCodec::encodeLabelReversePrefix(vid);
    tableScan(session, TABLE_LABEL_REVERSE, prefix, [&](std::string_view key, std::string_view) {
        auto [_, label_id] = KeyCodec::decodeLabelReverseKey(key);
        labels.insert(label_id);
        return true;
    });
    return labels;
}

bool SyncGraphDataStore::addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto lr_key = KeyCodec::encodeLabelReverseKey(vid, label_id);
    if (!tablePut(session, TABLE_LABEL_REVERSE, lr_key, {}))
        return false;

    auto lf_key = KeyCodec::encodeLabelForwardKey(vid);
    return tablePut(session, labelFwdTable(label_id), lf_key, {});
}

bool SyncGraphDataStore::removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) {
    auto session = getSession(txn);
    if (!session)
        return false;

    tableDel(session, TABLE_LABEL_REVERSE, KeyCodec::encodeLabelReverseKey(vid, label_id));
    tableDel(session, labelFwdTable(label_id), KeyCodec::encodeLabelForwardKey(vid));

    auto prefix = KeyCodec::encodeVPropPrefix(vid);
    tableScan(session, vpropTable(label_id), prefix, [&](std::string_view key, std::string_view) {
        tableDel(session, vpropTable(label_id), key);
        return true;
    });

    return true;
}

// ==================== Primary Key ====================

std::optional<VertexId> SyncGraphDataStore::getVertexIdByPk(const PropertyValue& pk_value) {
    auto pk_encoded = ValueCodec::encode(pk_value);
    auto key = KeyCodec::encodePkForwardKey(pk_encoded);
    auto val = tableGet(defaultSession_, TABLE_PK_FORWARD, key);
    if (!val)
        return std::nullopt;
    return ValueCodec::decodeU64(*val);
}

std::optional<PropertyValue> SyncGraphDataStore::getPkByVertexId(VertexId vid) {
    auto key = KeyCodec::encodePkReverseKey(vid);
    auto val = tableGet(defaultSession_, TABLE_PK_REVERSE, key);
    if (!val)
        return std::nullopt;
    return ValueCodec::decode(*val);
}

// ==================== Label Index Scan ====================

void SyncGraphDataStore::scanVerticesByLabel(GraphTxnHandle txn, LabelId label_id,
                                             const std::function<bool(VertexId)>& callback) {
    auto session = getSession(txn);
    if (!session)
        return;

    tableScan(session, labelFwdTable(label_id), {}, [&](std::string_view key, std::string_view) {
        VertexId vid = KeyCodec::decodeLabelForwardKey(key);
        return callback(vid);
    });
}

// ==================== Edge ====================

bool SyncGraphDataStore::insertEdge(GraphTxnHandle txn, EdgeId eid, VertexId src_id, VertexId dst_id,
                                    EdgeLabelId label_id, uint64_t seq, const Properties& props) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto edge_id_val = ValueCodec::encodeU64(eid);

    KeyCodec::EdgeIndexKey out_key{src_id, Direction::OUT, label_id, dst_id, seq};
    if (!tablePut(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(out_key), edge_id_val))
        return false;

    KeyCodec::EdgeIndexKey in_key{dst_id, Direction::IN, label_id, src_id, seq};
    if (!tablePut(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(in_key), edge_id_val))
        return false;

    KeyCodec::EdgeTypeIndexKey type_key{src_id, dst_id, seq};
    if (!tablePut(session, etypeTable(label_id), KeyCodec::encodeEdgeTypeIndexKey(type_key), edge_id_val))
        return false;

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

bool SyncGraphDataStore::deleteEdge(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, VertexId src_id,
                                    VertexId dst_id, uint64_t seq) {
    auto session = getSession(txn);
    if (!session)
        return false;

    KeyCodec::EdgeIndexKey out_key{src_id, Direction::OUT, label_id, dst_id, seq};
    tableDel(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(out_key));

    KeyCodec::EdgeIndexKey in_key{dst_id, Direction::IN, label_id, src_id, seq};
    tableDel(session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(in_key));

    KeyCodec::EdgeTypeIndexKey type_key{src_id, dst_id, seq};
    tableDel(session, etypeTable(label_id), KeyCodec::encodeEdgeTypeIndexKey(type_key));

    auto prefix = KeyCodec::encodeEPropPrefix(eid);
    tableScan(session, epropTable(label_id), prefix, [&](std::string_view key, std::string_view) {
        tableDel(session, epropTable(label_id), key);
        return true;
    });

    return true;
}

// ==================== Edge Properties ====================

std::optional<Properties> SyncGraphDataStore::getEdgeProperties(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid) {
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

std::optional<PropertyValue> SyncGraphDataStore::getEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid,
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

bool SyncGraphDataStore::putEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                                         const PropertyValue& value) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto key = KeyCodec::encodeEPropKey(eid, prop_id);
    return tablePut(session, epropTable(label_id), key, ValueCodec::encode(value));
}

bool SyncGraphDataStore::deleteEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto key = KeyCodec::encodeEPropKey(eid, prop_id);
    return tableDel(session, epropTable(label_id), key);
}

// ==================== Edge Traversal ====================

void SyncGraphDataStore::scanEdges(GraphTxnHandle txn, VertexId vid, Direction direction,
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
        EdgeIndexEntry entry{decoded.neighbor_id, decoded.edge_label_id, decoded.seq, ValueCodec::decodeU64(value)};
        return callback(entry);
    });
}

void SyncGraphDataStore::scanEdgesByType(GraphTxnHandle txn, EdgeLabelId label_id, std::optional<VertexId> src_filter,
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

    tableScan(session, etypeTable(label_id), prefix, [&](std::string_view key, std::string_view value) {
        auto decoded = KeyCodec::decodeEdgeTypeIndexKey(key);
        EdgeTypeIndexEntry entry{decoded.src_vertex_id, decoded.dst_vertex_id, decoded.seq,
                                 ValueCodec::decodeU64(value)};
        return callback(entry);
    });
}

// ==================== Statistics ====================

uint64_t SyncGraphDataStore::countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) {
    uint64_t count = 0;
    scanVerticesByLabel(txn, label_id, [&](VertexId) {
        ++count;
        return true;
    });
    return count;
}

uint64_t SyncGraphDataStore::countDegree(GraphTxnHandle txn, VertexId vid, Direction direction,
                                         std::optional<EdgeLabelId> label_filter) {
    uint64_t count = 0;
    scanEdges(txn, vid, direction, label_filter, [&](const EdgeIndexEntry&) {
        ++count;
        return true;
    });
    return count;
}

// ==================== Scan Cursor Factories ====================

std::unique_ptr<ISyncGraphDataStore::IVertexScanCursor> SyncGraphDataStore::createVertexScanCursor(GraphTxnHandle txn,
                                                                                                   LabelId label_id) {
    auto session = getSession(txn);
    if (!session)
        return nullptr;

    return std::make_unique<VertexScanCursorImpl>(session, labelFwdTable(label_id));
}

std::unique_ptr<ISyncGraphDataStore::IEdgeScanCursor>
SyncGraphDataStore::createEdgeScanCursor(GraphTxnHandle txn, VertexId vid, Direction direction,
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

std::unique_ptr<ISyncGraphDataStore::IEdgeTypeScanCursor>
SyncGraphDataStore::createEdgeTypeScanCursor(GraphTxnHandle txn, EdgeLabelId label_id, std::optional<VertexId> src_filter,
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

bool VertexScanCursorImpl::valid() const { return valid_; }
VertexId VertexScanCursorImpl::vertexId() const { return current_vid_; }

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

bool EdgeScanCursorImpl::valid() const { return valid_; }
const ISyncGraphDataStore::EdgeIndexEntry& EdgeScanCursorImpl::entry() const { return current_; }

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

bool EdgeTypeScanCursorImpl::valid() const { return valid_; }
const ISyncGraphDataStore::EdgeTypeIndexEntry& EdgeTypeScanCursorImpl::entry() const { return current_; }

void EdgeTypeScanCursorImpl::next() {
    int ret = cursor_->next(cursor_);
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

} // namespace eugraph
