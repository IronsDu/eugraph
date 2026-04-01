#include "storage/graph_store_impl.hpp"
#include "storage/kv/key_codec.hpp"
#include "storage/kv/value_codec.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {

GraphStoreImpl::GraphStoreImpl(std::unique_ptr<IKVEngine> engine)
    : engine_(std::move(engine)) {}

GraphStoreImpl::~GraphStoreImpl() {
    close();
}

bool GraphStoreImpl::open(const std::string& db_path) {
    return engine_->open(db_path);
}

void GraphStoreImpl::close() {
    engine_->close();
}

bool GraphStoreImpl::isOpen() const {
    return engine_->isOpen();
}

// ==================== Transaction ====================

GraphTxnHandle GraphStoreImpl::beginTransaction() {
    return static_cast<GraphTxnHandle>(engine_->beginTransaction());
}

bool GraphStoreImpl::commitTransaction(GraphTxnHandle txn) {
    return engine_->commitTransaction(static_cast<IKVEngine::TxnHandle>(txn));
}

bool GraphStoreImpl::rollbackTransaction(GraphTxnHandle txn) {
    return engine_->rollbackTransaction(static_cast<IKVEngine::TxnHandle>(txn));
}

// ==================== Helpers ====================

bool GraphStoreImpl::doPut(GraphTxnHandle txn, std::string_view key, std::string_view value) {
    if (txn != INVALID_GRAPH_TXN) {
        return engine_->put(static_cast<IKVEngine::TxnHandle>(txn), key, value);
    }
    return engine_->put(key, value);
}

std::optional<std::string> GraphStoreImpl::doGet(GraphTxnHandle txn, std::string_view key) {
    if (txn != INVALID_GRAPH_TXN) {
        return engine_->get(static_cast<IKVEngine::TxnHandle>(txn), key);
    }
    return engine_->get(key);
}

bool GraphStoreImpl::doDel(GraphTxnHandle txn, std::string_view key) {
    if (txn != INVALID_GRAPH_TXN) {
        return engine_->del(static_cast<IKVEngine::TxnHandle>(txn), key);
    }
    return engine_->del(key);
}

void GraphStoreImpl::doPrefixScan(
    GraphTxnHandle txn, std::string_view prefix,
    const std::function<bool(std::string_view, std::string_view)>& callback) {
    // TODO: add txn-scoped prefix scan to IKVEngine for repeatable-read.
    engine_->prefixScan(prefix, callback);
}

// ==================== Vertex ====================

bool GraphStoreImpl::insertVertex(
    GraphTxnHandle txn, VertexId vid,
    std::span<const std::pair<LabelId, Properties>> label_props,
    const PropertyValue* pk_value) {

    for (const auto& [label_id, props] : label_props) {
        // L| reverse index
        auto l_key = KeyCodec::encodeLabelReverseKey(vid, label_id);
        if (!doPut(txn, l_key, {})) return false;

        // I| forward index
        auto i_key = KeyCodec::encodeLabelForwardKey(label_id, vid);
        if (!doPut(txn, i_key, {})) return false;

        // X| properties
        for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
            if (props[prop_id].has_value()) {
                auto x_key = KeyCodec::encodePropertyKey(label_id, vid, prop_id);
                auto x_val = ValueCodec::encode(props[prop_id].value());
                if (!doPut(txn, x_key, x_val)) return false;
            }
        }
    }

    // Primary key indexes
    if (pk_value) {
        auto pk_encoded = ValueCodec::encode(*pk_value);
        auto p_key = KeyCodec::encodePkForwardKey(pk_encoded);
        if (!doPut(txn, p_key, ValueCodec::encodeU64(vid))) return false;

        auto r_key = KeyCodec::encodePkReverseKey(vid);
        if (!doPut(txn, r_key, pk_encoded)) return false;
    }

    return true;
}

bool GraphStoreImpl::deleteVertex(GraphTxnHandle txn, VertexId vid) {
    // 1. Delete all labels (L|) and forward indexes (I|)
    auto labels = getVertexLabels(txn, vid);
    for (LabelId label_id : labels) {
        doDel(txn, KeyCodec::encodeLabelReverseKey(vid, label_id));
        doDel(txn, KeyCodec::encodeLabelForwardKey(label_id, vid));

        // Delete all X| properties under this label
        auto prefix = KeyCodec::encodePropertyPrefix(label_id, vid);
        doPrefixScan(txn, prefix, [&](std::string_view key, std::string_view) {
            doDel(txn, key);
            return true;
        });
    }

    // 2. Delete primary key indexes
    auto pk_encoded = doGet(txn, KeyCodec::encodePkReverseKey(vid));
    if (pk_encoded) {
        doDel(txn, KeyCodec::encodePkForwardKey(*pk_encoded));
        doDel(txn, KeyCodec::encodePkReverseKey(vid));
    }

    return true;
}

