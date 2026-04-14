#pragma once

#include "common/types/graph_types.hpp"
#include "storage/graph_store.hpp"

#include <folly/coro/Task.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {

/// Metadata service interface.
/// Manages Label/EdgeLabel definitions (name ↔ ID mapping, property definitions)
/// and global ID allocation. Coroutine-style, all operations via co_await.
class IMetadataService {
public:
    virtual ~IMetadataService() = default;

    // ==================== Lifecycle ====================

    virtual folly::coro::Task<bool> open(IGraphStore& store) = 0;
    virtual folly::coro::Task<void> close() = 0;

    // ==================== Label management ====================

    virtual folly::coro::Task<LabelId> createLabel(const std::string& name,
                                                   const std::vector<PropertyDef>& properties = {}) = 0;

    virtual folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) = 0;
    virtual folly::coro::Task<std::vector<LabelDef>> listLabels() = 0;

    // ==================== EdgeLabel management ====================

    virtual folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name,
                                                           const std::vector<PropertyDef>& properties = {}) = 0;

    virtual folly::coro::Task<std::optional<EdgeLabelId>> getEdgeLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getEdgeLabelName(EdgeLabelId id) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) = 0;
    virtual folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() = 0;

    // ==================== ID allocation ====================

    virtual folly::coro::Task<VertexId> nextVertexId() = 0;
    virtual folly::coro::Task<EdgeId> nextEdgeId() = 0;
};

// ==================== In-memory cache ====================

struct MetadataCache {
    std::unordered_map<std::string, LabelDef> label_by_name;
    std::unordered_map<LabelId, LabelDef> label_by_id;
    LabelId next_label_id = 1;

    std::unordered_map<std::string, EdgeLabelDef> edge_label_by_name;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_by_id;
    EdgeLabelId next_edge_label_id = 1;

    VertexId next_vertex_id = 1;
    EdgeId next_edge_id = 1;
};

// ==================== Local implementation ====================

/// MetadataServiceImpl: local implementation using IGraphStore for persistence.
/// Loads all metadata into memory cache on open; reads serve from cache,
/// writes update both cache and KV store.
class MetadataServiceImpl : public IMetadataService {
public:
    MetadataServiceImpl() = default;
    ~MetadataServiceImpl() override = default;

    folly::coro::Task<bool> open(IGraphStore& store) override;
    folly::coro::Task<void> close() override;

    folly::coro::Task<LabelId> createLabel(const std::string& name,
                                           const std::vector<PropertyDef>& properties = {}) override;
    folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) override;
    folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) override;
    folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) override;
    folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) override;
    folly::coro::Task<std::vector<LabelDef>> listLabels() override;

    folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name,
                                                   const std::vector<PropertyDef>& properties = {}) override;
    folly::coro::Task<std::optional<EdgeLabelId>> getEdgeLabelId(const std::string& name) override;
    folly::coro::Task<std::optional<std::string>> getEdgeLabelName(EdgeLabelId id) override;
    folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDef(const std::string& name) override;
    folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) override;
    folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() override;

    folly::coro::Task<VertexId> nextVertexId() override;
    folly::coro::Task<EdgeId> nextEdgeId() override;

private:
    // Persist ID counters to KV
    void saveNextIds();

    IGraphStore* store_ = nullptr;
    MetadataCache cache_;
};

} // namespace eugraph
