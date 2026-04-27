#pragma once

#include "common/types/graph_types.hpp"
#include "storage/graph_schema.hpp"

#include <folly/coro/Task.h>

#include <optional>
#include <string>
#include <vector>

namespace eugraph {

class ISyncGraphMetaStore; // forward declare
class IoScheduler;

/// Async graph metadata store interface.
/// Renamed from IMetadataService. Maintains GraphSchema in memory.
class IAsyncGraphMetaStore {
public:
    virtual ~IAsyncGraphMetaStore() = default;

    // Lifecycle
    virtual folly::coro::Task<bool> open(ISyncGraphMetaStore& store, IoScheduler& io) = 0;
    virtual folly::coro::Task<void> close() = 0;

    // Label management
    virtual folly::coro::Task<LabelId> createLabel(const std::string& name,
                                                   const std::vector<PropertyDef>& properties = {}) = 0;
    virtual folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) = 0;
    virtual folly::coro::Task<std::vector<LabelDef>> listLabels() = 0;

    // EdgeLabel management
    virtual folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name,
                                                           const std::vector<PropertyDef>& properties = {}) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelId>> getEdgeLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getEdgeLabelName(EdgeLabelId id) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) = 0;
    virtual folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() = 0;

    // ID allocation
    virtual folly::coro::Task<VertexId> nextVertexId() = 0;
    virtual folly::coro::Task<EdgeId> nextEdgeId() = 0;
    virtual folly::coro::Task<VertexId> nextVertexIdRange(uint64_t count) = 0;
    virtual folly::coro::Task<EdgeId> nextEdgeIdRange(uint64_t count) = 0;

    // Index management
    struct IndexInfo {
        std::string name;
        std::string label_name;
        std::string property_name;
        bool unique;
        bool is_edge;
        IndexState state;
    };

    virtual folly::coro::Task<bool> createVertexIndex(const std::string& name, const std::string& label_name,
                                                      const std::string& prop_name, bool unique) = 0;
    virtual folly::coro::Task<bool> createEdgeIndex(const std::string& name, const std::string& edge_label_name,
                                                    const std::string& prop_name, bool unique) = 0;
    virtual folly::coro::Task<bool> updateIndexState(const std::string& name, IndexState new_state) = 0;
    virtual folly::coro::Task<bool> dropIndex(const std::string& name) = 0;
    virtual folly::coro::Task<std::vector<IndexInfo>> listIndexes() = 0;
    virtual folly::coro::Task<std::optional<IndexInfo>> getIndex(const std::string& name) = 0;

    // Schema access
    virtual const GraphSchema& schema() const = 0;
};

} // namespace eugraph
