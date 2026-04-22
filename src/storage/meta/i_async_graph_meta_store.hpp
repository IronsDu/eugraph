#pragma once

#include "common/types/graph_types.hpp"
#include "storage/graph_schema.hpp"

#include <folly/coro/Task.h>

#include <optional>
#include <string>
#include <vector>

namespace eugraph {

class ISyncGraphMetaStore; // forward declare

/// Async graph metadata store interface.
/// Renamed from IMetadataService. Maintains GraphSchema in memory.
class IAsyncGraphMetaStore {
public:
    virtual ~IAsyncGraphMetaStore() = default;

    // Lifecycle
    virtual folly::coro::Task<bool> open(ISyncGraphMetaStore& store) = 0;
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

    // Schema access
    virtual const GraphSchema& schema() const = 0;
};

} // namespace eugraph
