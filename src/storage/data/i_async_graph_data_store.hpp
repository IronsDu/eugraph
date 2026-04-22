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

    virtual void setTransaction(GraphTxnHandle txn) = 0;

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
};

} // namespace eugraph
