#pragma once

#include "storage/graph_store.hpp"
#include "storage/kv/kv_engine.hpp"

#include <memory>

namespace eugraph {

/// IGraphStore implementation backed by any IKVEngine.
/// Uses KeyCodec for key encoding and ValueCodec for property serialization.
class GraphStoreImpl : public IGraphStore {
public:
    explicit GraphStoreImpl(std::unique_ptr<IKVEngine> engine);
    ~GraphStoreImpl() override;

    GraphStoreImpl(const GraphStoreImpl&) = delete;
    GraphStoreImpl& operator=(const GraphStoreImpl&) = delete;

    // ==================== IGraphStore ====================

    bool open(const std::string& db_path) override;
    void close() override;
    bool isOpen() const override;

    GraphTxnHandle beginTransaction() override;
    bool commitTransaction(GraphTxnHandle txn) override;
    bool rollbackTransaction(GraphTxnHandle txn) override;

    bool insertVertex(GraphTxnHandle txn, VertexId vid, std::span<const std::pair<LabelId, Properties>> label_props,
                      const PropertyValue* pk_value) override;

    bool deleteVertex(GraphTxnHandle txn, VertexId vid) override;

    std::optional<Properties> getVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;

    std::optional<PropertyValue> getVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id,
                                                   uint16_t prop_id) override;

    bool putVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id,
                           const PropertyValue& value) override;

    bool putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id, const Properties& props) override;

    bool deleteVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) override;

    LabelIdSet getVertexLabels(GraphTxnHandle txn, VertexId vid) override;
    bool addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;
    bool removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;

    std::optional<VertexId> getVertexIdByPk(const PropertyValue& pk_value) override;
    std::optional<PropertyValue> getPkByVertexId(VertexId vid) override;

    void scanVerticesByLabel(GraphTxnHandle txn, LabelId label_id,
                             const std::function<bool(VertexId)>& callback) override;

    bool insertEdge(GraphTxnHandle txn, EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                    uint64_t seq, const Properties& props) override;

    bool deleteEdge(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                    uint64_t seq) override;

    std::optional<Properties> getEdgeProperties(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid) override;

    std::optional<PropertyValue> getEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid,
                                                 uint16_t prop_id) override;

    bool putEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                         const PropertyValue& value) override;

    bool deleteEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) override;

    void scanEdges(GraphTxnHandle txn, VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter,
                   const std::function<bool(const EdgeIndexEntry&)>& callback) override;

    void scanEdgesByType(GraphTxnHandle txn, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                         std::optional<VertexId> dst_filter,
                         const std::function<bool(const EdgeTypeIndexEntry&)>& callback) override;

    uint64_t countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) override;

    uint64_t countDegree(GraphTxnHandle txn, VertexId vid, Direction direction,
                         std::optional<EdgeLabelId> label_filter) override;

private:
    bool doPut(GraphTxnHandle txn, std::string_view key, std::string_view value);
    std::optional<std::string> doGet(GraphTxnHandle txn, std::string_view key);
    bool doDel(GraphTxnHandle txn, std::string_view key);
    void doPrefixScan(GraphTxnHandle txn, std::string_view prefix,
                      const std::function<bool(std::string_view, std::string_view)>& callback);

    std::unique_ptr<IKVEngine> engine_;
};

} // namespace eugraph
