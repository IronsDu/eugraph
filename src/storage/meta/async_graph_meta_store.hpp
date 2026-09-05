#pragma once

#include "storage/graph_schema.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/Mutex.h>

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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
    folly::coro::Task<bool> open(ISyncGraphMetaStore& store, IoScheduler& io) override;
    folly::coro::Task<void> close() override;

    // Label management
    folly::coro::Task<LabelId> createLabel(const std::string& name,
                                           const std::vector<PropertyDef>& properties = {}) override;
    folly::coro::Task<bool>
    addVertexLabelProperties(const std::string& name,
                             const std::vector<std::pair<std::string, PropertyType>>& prop_defs) override;
    folly::coro::Task<uint16_t> getOrCreateAnonPropId(const std::string& prop_name, PropertyType prop_type) override;
    folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) override;
    folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) override;
    folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) override;
    folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) override;
    folly::coro::Task<std::vector<LabelDef>> listLabels() override;

    // EdgeLabel management
    folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name,
                                                   const std::vector<PropertyDef>& properties = {}) override;
    folly::coro::Task<bool>
    addEdgeLabelProperties(const std::string& name,
                           const std::vector<std::pair<std::string, PropertyType>>& prop_defs) override;
    folly::coro::Task<std::optional<EdgeLabelId>> getEdgeLabelId(const std::string& name) override;
    folly::coro::Task<std::optional<std::string>> getEdgeLabelName(EdgeLabelId id) override;
    folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDef(const std::string& name) override;
    folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) override;
    folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() override;

    // ID allocation
    folly::coro::Task<VertexId> nextVertexId() override;
    folly::coro::Task<EdgeId> nextEdgeId() override;
    folly::coro::Task<VertexId> nextVertexIdRange(uint64_t count) override;
    folly::coro::Task<EdgeId> nextEdgeIdRange(uint64_t count) override;

    // Index management
    folly::coro::Task<bool> createVertexIndex(const std::string& name, const std::string& label_name,
                                              const std::vector<std::string>& prop_names, bool unique) override;
    folly::coro::Task<bool> createEdgeIndex(const std::string& name, const std::string& edge_label_name,
                                            const std::vector<std::string>& prop_names, bool unique) override;
    folly::coro::Task<bool> updateIndexState(const std::string& name, IndexState new_state) override;
    folly::coro::Task<bool> dropIndex(const std::string& name) override;
    folly::coro::Task<std::vector<IAsyncGraphMetaStore::IndexInfo>> listIndexes() override;
    folly::coro::Task<std::optional<IAsyncGraphMetaStore::IndexInfo>> getIndex(const std::string& name) override;

    // Schema access
    const GraphSchema& schema() const override {
        return schema_;
    }

private:
    folly::coro::Task<void> saveNextIds();
    folly::coro::Task<void> refillVertexIdCache(uint64_t count);
    folly::coro::Task<void> refillEdgeIdCache(uint64_t count);

    std::optional<std::reference_wrapper<ISyncGraphMetaStore>> store_;
    std::optional<std::reference_wrapper<IoScheduler>> io_;
    GraphSchema schema_;

    // Fast-path in-memory ID caches. The persisted high-water mark in schema_ is
    // advanced by a chunk when a cache is empty, so normal batch imports do not
    // need to persist M|next_ids on every RPC batch.
    VertexId vertex_cache_next_ = 0;
    VertexId vertex_cache_end_ = 0;
    EdgeId edge_cache_next_ = 0;
    EdgeId edge_cache_end_ = 0;

    // Serializes cache refill + next-id persistence across concurrent import requests.
    std::mutex id_mu_;
    folly::coro::Mutex refill_mu_;

    // Lightweight __anon__ prop_id allocation
    std::mutex anon_prop_mu_;
    std::unordered_map<std::string, uint16_t> anon_prop_cache_;
};

} // namespace eugraph
