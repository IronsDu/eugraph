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
    /// Projection-aware variant: fetch only specified property IDs. Default delegates to full fetch.
    virtual folly::coro::Task<std::optional<Properties>>
    getVertexProperties(VertexId vid, LabelId label_id, const std::vector<uint16_t>& /*projection*/) {
        return getVertexProperties(vid, label_id);
    }
    virtual folly::coro::Task<LabelIdSet> getVertexLabels(VertexId vid) = 0;

    // Edge Properties
    virtual folly::coro::Task<std::optional<Properties>> getEdgeProperties(EdgeLabelId label_id, EdgeId eid) = 0;
    /// Projection-aware variant: fetch only specified property IDs. Default delegates to full fetch.
    virtual folly::coro::Task<std::optional<Properties>>
    getEdgeProperties(EdgeLabelId label_id, EdgeId eid, const std::vector<uint16_t>& /*projection*/) {
        return getEdgeProperties(label_id, eid);
    }

    /// Batch vertex property fetch for multiple vertices (same label, same projection).
    /// Reduces N+1 call overhead in Expand operator.
    virtual folly::coro::Task<std::vector<std::optional<Properties>>>
    batchGetVertexProperties(const std::vector<VertexId>& vids, LabelId label_id,
                             const std::vector<uint16_t>& /*projection*/) {
        std::vector<std::optional<Properties>> results;
        results.reserve(vids.size());
        for (auto vid : vids) {
            results.push_back(co_await getVertexProperties(vid, label_id));
        }
        co_return results;
    }

    // Vertex Property Write
    virtual folly::coro::Task<bool> putVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id,
                                                      const PropertyValue& value) = 0;
    virtual folly::coro::Task<bool> putVertexProperties(VertexId vid, LabelId label_id, const Properties& props) = 0;
    virtual folly::coro::Task<bool> deleteVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id) = 0;
    virtual folly::coro::Task<bool> deleteEdgeProperty(EdgeId eid, EdgeLabelId label_id, uint16_t prop_id) = 0;

    // Vertex Label Write
    virtual folly::coro::Task<bool> addVertexLabel(VertexId vid, LabelId label_id) = 0;
    virtual folly::coro::Task<bool> removeVertexLabel(VertexId vid, LabelId label_id) = 0;

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
                                                 std::span<const std::pair<LabelId, Properties>> label_props) = 0;
    virtual folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                                               uint64_t seq, const Properties& props) = 0;
    virtual folly::coro::Task<bool> deleteVertex(VertexId vid) = 0;
    virtual folly::coro::Task<bool> deleteEdge(EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                                               uint64_t seq) = 0;

    // Index Operations
    virtual folly::coro::Task<bool> createIndex(const std::string& table_name) = 0;
    virtual folly::coro::Task<bool> dropIndex(const std::string& table_name) = 0;
    virtual folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value,
                                                     uint64_t entity_id) = 0;
    virtual folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                                     uint64_t entity_id) = 0;
    virtual folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value,
                                                     uint64_t entity_id, std::string payload) = 0;
    virtual folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                                     uint64_t entity_id, std::string payload) = 0;
    virtual folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const PropertyValue& value,
                                                     uint64_t entity_id) = 0;
    virtual folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                                     uint64_t entity_id) = 0;
    virtual folly::coro::Task<bool> checkUniqueConstraint(const std::string& table, const PropertyValue& value) = 0;
    virtual folly::coro::Task<bool> checkUniqueConstraint(const std::string& table,
                                                          const std::vector<PropertyValue>& values) = 0;

    // Index Scan
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndex(LabelId label_id, uint16_t prop_id,
                                                                                   const PropertyValue& value) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexComposite(LabelId label_id, const std::vector<uint16_t>& prop_ids,
                                 const std::vector<PropertyValue>& values) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexRange(LabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                             const std::optional<PropertyValue>& end) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexRangeComposite(LabelId label_id, const std::vector<uint16_t>& prop_ids,
                                      const std::optional<std::vector<PropertyValue>>& start,
                                      const std::optional<std::vector<PropertyValue>>& end) = 0;

    // Edge Index Scan
    virtual folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndex(EdgeLabelId label_id, uint16_t prop_id, const PropertyValue& value) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexComposite(EdgeLabelId label_id, const std::vector<uint16_t>& prop_ids,
                              const std::vector<PropertyValue>& values) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexRange(EdgeLabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                          const std::optional<PropertyValue>& end) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexRangeComposite(EdgeLabelId label_id, const std::vector<uint16_t>& prop_ids,
                                   const std::optional<std::vector<PropertyValue>>& start,
                                   const std::optional<std::vector<PropertyValue>>& end) = 0;

    // Batch write (single transaction, single dispatch)
    struct BatchVertexEntry {
        VertexId vid;
        Properties props;
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