// ==================== Vertex Properties ====================

std::optional<Properties> GraphStoreImpl::getVertexProperties(
    GraphTxnHandle txn, VertexId vid, LabelId label_id) {

    auto prefix = KeyCodec::encodePropertyPrefix(label_id, vid);
    Properties props;
    bool found_any = false;

    doPrefixScan(txn, prefix, [&](std::string_view key, std::string_view value) {
        auto [_, __, prop_id] = KeyCodec::decodePropertyKey(key);
        if (prop_id >= props.size()) {
            props.resize(prop_id + 1);
        }
        props[prop_id] = ValueCodec::decode(value);
        found_any = true;
        return true;
    });

    if (!found_any) return std::nullopt;
    return props;
}

std::optional<PropertyValue> GraphStoreImpl::getVertexProperty(
    GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) {

    auto key = KeyCodec::encodePropertyKey(label_id, vid, prop_id);
    auto val = doGet(txn, key);
    if (!val) return std::nullopt;
    return ValueCodec::decode(*val);
}

bool GraphStoreImpl::putVertexProperty(
    GraphTxnHandle txn, VertexId vid, LabelId label_id,
    uint16_t prop_id, const PropertyValue& value) {

    auto key = KeyCodec::encodePropertyKey(label_id, vid, prop_id);
    return doPut(txn, key, ValueCodec::encode(value));
}

bool GraphStoreImpl::deleteVertexProperty(
    GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) {

    auto key = KeyCodec::encodePropertyKey(label_id, vid, prop_id);
    return doDel(txn, key);
}

bool GraphStoreImpl::putVertexProperties(
    GraphTxnHandle txn, VertexId vid, LabelId label_id,
    const Properties& props) {

    for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
        if (props[prop_id].has_value()) {
            auto key = KeyCodec::encodePropertyKey(label_id, vid, prop_id);
            auto val = ValueCodec::encode(props[prop_id].value());
            if (!doPut(txn, key, val)) return false;
        }
    }
    return true;
}

// ==================== Vertex Labels ====================

LabelIdSet GraphStoreImpl::getVertexLabels(GraphTxnHandle txn, VertexId vid) {
    LabelIdSet labels;
    auto prefix = KeyCodec::encodeLabelReversePrefix(vid);
    doPrefixScan(txn, prefix, [&](std::string_view key, std::string_view) {
        auto [_, label_id] = KeyCodec::decodeLabelReverseKey(key);
        labels.insert(label_id);
        return true;
    });
    return labels;
}

bool GraphStoreImpl::addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) {
    auto l_key = KeyCodec::encodeLabelReverseKey(vid, label_id);
    if (!doPut(txn, l_key, {})) return false;

    auto i_key = KeyCodec::encodeLabelForwardKey(label_id, vid);
    return doPut(txn, i_key, {});
}

bool GraphStoreImpl::removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) {
    doDel(txn, KeyCodec::encodeLabelReverseKey(vid, label_id));
    doDel(txn, KeyCodec::encodeLabelForwardKey(label_id, vid));

    // Delete all X| properties under this label
    auto prefix = KeyCodec::encodePropertyPrefix(label_id, vid);
    doPrefixScan(txn, prefix, [&](std::string_view key, std::string_view) {
        doDel(txn, key);
        return true;
    });

    return true;
}

// ==================== Primary Key ====================

std::optional<VertexId> GraphStoreImpl::getVertexIdByPk(const PropertyValue& pk_value) {
    auto pk_encoded = ValueCodec::encode(pk_value);
    auto key = KeyCodec::encodePkForwardKey(pk_encoded);
    auto val = engine_->get(key);
    if (!val) return std::nullopt;
    return ValueCodec::decodeU64(*val);
}

std::optional<PropertyValue> GraphStoreImpl::getPkByVertexId(VertexId vid) {
    auto key = KeyCodec::encodePkReverseKey(vid);
    auto val = engine_->get(key);
    if (!val) return std::nullopt;
    return ValueCodec::decode(*val);
}

// ==================== Label Index Scan ====================

void GraphStoreImpl::scanVerticesByLabel(
    GraphTxnHandle txn, LabelId label_id,
    const std::function<bool(VertexId)>& callback) {

    auto prefix = KeyCodec::encodeLabelForwardPrefix(label_id);
    doPrefixScan(txn, prefix, [&](std::string_view key, std::string_view) {
        auto [_, vertex_id] = KeyCodec::decodeLabelForwardKey(key);
        return callback(vertex_id);
    });
}

// ==================== Edge ====================

