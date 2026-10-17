#include "storage/data/sync_graph_data_store.hpp"

#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>

#include "storage/index/index_delta_codec.hpp"
#include "storage/kv/index_key_codec.hpp"

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

std::string_view getKeyViewFromCursor(WT_CURSOR* cursor) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    cursor->get_key(cursor, &item);
    return std::string_view(static_cast<const char*>(item.data), item.size);
}

std::string_view getValueViewFromCursor(WT_CURSOR* cursor) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    cursor->get_value(cursor, &item);
    return std::string_view(static_cast<const char*>(item.data), item.size);
}

} // anonymous namespace

// ==================== Lifecycle ====================

SyncGraphDataStore::~SyncGraphDataStore() {
    close();
}

bool SyncGraphDataStore::open(const std::string& db_path) {
    return open(db_path, "");
}

bool SyncGraphDataStore::open(const std::string& db_path, const std::string& wt_extra_config) {
    if (!openConnection(db_path, wt_extra_config))
        return false;

    if (!ensureGlobalTable(TABLE_LABEL_REVERSE) || !ensureGlobalTable(TABLE_EDGE_INDEX) ||
        !ensureGlobalTable(TABLE_VERTEX_EXISTENCE)) {
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
    ts->session = conn_.openSession();
    if (!ts->session) {
        spdlog::error("Failed to open transaction session");
        return INVALID_GRAPH_TXN;
    }

    int ret = ts->session.get()->begin_transaction(ts->session.get(), "isolation=snapshot");
    if (ret != 0) {
        spdlog::error("Failed to begin transaction: error {}", ret);
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

    int ret = ts->session.get()->commit_transaction(ts->session.get(), nullptr);
    closeTxnCursors(ts);

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

    int ret = ts->session.get()->rollback_transaction(ts->session.get(), nullptr);
    closeTxnCursors(ts);

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

    WtSession ddl_session(conn_.get());
    if (!ddl_session)
        return false;

    if (!ddl_session.createTable(fwd.c_str()))
        return false;
    if (!ddl_session.createTable(vprop.c_str()))
        return false;

    return true;
}

bool SyncGraphDataStore::dropLabel(LabelId label_id) {
    auto fwd = labelFwdTable(label_id);
    auto vprop = vpropTable(label_id);

    // Business-level (schema/lifecycle) exclusivity: serialises this DDL sequence against other
    // DDL and against checkpoint/close. Sessions are never shared any more, so the old
    // "close the default session, then reopen it" dance is gone -- it never quiesced anything
    // (other threads keep their own sessions), and it was the source of the shared session.
    std::lock_guard<std::recursive_mutex> lock(schemaMutex_);

    WtSession ddl_session = openAdminSession();
    if (!ddl_session)
        return false;

    ddl_session.get()->checkpoint(ddl_session.get(), nullptr);

    int ret = ddl_session.get()->drop(ddl_session.get(), fwd.c_str(), nullptr);
    if (ret != 0) {
        spdlog::error("Failed to drop label fwd table {}: error {}", fwd, wiredtiger_strerror(ret));
        return false;
    }

    ret = ddl_session.get()->drop(ddl_session.get(), vprop.c_str(), nullptr);
    if (ret != 0) {
        spdlog::error("Failed to drop vprop table {}: error {}", vprop, wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

bool SyncGraphDataStore::createEdgeLabel(EdgeLabelId edge_label_id) {
    auto etype = etypeTable(edge_label_id);
    auto eprop = epropTable(edge_label_id);

    WtSession ddl_session(conn_.get());
    if (!ddl_session)
        return false;

    if (!ddl_session.createTable(etype.c_str()))
        return false;
    if (!ddl_session.createTable(eprop.c_str()))
        return false;

    return true;
}

bool SyncGraphDataStore::dropEdgeLabel(EdgeLabelId edge_label_id) {
    auto etype = etypeTable(edge_label_id);
    auto eprop = epropTable(edge_label_id);

    // Same as dropLabel: schema/lifecycle exclusivity via the business lock; no session is
    // shared, so no close/reopen dance is needed.
    std::lock_guard<std::recursive_mutex> lock(schemaMutex_);

    WtSession ddl_session = openAdminSession();
    if (!ddl_session)
        return false;

    ddl_session.get()->checkpoint(ddl_session.get(), nullptr);

    int ret = ddl_session.get()->drop(ddl_session.get(), etype.c_str(), nullptr);
    if (ret != 0) {
        spdlog::error("Failed to drop etype table {}: error {}", etype, wiredtiger_strerror(ret));
        return false;
    }

    ret = ddl_session.get()->drop(ddl_session.get(), eprop.c_str(), nullptr);
    if (ret != 0) {
        spdlog::error("Failed to drop eprop table {}: error {}", eprop, wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

// ==================== Vertex ====================

bool SyncGraphDataStore::insertVertex(GraphTxnHandle txn, VertexId vid,
                                      std::span<const std::pair<LabelId, Properties>> label_props) {
    auto session = getSession(txn);
    if (!session)
        return false;

    auto ve_key = KeyCodec::encodeVertexExistenceKey(vid);
    if (!tablePutTxn(txn, session, TABLE_VERTEX_EXISTENCE, ve_key, {}))
        return false;

    for (const auto& [label_id, props] : label_props) {
        if (label_id == INVALID_LABEL_ID)
            continue;
        auto lr_key = KeyCodec::encodeLabelReverseKey(vid, label_id);
        if (!tablePutTxn(txn, session, TABLE_LABEL_REVERSE, lr_key, {}))
            return false;

        auto lf_key = KeyCodec::encodeLabelForwardKey(vid);
        if (!tablePutTxn(txn, session, labelFwdTable(label_id), lf_key, {}))
            return false;

        for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
            if (props[prop_id].has_value()) {
                auto vp_key = KeyCodec::encodeVPropKey(vid, prop_id);
                auto vp_val = ValueCodec::encode(props[prop_id].value());
                if (!tablePutTxn(txn, session, vpropTable(label_id), vp_key, vp_val))
                    return false;
            }
        }
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

    tableDel(session, TABLE_VERTEX_EXISTENCE, KeyCodec::encodeVertexExistenceKey(vid));

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

std::vector<std::optional<Properties>> SyncGraphDataStore::getVertexPropertiesBatchProjected(
    GraphTxnHandle txn, LabelId label_id, const std::vector<VertexId>& vids, const std::vector<uint16_t>& proj) {
    std::vector<std::optional<Properties>> out(vids.size());
    if (proj.empty())
        return out;
    auto session = getSession(txn);
    if (!session)
        return out;
    // One cursor for the whole batch (see the header comment for the measured rationale).
    auto cursor = openCursor(session, vpropTable(label_id));
    if (!cursor)
        return out;
    auto* c = cursor.get();
    const size_t width = static_cast<size_t>(*std::max_element(proj.begin(), proj.end())) + 1;
    for (size_t i = 0; i < vids.size(); ++i) {
        Properties props;
        props.resize(width);
        bool found = false;
        for (uint16_t pid : proj) {
            auto key = KeyCodec::encodeVPropKey(vids[i], pid);
            setItem(c, key);
            if (c->search(c) != 0)
                continue;
            props[pid] = ValueCodec::decode(getValueFromCursor(c));
            found = true;
        }
        if (found)
            out[i] = std::move(props);
    }
    return out;
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

bool SyncGraphDataStore::putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id,
                                             const Properties& props) {
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

std::vector<LabelIdSet> SyncGraphDataStore::getVertexLabelsBatch(GraphTxnHandle txn,
                                                                 const std::vector<VertexId>& vids) {
    std::vector<LabelIdSet> out(vids.size());
    auto session = getSession(txn);
    if (!session)
        return out;

    auto cursor = openCursor(session, TABLE_LABEL_REVERSE);
    if (!cursor)
        return out;
    auto* c = cursor.get();
    std::string prefix;
    for (size_t i = 0; i < vids.size(); ++i) {
        prefix = KeyCodec::encodeLabelReversePrefix(vids[i]);
        setItem(c, prefix);
        int exact = 0;
        int ret = c->search_near(c, &exact);
        if (ret == WT_NOTFOUND)
            continue;
        if (ret != 0 || exact < 0)
            ret = c->next(c);
        while (ret == 0) {
            std::string key = getKeyFromCursor(c);
            if (!key.starts_with(prefix))
                break;
            auto [_, label_id] = KeyCodec::decodeLabelReverseKey(key);
            out[i].insert(label_id);
            ret = c->next(c);
        }
    }
    return out;
}

std::vector<std::optional<Properties>>
SyncGraphDataStore::getVertexPropertiesBatch(GraphTxnHandle txn, LabelId label_id, const std::vector<VertexId>& vids) {
    std::vector<std::optional<Properties>> out(vids.size());
    auto session = getSession(txn);
    if (!session)
        return out;

    auto cursor = openCursor(session, vpropTable(label_id));
    if (!cursor)
        return out;
    auto* c = cursor.get();
    std::string prefix;
    for (size_t i = 0; i < vids.size(); ++i) {
        prefix = KeyCodec::encodeVPropPrefix(vids[i]);
        setItem(c, prefix);
        int exact = 0;
        int ret = c->search_near(c, &exact);
        if (ret == WT_NOTFOUND)
            continue;
        if (ret != 0 || exact < 0)
            ret = c->next(c);
        Properties props;
        bool found = false;
        while (ret == 0) {
            std::string key = getKeyFromCursor(c);
            if (!key.starts_with(prefix))
                break;
            auto [_, prop_id] = KeyCodec::decodeVPropKey(key);
            if (prop_id >= props.size())
                props.resize(prop_id + 1);
            props[prop_id] = ValueCodec::decode(getValueFromCursor(c));
            found = true;
            ret = c->next(c);
        }
        if (found)
            out[i] = std::move(props);
    }
    return out;
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

// ==================== All-Vertex Scan ====================

void SyncGraphDataStore::scanAllVertices(GraphTxnHandle txn, const std::function<bool(VertexId)>& callback) {
    auto session = getSession(txn);
    if (!session)
        return;

    tableScan(session, TABLE_VERTEX_EXISTENCE, {}, [&](std::string_view key, std::string_view) {
        VertexId vid = KeyCodec::decodeVertexExistenceKey(key);
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
    if (!tablePutTxn(txn, session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(out_key), edge_id_val))
        return false;

    KeyCodec::EdgeIndexKey in_key{dst_id, Direction::IN, label_id, src_id, seq};
    if (!tablePutTxn(txn, session, TABLE_EDGE_INDEX, KeyCodec::encodeEdgeIndexKey(in_key), edge_id_val))
        return false;

    KeyCodec::EdgeTypeIndexKey type_key{src_id, dst_id, seq};
    if (!tablePutTxn(txn, session, etypeTable(label_id), KeyCodec::encodeEdgeTypeIndexKey(type_key), edge_id_val))
        return false;

    for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
        if (props[prop_id].has_value()) {
            auto ep_key = KeyCodec::encodeEPropKey(eid, prop_id);
            auto ep_val = ValueCodec::encode(props[prop_id].value());
            if (!tablePutTxn(txn, session, epropTable(label_id), ep_key, ep_val))
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

std::vector<std::optional<PropertyValue>> SyncGraphDataStore::getEdgePropertyBatch(GraphTxnHandle txn,
                                                                                   EdgeLabelId label_id,
                                                                                   const std::vector<EdgeId>& edge_ids,
                                                                                   uint16_t prop_id) {
    std::vector<std::optional<PropertyValue>> out;
    out.reserve(edge_ids.size());
    auto session = getSession(txn);
    if (!session) {
        out.resize(edge_ids.size());
        return out;
    }

    auto cursor = openCursor(session, epropTable(label_id));
    if (!cursor) {
        out.resize(edge_ids.size());
        return out;
    }

    auto* c = cursor.get();
    std::string key;
    for (EdgeId eid : edge_ids) {
        key = KeyCodec::encodeEPropKey(eid, prop_id);
        setItem(c, key);
        int ret = c->search(c);
        if (ret != 0) {
            out.emplace_back(std::nullopt);
            continue;
        }
        out.emplace_back(ValueCodec::decode(getValueFromCursor(c)));
    }
    return out;
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

    if (direction == Direction::BOTH) {
        scanEdges(txn, vid, Direction::OUT, label_filter, callback);
        scanEdges(txn, vid, Direction::IN, label_filter, callback);
        return;
    }

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
    if (label_id == INVALID_LABEL_ID)
        return nullptr;
    auto session = getSession(txn);
    if (!session)
        return nullptr;

    return std::make_unique<VertexScanCursorImpl>(session, labelFwdTable(label_id));
}

std::unique_ptr<ISyncGraphDataStore::IVertexScanCursor>
SyncGraphDataStore::createAllVertexScanCursor(GraphTxnHandle txn) {
    auto session = getSession(txn);
    if (!session)
        return nullptr;
    return std::make_unique<VertexScanCursorImpl>(session, TABLE_VERTEX_EXISTENCE, /*existence_keys=*/true);
}

std::unique_ptr<ISyncGraphDataStore::IEdgeTypeScanCursor>
SyncGraphDataStore::createEdgeTypeScanCursor(GraphTxnHandle txn, EdgeLabelId label_id,
                                             std::optional<VertexId> src_filter, std::optional<VertexId> dst_filter) {
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

VertexScanCursorImpl::VertexScanCursorImpl(WT_SESSION* session, const std::string& table_name, bool existence_keys)
    : cursor_(session, table_name), session_(session), existence_keys_(existence_keys) {
    if (!cursor_)
        return;

    int ret = cursor_.get()->next(cursor_.get());
    if (ret == 0) {
        readCurrent();
    }
}

VertexScanCursorImpl::~VertexScanCursorImpl() = default;

void VertexScanCursorImpl::readCurrent() {
    currentKey_ = getKeyFromCursor(cursor_.get());
    current_vid_ = existence_keys_ ? KeyCodec::decodeVertexExistenceKey(currentKey_)
                                   : KeyCodec::decodeLabelForwardKey(currentKey_);
    valid_ = true;
}

bool VertexScanCursorImpl::valid() const {
    return valid_;
}
VertexId VertexScanCursorImpl::vertexId() const {
    return current_vid_;
}

void VertexScanCursorImpl::next() {
    int ret = cursor_.get()->next(cursor_.get());
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

// ==================== EdgeScanCursorImpl ====================

EdgeScanCursorImpl::EdgeScanCursorImpl(WT_SESSION* session, std::string_view prefix)
    : cursor_(session, TABLE_EDGE_INDEX), session_(session), prefix_(prefix) {
    if (!cursor_)
        return;

    auto* c = cursor_.get();
    setItem(c, prefix_);
    int exact = 0;
    int ret = c->search_near(c, &exact);

    if (ret != 0 || exact < 0) {
        ret = c->next(c);
    }

    if (ret == 0) {
        readCurrent();
    }
}

EdgeScanCursorImpl::~EdgeScanCursorImpl() = default;

void EdgeScanCursorImpl::readCurrent() {
    auto* c = cursor_.get();
    const auto key = getKeyViewFromCursor(c);
    if (!key.starts_with(prefix_)) {
        valid_ = false;
        return;
    }

    const auto value = getValueViewFromCursor(c);
    auto decoded = KeyCodec::decodeEdgeIndexKey(key);
    current_ = {decoded.neighbor_id, decoded.edge_label_id, decoded.seq, ValueCodec::decodeU64(value)};
    valid_ = true;
}

bool EdgeScanCursorImpl::valid() const {
    return valid_;
}
const ISyncGraphDataStore::EdgeIndexEntry& EdgeScanCursorImpl::entry() const {
    return current_;
}

void EdgeScanCursorImpl::next() {
    auto* c = cursor_.get();
    int ret = c->next(c);
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

// BOTH-direction cursor: merges OUT and IN scans into a single iterator.
class MergedEdgeScanCursor : public ISyncGraphDataStore::IEdgeScanCursor {
public:
    MergedEdgeScanCursor(WT_SESSION* session, VertexId vid, std::optional<EdgeLabelId> label_filter)
        : cursor1_(session, makePrefix(vid, Direction::OUT, label_filter)),
          cursor2_(session, makePrefix(vid, Direction::IN, label_filter)), onFirst_(true) {}

    const EdgeScanCursorImpl& active() const {
        if (onFirst_ && cursor1_.valid())
            return cursor1_;
        return cursor2_;
    }
    EdgeScanCursorImpl& active() {
        if (onFirst_ && cursor1_.valid())
            return cursor1_;
        return cursor2_;
    }

    bool valid() const override {
        return active().valid();
    }
    const ISyncGraphDataStore::EdgeIndexEntry& entry() const override {
        return active().entry();
    }
    void next() override {
        auto& a = active();
        a.next();
        if (!a.valid() && onFirst_)
            onFirst_ = false;
    }

private:
    static std::string makePrefix(VertexId vid, Direction dir, std::optional<EdgeLabelId> label) {
        if (label)
            return KeyCodec::encodeEdgeIndexPrefix(vid, dir, *label);
        return KeyCodec::encodeEdgeIndexPrefix(vid, dir);
    }

    EdgeScanCursorImpl cursor1_, cursor2_;
    bool onFirst_;
};

std::unique_ptr<ISyncGraphDataStore::IEdgeScanCursor>
SyncGraphDataStore::createEdgeScanCursor(GraphTxnHandle txn, VertexId vid, Direction direction,
                                         std::optional<EdgeLabelId> label_filter) {
    auto session = getSession(txn);
    if (!session)
        return nullptr;

    if (direction == Direction::BOTH)
        return std::make_unique<MergedEdgeScanCursor>(session, vid, label_filter);

    std::string prefix;
    if (label_filter) {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction, *label_filter);
    } else {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction);
    }

    return std::make_unique<EdgeScanCursorImpl>(session, prefix);
}

// ==================== EdgeTypeScanCursorImpl ====================

EdgeTypeScanCursorImpl::EdgeTypeScanCursorImpl(WT_SESSION* session, const std::string& table_name,
                                               std::string_view prefix)
    : cursor_(session, table_name), session_(session), prefix_(prefix) {
    if (!cursor_)
        return;

    auto* c = cursor_.get();
    int ret;
    if (prefix_.empty()) {
        ret = c->next(c);
    } else {
        setItem(c, prefix_);
        int exact = 0;
        ret = c->search_near(c, &exact);
        if (ret != 0 || exact < 0) {
            ret = c->next(c);
        }
    }

    if (ret == 0) {
        readCurrent();
    }
}

EdgeTypeScanCursorImpl::~EdgeTypeScanCursorImpl() = default;

void EdgeTypeScanCursorImpl::readCurrent() {
    auto* c = cursor_.get();
    const auto key = getKeyViewFromCursor(c);
    if (!prefix_.empty() && !key.starts_with(prefix_)) {
        valid_ = false;
        return;
    }

    const auto value = getValueViewFromCursor(c);
    auto decoded = KeyCodec::decodeEdgeTypeIndexKey(key);
    current_ = {decoded.src_vertex_id, decoded.dst_vertex_id, decoded.seq, ValueCodec::decodeU64(value)};
    valid_ = true;
}

bool EdgeTypeScanCursorImpl::valid() const {
    return valid_;
}
const ISyncGraphDataStore::EdgeTypeIndexEntry& EdgeTypeScanCursorImpl::entry() const {
    return current_;
}

void EdgeTypeScanCursorImpl::next() {
    auto* c = cursor_.get();
    int ret = c->next(c);
    if (ret != 0) {
        valid_ = false;
        return;
    }
    readCurrent();
}

// ==================== Index DDL ====================

bool SyncGraphDataStore::createIndex(const std::string& table_name) {
    return ensureGlobalTable(table_name.c_str());
}

bool SyncGraphDataStore::dropIndex(const std::string& table_name) {
    std::string uri = table_name;
    // ensureGlobalTable stores as "table:xxx", drop needs "table:xxx".
    // DDL sequence => business-level exclusivity, and a short-lived session of our own.
    std::lock_guard<std::recursive_mutex> lock(schemaMutex_);
    WtSession admin = openAdminSession();
    if (!admin)
        return false;
    int ret = admin.get()->drop(admin.get(), uri.c_str(), nullptr);
    if (ret != 0 && ret != ENOENT) {
        spdlog::error("dropIndex: failed to drop {}: {}", uri, wiredtiger_strerror(ret));
        return false;
    }
    return true;
}

// ==================== Index Entry Operations ====================

namespace {
/// 定义在本文件后部（索引扫描实现处）；此处前向声明以便变更表扫描复用。
bool positionIndexCursor(WT_CURSOR* c, std::string_view from);
} // namespace

bool SyncGraphDataStore::putIndexEntryByKey(GraphTxnHandle txn, const std::string& table, std::string_view key,
                                            std::string_view payload) {
    auto session = getSession(txn);
    if (!session)
        return false;
    return tablePutTxn(txn, session, table, key, payload);
}

bool SyncGraphDataStore::deleteIndexEntryByKey(GraphTxnHandle txn, const std::string& table, std::string_view key) {
    auto session = getSession(txn);
    if (!session)
        return false;
    return tableDel(session, table, key);
}

bool SyncGraphDataStore::scanDeltaWithKey(
    GraphTxnHandle txn, const std::string& table,
    const std::function<bool(uint64_t, std::string_view, std::string_view)>& callback, std::string_view start_after,
    std::string* last_key_out) {
    auto* session = getSession(txn);
    auto cursor = openCursor(session, table);
    if (!cursor)
        return false;
    auto* c = cursor.get();
    if (!positionIndexCursor(c, start_after))
        return false;
    while (true) {
        WT_ITEM key_item;
        if (c->get_key(c, &key_item) != 0)
            break;
        std::string_view key(static_cast<const char*>(key_item.data), key_item.size);
        if (!start_after.empty() && key <= start_after) {
            if (c->next(c) != 0)
                break;
            continue;
        }
        if (last_key_out != nullptr)
            last_key_out->assign(key.data(), key.size());
        WT_ITEM val_item;
        int vret = c->get_value(c, &val_item);
        std::string_view val((vret == 0 && val_item.data) ? static_cast<const char*>(val_item.data) : "",
                             (vret == 0) ? val_item.size : 0);
        if (!callback(IndexKeyCodec::decodeEntityId(key), key, val))
            break;
        if (c->next(c) != 0)
            break;
    }
    return true;
}

bool SyncGraphDataStore::putDeltaEntry(GraphTxnHandle txn, const std::string& table, const PropertyValue& value,
                                       uint64_t entity_id, bool is_delete, std::string_view payload) {
    auto key = IndexKeyCodec::encodeIndexKey(value, entity_id);
    auto session = getSession(txn);
    if (!session)
        return false;
    return tablePutTxn(txn, session, table, key, encodeDeltaValue(is_delete, payload));
}

bool SyncGraphDataStore::putDeltaEntry(GraphTxnHandle txn, const std::string& table,
                                       const std::vector<PropertyValue>& values, uint64_t entity_id, bool is_delete,
                                       std::string_view payload) {
    auto key = IndexKeyCodec::encodeIndexKey(values, entity_id);
    auto session = getSession(txn);
    if (!session)
        return false;
    return tablePutTxn(txn, session, table, key, encodeDeltaValue(is_delete, payload));
}

bool SyncGraphDataStore::insertIndexEntry(GraphTxnHandle txn, const std::string& table, const PropertyValue& value,
                                          uint64_t entity_id) {
    auto key = IndexKeyCodec::encodeIndexKey(value, entity_id);
    auto session = getSession(txn);
    if (!session)
        return false;
    return tablePutTxn(txn, session, table, key, {});
}

bool SyncGraphDataStore::deleteIndexEntry(GraphTxnHandle txn, const std::string& table, const PropertyValue& value,
                                          uint64_t entity_id) {
    auto key = IndexKeyCodec::encodeIndexKey(value, entity_id);
    auto session = getSession(txn);
    if (!session)
        return false;
    return tableDel(session, table, key);
}

bool SyncGraphDataStore::insertIndexEntry(GraphTxnHandle txn, const std::string& table, const PropertyValue& value,
                                          uint64_t entity_id, std::string_view payload) {
    auto key = IndexKeyCodec::encodeIndexKey(value, entity_id);
    auto session = getSession(txn);
    if (!session)
        return false;
    return tablePutTxn(txn, session, table, key, payload);
}

bool SyncGraphDataStore::insertIndexEntry(GraphTxnHandle txn, const std::string& table,
                                          const std::vector<PropertyValue>& values, uint64_t entity_id) {
    auto key = IndexKeyCodec::encodeIndexKey(values, entity_id);
    auto session = getSession(txn);
    if (!session)
        return false;
    return tablePutTxn(txn, session, table, key, {});
}

bool SyncGraphDataStore::insertIndexEntry(GraphTxnHandle txn, const std::string& table,
                                          const std::vector<PropertyValue>& values, uint64_t entity_id,
                                          std::string_view payload) {
    auto key = IndexKeyCodec::encodeIndexKey(values, entity_id);
    auto session = getSession(txn);
    if (!session)
        return false;
    return tablePutTxn(txn, session, table, key, payload);
}

bool SyncGraphDataStore::deleteIndexEntry(GraphTxnHandle txn, const std::string& table,
                                          const std::vector<PropertyValue>& values, uint64_t entity_id) {
    auto key = IndexKeyCodec::encodeIndexKey(values, entity_id);
    auto session = getSession(txn);
    if (!session)
        return false;
    return tableDel(session, table, key);
}

bool SyncGraphDataStore::checkUniqueConstraint(GraphTxnHandle txn, const std::string& table,
                                               const PropertyValue& value) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(value);
    auto session = getSession(txn);
    if (!session)
        return false;
    bool found = false;
    tableScan(session, table, prefix, [&](std::string_view key, std::string_view /*val*/) {
        if (key.size() >= prefix.size()) {
            found = true;
            return false;
        }
        return true;
    });
    return !found;
}

bool SyncGraphDataStore::checkUniqueConstraint(GraphTxnHandle txn, const std::string& table,
                                               const std::vector<PropertyValue>& values) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(values);
    auto session = getSession(txn);
    if (!session)
        return false;
    bool found = false;
    tableScan(session, table, prefix, [&](std::string_view key, std::string_view /*val*/) {
        if (key.size() >= prefix.size()) {
            found = true;
            return false;
        }
        return true;
    });
    return !found;
}

// ==================== Index Scan ====================

void SyncGraphDataStore::scanIndexEquality(GraphTxnHandle txn, const std::string& table, const PropertyValue& value,
                                           const std::function<bool(uint64_t)>& callback) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(value);
    auto* session = getSession(txn);
    tableScan(session, table, prefix, [&](std::string_view key, std::string_view /*val*/) {
        auto entity_id = IndexKeyCodec::decodeEntityId(key);
        return callback(entity_id);
    });
}

void SyncGraphDataStore::scanIndexEquality(GraphTxnHandle txn, const std::string& table,
                                           const std::vector<PropertyValue>& values,
                                           const std::function<bool(uint64_t)>& callback) {
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(values);
    auto* session = getSession(txn);
    tableScan(session, table, prefix, [&](std::string_view key, std::string_view /*val*/) {
        auto entity_id = IndexKeyCodec::decodeEntityId(key);
        return callback(entity_id);
    });
}

void SyncGraphDataStore::scanIndexRange(GraphTxnHandle txn, const std::string& table,
                                        const std::optional<PropertyValue>& start,
                                        const std::optional<PropertyValue>& end,
                                        const std::function<bool(uint64_t)>& callback) {
    auto* session = getSession(txn);
    auto cursor = openCursor(session, table);
    if (!cursor)
        return;

    auto* c = cursor.get();
    std::string start_encoded;
    if (start.has_value()) {
        // Append max entity_id suffix so search_near skips past all entries
        // with the exact start value (exclusive lower bound)
        start_encoded = IndexKeyCodec::encodeSortableValue(*start);
        start_encoded.append(8, '\xFF');
    }

    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValue(*end);

    int ret;
    if (!start_encoded.empty()) {
        WT_ITEM item;
        item.data = start_encoded.data();
        item.size = start_encoded.size();
        c->set_key(c, &item);
        int cmp;
        ret = c->search_near(c, &cmp);
        if (ret != 0)
            return;
        // cmp < 0: returned key < search key → advance
        // cmp == 0: exact match → also advance (exclusive lower bound)
        if (cmp <= 0) {
            ret = c->next(c);
            if (ret != 0)
                return;
        }
    } else {
        ret = c->reset(c);
        if (ret != 0)
            return;
        ret = c->next(c);
        if (ret != 0)
            return;
    }

    while (true) {
        WT_ITEM key_item;
        ret = c->get_key(c, &key_item);
        if (ret != 0)
            break;

        std::string_view key(static_cast<const char*>(key_item.data), key_item.size);

        // Check upper bound: compare sortable value part (strip 8-byte entity_id suffix)
        if (!end_encoded.empty()) {
            auto key_sortable = key.substr(0, key.size() - 8);
            if (key_sortable >= end_encoded)
                break;
        }

        auto entity_id = IndexKeyCodec::decodeEntityId(key);
        if (!callback(entity_id))
            break;

        ret = c->next(c);
        if (ret != 0)
            break;
    }
}

void SyncGraphDataStore::scanIndexRange(GraphTxnHandle txn, const std::string& table,
                                        const std::optional<std::vector<PropertyValue>>& start,
                                        const std::optional<std::vector<PropertyValue>>& end,
                                        const std::function<bool(uint64_t)>& callback) {
    auto* session = getSession(txn);
    auto cursor = openCursor(session, table);
    if (!cursor)
        return;

    auto* c = cursor.get();
    std::string start_encoded;
    if (start.has_value()) {
        start_encoded = IndexKeyCodec::encodeSortableValues(*start);
        start_encoded.append(8, '\xFF');
    }

    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValues(*end);

    int ret;
    if (!start_encoded.empty()) {
        WT_ITEM item;
        item.data = start_encoded.data();
        item.size = start_encoded.size();
        c->set_key(c, &item);
        int cmp;
        ret = c->search_near(c, &cmp);
        if (ret != 0)
            return;
        if (cmp <= 0) {
            ret = c->next(c);
            if (ret != 0)
                return;
        }
    } else {
        ret = c->reset(c);
        if (ret != 0)
            return;
        ret = c->next(c);
        if (ret != 0)
            return;
    }

    while (true) {
        WT_ITEM key_item;
        ret = c->get_key(c, &key_item);
        if (ret != 0)
            break;

        std::string_view key(static_cast<const char*>(key_item.data), key_item.size);

        if (!end_encoded.empty()) {
            auto key_sortable = key.substr(0, key.size() - 8);
            if (key_sortable >= end_encoded)
                break;
        }

        auto entity_id = IndexKeyCodec::decodeEntityId(key);
        if (!callback(entity_id))
            break;

        ret = c->next(c);
        if (ret != 0)
            break;
    }
}

// ==================== Index Scan With Value ====================

namespace {
/// 定位扫描游标：`from` 为空 ⇒ 从头（reset + next）；否则定位到该键**之后**（分批续扫用）。
/// **续扫是分批正确性的前提**：先前每批从头重扫、满批即停，只交付第一批（BATCH=1024），
/// >1 批的结果被静默截断。
bool positionIndexCursor(WT_CURSOR* c, std::string_view from) {
    if (from.empty()) {
        if (c->reset(c) != 0)
            return false;
        return c->next(c) == 0;
    }
    WT_ITEM item;
    item.data = from.data();
    item.size = from.size();
    c->set_key(c, &item);
    int cmp = 0;
    if (c->search_near(c, &cmp) != 0)
        return false;
    if (cmp <= 0)
        return c->next(c) == 0; // 命中该键或落在其前 ⇒ 跳过它本身
    return true;                // 已在目标键之后
}

/// 读取当前键值、回填 `last_key_out`、交给回调；返回是否继续扫描。
/// 语义：回调返回 false 时，**该键已被消费**（调用方据此作为下一批的续扫起点）。
bool emitIndexEntryWithValue(WT_CURSOR* c, std::string_view start_after, std::string* last_key_out,
                             const std::function<bool(uint64_t, std::string_view)>& callback) {
    WT_ITEM key_item;
    if (c->get_key(c, &key_item) != 0)
        return false;
    std::string_view key(static_cast<const char*>(key_item.data), key_item.size);
    if (!start_after.empty() && key <= start_after)
        return true; // 续扫起点之前（含自身）⇒ 跳过
    if (last_key_out != nullptr)
        last_key_out->assign(key.data(), key.size());
    WT_ITEM val_item;
    int vret = c->get_value(c, &val_item);
    std::string_view val((vret == 0 && val_item.data) ? static_cast<const char*>(val_item.data) : "",
                         (vret == 0) ? val_item.size : 0);
    return callback(IndexKeyCodec::decodeEntityId(key), val);
}

/// 等值（前缀）扫描主体：从游标当前位置沿前缀前进，越出前缀即结束。
void loopEqualityPrefix(WT_CURSOR* c, std::string_view prefix, std::string_view start_after, std::string* last_key_out,
                        const std::function<bool(uint64_t, std::string_view)>& cb) {
    while (true) {
        WT_ITEM key_item;
        if (c->get_key(c, &key_item) != 0)
            break;
        std::string_view key(static_cast<const char*>(key_item.data), key_item.size);
        if (key.size() < prefix.size() || key.compare(0, prefix.size(), prefix) != 0)
            break;
        if (!emitIndexEntryWithValue(c, start_after, last_key_out, cb))
            break;
        if (c->next(c) != 0)
            break;
    }
}

/// 范围扫描主体：按 end 界（键去掉尾部 8 字节实体 id 后比较）终止。
void loopRange(WT_CURSOR* c, std::string_view end_encoded, std::string_view start_after, std::string* last_key_out,
               const std::function<bool(uint64_t, std::string_view)>& cb) {
    while (true) {
        WT_ITEM key_item;
        if (c->get_key(c, &key_item) != 0)
            break;
        std::string_view key(static_cast<const char*>(key_item.data), key_item.size);
        if (!end_encoded.empty() && key.size() >= 8 && key.substr(0, key.size() - 8) >= end_encoded)
            break;
        if (!emitIndexEntryWithValue(c, start_after, last_key_out, cb))
            break;
        if (c->next(c) != 0)
            break;
    }
}
} // namespace

void SyncGraphDataStore::scanIndexEqualityWithValue(GraphTxnHandle txn, const std::string& table,
                                                    const PropertyValue& value,
                                                    const std::function<bool(uint64_t, std::string_view)>& callback,
                                                    std::string_view start_after, std::string* last_key_out) {
    auto* session = getSession(txn);
    auto cursor = openCursor(session, table);
    if (!cursor)
        return;
    auto* c = cursor.get();
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(value);
    // 首批发起时必须**定位到前缀起点**（reset+next 会从表首开始，前缀检查会立刻失败 ⇒ 等值查询返回 0）；
    // 续扫时定位到上一批最后的键之后。
    if (!positionIndexCursor(c, start_after.empty() ? std::string_view(prefix) : start_after))
        return;
    loopEqualityPrefix(c, prefix, start_after, last_key_out, callback);
}

void SyncGraphDataStore::scanIndexEqualityWithValue(GraphTxnHandle txn, const std::string& table,
                                                    const std::vector<PropertyValue>& values,
                                                    const std::function<bool(uint64_t, std::string_view)>& callback,
                                                    std::string_view start_after, std::string* last_key_out) {
    auto* session = getSession(txn);
    auto cursor = openCursor(session, table);
    if (!cursor)
        return;
    auto* c = cursor.get();
    auto prefix = IndexKeyCodec::encodeEqualityPrefix(values);
    if (!positionIndexCursor(c, start_after.empty() ? std::string_view(prefix) : start_after))
        return; // 同上：首批从前缀起点开始
    loopEqualityPrefix(c, prefix, start_after, last_key_out, callback);
}

void SyncGraphDataStore::scanIndexRangeWithValue(GraphTxnHandle txn, const std::string& table,
                                                 const std::optional<PropertyValue>& start,
                                                 const std::optional<PropertyValue>& end,
                                                 const std::function<bool(uint64_t, std::string_view)>& callback,
                                                 std::string_view start_after, std::string* last_key_out) {
    auto* session = getSession(txn);
    auto cursor = openCursor(session, table);
    if (!cursor)
        return;
    auto* c = cursor.get();
    std::string start_encoded;
    if (start.has_value()) {
        start_encoded = IndexKeyCodec::encodeSortableValue(*start);
        start_encoded.append(8, '\xFF');
    }
    if (!positionIndexCursor(c, start_after.empty() ? std::string_view(start_encoded) : start_after))
        return;
    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValue(*end);
    loopRange(c, end_encoded, start_after, last_key_out, callback);
}

void SyncGraphDataStore::scanIndexRangeWithValue(GraphTxnHandle txn, const std::string& table,
                                                 const std::optional<std::vector<PropertyValue>>& start,
                                                 const std::optional<std::vector<PropertyValue>>& end,
                                                 const std::function<bool(uint64_t, std::string_view)>& callback,
                                                 std::string_view start_after, std::string* last_key_out) {
    auto* session = getSession(txn);
    auto cursor = openCursor(session, table);
    if (!cursor)
        return;
    auto* c = cursor.get();
    std::string start_encoded;
    if (start.has_value()) {
        start_encoded = IndexKeyCodec::encodeSortableValues(*start);
        start_encoded.append(8, '\xFF');
    }
    if (!positionIndexCursor(c, start_after.empty() ? std::string_view(start_encoded) : start_after))
        return;
    std::string end_encoded;
    if (end.has_value())
        end_encoded = IndexKeyCodec::encodeSortableValues(*end);
    loopRange(c, end_encoded, start_after, last_key_out, callback);
}

// ==================== Index Cleanup ====================

void SyncGraphDataStore::dropAllIndexEntries(const std::string& table) {
    tableScan(getSession(INVALID_GRAPH_TXN), table, {}, [&](std::string_view key, std::string_view /*val*/) {
        tableDel(getSession(INVALID_GRAPH_TXN), table, key);
        return true; // continue
    });
}

} // namespace eugraph
