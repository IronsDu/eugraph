#pragma once

#include "common/types/graph_types.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace eugraph {

/// Async graph data store interface.
/// Compute layer depends on this interface instead of sync types.
class IAsyncGraphDataStore {
public:
    virtual ~IAsyncGraphDataStore() = default;

    // ==================== Transaction ====================

    virtual folly::coro::Task<GraphTxnHandle> beginTran() = 0;
    virtual folly::coro::Task<bool> commitTran(GraphTxnHandle txn) = 0;
    virtual folly::coro::Task<bool> rollbackTran(GraphTxnHandle txn) = 0;
    virtual void setTransaction(GraphTxnHandle txn) = 0;

    // ==================== DDL ====================

    /// Create storage tables for a new label (async dispatch to IO pool).
    virtual folly::coro::Task<bool> createLabel(LabelId label_id) = 0;

    /// Create storage tables for a new edge label (async dispatch to IO pool).
    virtual folly::coro::Task<bool> createEdgeLabel(EdgeLabelId edge_label_id) = 0;

    // Vertex Properties
    virtual folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id) = 0;
    virtual folly::coro::Task<LabelIdSet> getVertexLabels(VertexId vid) = 0;

    // Vertex Scan
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByLabel(LabelId label_id) = 0;

    // Edge Scan
    virtual folly::coro::AsyncGenerator<std::vector<ISyncGraphDataStore::EdgeIndexEntry>>
    scanEdges(VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter) = 0;

    // Edge Type Scan
    virtual folly::coro::AsyncGenerator<std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry>>
    scanEdgesByType(EdgeLabelId label_id, std::optional<VertexId> src_filter, std::optional<VertexId> dst_filter) = 0;

    // Write Operations
    virtual folly::coro::Task<bool> insertVertex(VertexId vid,
                                                 std::span<const std::pair<LabelId, Properties>> label_props,
                                                 const PropertyValue* pk_value) = 0;
    virtual folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                                               uint64_t seq, const Properties& props) = 0;

    // Index Operations
    virtual folly::coro::Task<bool> createIndex(const std::string& table_name) = 0;
    virtual folly::coro::Task<bool> dropIndex(const std::string& table_name) = 0;
    virtual folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value,
                                                     uint64_t entity_id) = 0;
    virtual folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const PropertyValue& value,
                                                     uint64_t entity_id) = 0;

    // Index Scan
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndex(LabelId label_id, uint16_t prop_id,
                                                                                   const PropertyValue& value) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexRange(LabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                             const std::optional<PropertyValue>& end) = 0;

    // Batch write (single transaction, single dispatch)
    struct BatchVertexEntry {
        VertexId vid;
        Properties props;
        PropertyValue pk_value;
    };
    struct BatchEdgeEntry {
        EdgeId eid;
        VertexId src_id;
        VertexId dst_id;
        uint64_t seq;
        Properties props;
    };
    virtual folly::coro::Task<void> batchInsertVertices(LabelId label_id, std::vector<BatchVertexEntry> entries) = 0;
    virtual folly::coro::Task<void> batchInsertEdges(EdgeLabelId edge_label_id,
                                                     std::vector<BatchEdgeEntry> entries) = 0;
};

} // namespace eugraph