bool GraphStoreImpl::insertEdge(
    GraphTxnHandle txn, EdgeId eid,
    VertexId src_id, VertexId dst_id,
    EdgeLabelId label_id, uint64_t seq,
    const Properties& props) {

    auto edge_id_val = ValueCodec::encodeU64(eid);

    // E| OUT index: src -> dst
    KeyCodec::EdgeIndexKey out_key{src_id, Direction::OUT, label_id, dst_id, seq};
    if (!doPut(txn, KeyCodec::encodeEdgeIndexKey(out_key), edge_id_val)) return false;

    // E| IN index: dst -> src
    KeyCodec::EdgeIndexKey in_key{dst_id, Direction::IN, label_id, src_id, seq};
    if (!doPut(txn, KeyCodec::encodeEdgeIndexKey(in_key), edge_id_val)) return false;

    // D| edge properties
    for (uint16_t prop_id = 0; prop_id < props.size(); ++prop_id) {
        if (props[prop_id].has_value()) {
            auto d_key = KeyCodec::encodeEdgePropertyKey(label_id, eid, prop_id);
            auto d_val = ValueCodec::encode(props[prop_id].value());
            if (!doPut(txn, d_key, d_val)) return false;
        }
    }

    return true;
}

bool GraphStoreImpl::deleteEdge(
    GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id,
    VertexId src_id, VertexId dst_id, uint64_t seq) {

    // Delete E| entries (both directions)
    KeyCodec::EdgeIndexKey out_key{src_id, Direction::OUT, label_id, dst_id, seq};
    doDel(txn, KeyCodec::encodeEdgeIndexKey(out_key));

    KeyCodec::EdgeIndexKey in_key{dst_id, Direction::IN, label_id, src_id, seq};
    doDel(txn, KeyCodec::encodeEdgeIndexKey(in_key));

    // Delete D| properties
    auto prefix = KeyCodec::encodeEdgePropertyPrefix(label_id, eid);
    doPrefixScan(txn, prefix, [&](std::string_view key, std::string_view) {
        doDel(txn, key);
        return true;
    });

    return true;
}

// ==================== Edge Properties ====================

std::optional<Properties> GraphStoreImpl::getEdgeProperties(
    GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid) {

    auto prefix = KeyCodec::encodeEdgePropertyPrefix(label_id, eid);
    Properties props;
    bool found_any = false;

    doPrefixScan(txn, prefix, [&](std::string_view key, std::string_view value) {
        auto [_, __, prop_id] = KeyCodec::decodeEdgePropertyKey(key);
        if (prop_id >= props.size()) {
            props.resize(prop_id + 1);
        }
        props[prop_id] = ValueCodec::decode(value);
        found_any = true;
        return true;
    });

    if (!found_any) return std::nullopt;
    return props;
}

std::optional<PropertyValue> GraphStoreImpl::getEdgeProperty(
    GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) {

    auto key = KeyCodec::encodeEdgePropertyKey(label_id, eid, prop_id);
    auto val = doGet(txn, key);
    if (!val) return std::nullopt;
    return ValueCodec::decode(*val);
}

bool GraphStoreImpl::putEdgeProperty(
    GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid,
    uint16_t prop_id, const PropertyValue& value) {

    auto key = KeyCodec::encodeEdgePropertyKey(label_id, eid, prop_id);
    return doPut(txn, key, ValueCodec::encode(value));
}

bool GraphStoreImpl::deleteEdgeProperty(
    GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) {

    auto key = KeyCodec::encodeEdgePropertyKey(label_id, eid, prop_id);
    return doDel(txn, key);
}

// ==================== Edge Traversal ====================

void GraphStoreImpl::scanEdges(
    GraphTxnHandle txn, VertexId vid, Direction direction,
    std::optional<EdgeLabelId> label_filter,
    const std::function<bool(const EdgeIndexEntry&)>& callback) {

    std::string prefix;
    if (label_filter) {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction, *label_filter);
    } else {
        prefix = KeyCodec::encodeEdgeIndexPrefix(vid, direction);
    }

    doPrefixScan(txn, prefix, [&](std::string_view key, std::string_view value) {
        auto decoded = KeyCodec::decodeEdgeIndexKey(key);
        EdgeIndexEntry entry{
            decoded.neighbor_id,
            decoded.edge_label_id,
            decoded.seq,
            ValueCodec::decodeU64(value)
        };
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

uint64_t GraphStoreImpl::countDegree(
    GraphTxnHandle txn, VertexId vid, Direction direction,
    std::optional<EdgeLabelId> label_filter) {

    uint64_t count = 0;
    scanEdges(txn, vid, direction, label_filter, [&](const EdgeIndexEntry&) {
        ++count;
        return true;
    });
    return count;
}

} // namespace eugraph
