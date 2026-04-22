#pragma once

#include "storage/graph_schema.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <string>
#include <vector>

namespace eugraph {

/// Async graph metadata store implementation.
/// Renamed from MetadataServiceImpl. Uses GraphSchema instead of MetadataCache.
/// Does NOT call store->createLabel/createEdgeLabel — data table creation is the handler's responsibility.
class AsyncGraphMetaStore : public IAsyncGraphMetaStore {
public:
    AsyncGraphMetaStore() = default;
    ~AsyncGraphMetaStore() override = default;

    // Lifecycle
    folly::coro::Task<bool> open(ISyncGraphMetaStore& store) override;
    folly::coro::Task<void> close() override;

    // Label management
    folly::coro::Task<LabelId> createLabel(const std::string& name,
                                           const std::vector<PropertyDef>& properties = {}) override;
    folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) override;
    folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) override;
    folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) override;
    folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) override;
    folly::coro::Task<std::vector<LabelDef>> listLabels() override;

    // EdgeLabel management
    folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name,
                                                   const std::vector<PropertyDef>& properties = {}) override;
    folly::coro::Task<std::optional<EdgeLabelId>> getEdgeLabelId(const std::string& name) override;
    folly::coro::Task<std::optional<std::string>> getEdgeLabelName(EdgeLabelId id) override;
    folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDef(const std::string& name) override;
    folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) override;
    folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() override;

    // ID allocation
    folly::coro::Task<VertexId> nextVertexId() override;
    folly::coro::Task<EdgeId> nextEdgeId() override;

    // Schema access
    const GraphSchema& schema() const override {
        return schema_;
    }

private:
    void saveNextIds();

    ISyncGraphMetaStore* store_ = nullptr;
    GraphSchema schema_;
};

} // namespace eugraph
