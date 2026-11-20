#pragma once

#include "common/schema/graph_schema.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/Mutex.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
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
    folly::coro::Task<LabelId> createLabel(const std::string& name, const std::vector<PropertyDef>& properties = {},
                                           const std::vector<std::string>& pk_props = {},
                                           bool merge_properties = false) override;
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
    folly::coro::Task<bool> createVertexIndexWithAccessors(const std::string& name, const std::string& label_name,
                                                           const std::vector<IndexAccessorDef>& accessors,
                                                           bool unique) override;
    folly::coro::Task<bool> createEdgeIndex(const std::string& name, const std::string& edge_label_name,
                                            const std::vector<std::string>& prop_names, bool unique) override;
    folly::coro::Task<bool> updateIndexState(const std::string& name, IndexState new_state) override;
    folly::coro::Task<bool> dropIndex(const std::string& name) override;
    folly::coro::Task<std::vector<IAsyncGraphMetaStore::IndexInfo>> listIndexes() override;
    folly::coro::Task<std::optional<IAsyncGraphMetaStore::IndexInfo>> getIndex(const std::string& name) override;

    /// 一次性修复：给持久化时 index_id==0 的遗留索引补 id 并落盘（幂等）。
    /// 返回修复条数。只在图打开、任何查询之前调用。
    folly::coro::Task<size_t> repairMissingIndexIds();

    /// 启动恢复：把持久化为 WRITE_ONLY（构建中）的索引置 ERROR（保留定义），
    /// 并把它们的索引表/变更表登记为 orphan 记录（返回这些记录；调用方尝试删除）。
    /// 本期不做崩溃续建（无扫描水位）；调用点：图打开、任何查询之前。
    folly::coro::Task<std::vector<IndexCleanupRecord>> recoverUnfinishedIndexes();

    folly::coro::Task<bool> putCleanupRecord(const std::string& kind, uint32_t index_id,
                                             const std::vector<std::string>& tables) override;
    folly::coro::Task<std::vector<IndexCleanupRecord>> listCleanupRecords() override;
    folly::coro::Task<bool> removeCleanupRecord(const std::string& kind, uint32_t index_id) override;

    // ==================== schema 版本（在线索引构建）====================
    /// 快照代数：每次"定义发布"（标签/属性变化、索引定义增删、索引转 PUBLIC）递增。
    /// 用法：事务 BEGIN 时取 pin；索引的 public_generation ≤ pin 才允许被该事务的**新计划**选中。
    uint64_t schemaGeneration() const noexcept override {
        return schema_generation_.load(std::memory_order_acquire);
    }
    /// 兼容性版本：只在不兼容变更（删标签/属性、改类型、改主键）时递增。
    /// 当前没有破坏性 DDL（DDL 未开放）⇒ 恒 0；提交校验的快速路径用它。
    uint64_t compatVersion() const noexcept override {
        return compat_version_.load(std::memory_order_acquire);
    }
    void bumpSchemaGeneration() noexcept {
        schema_generation_.fetch_add(1, std::memory_order_acq_rel);
    }

    // Schema access
    std::shared_ptr<const GraphSchema> schemaPtr() const override {
        std::lock_guard<std::mutex> lock(published_mu_);
        if (published_schema_)
            return published_schema_;
        return std::make_shared<GraphSchema>(schema_); // open 之前/测试兜底
    }
    GraphSchema schemaSnapshot() const override {
        return *schemaPtr();
    }

    /// Planning heuristic: approximate vertex count per label (refreshed at
    /// graph open; later writes may make it stale). Never used for semantics.
    void setLabelVertexCount(LabelId id, uint64_t count) {
        std::unique_lock<std::shared_mutex> lock(schema_mu_);
        schema_.label_vertex_counts[id] = count;
        publishSchemaVersion();
    }

private:
    folly::coro::Task<void> saveNextIds();
    folly::coro::Task<void> refillVertexIdCache(uint64_t count);
    folly::coro::Task<void> refillEdgeIdCache(uint64_t count);

    std::optional<std::reference_wrapper<ISyncGraphMetaStore>> store_;
    std::optional<std::reference_wrapper<IoScheduler>> io_;
    GraphSchema schema_;
    /// 保护 schema_ 的读写（读用 shared，写用 unique；只在内存赋值/容器修改的短临界区持有）。
    mutable std::shared_mutex schema_mu_;
    /// 串行化 DDL 写者（协程友好，可跨 co_await 持有）。
    folly::coro::Mutex schema_write_mu_;
    /// 已发布的不可变版本：读者持有 shared_ptr，旧版本由引用计数释放。
    std::shared_ptr<const GraphSchema> published_schema_;
    mutable std::mutex published_mu_;
    /// 把当前 schema_ 发布成一个新版本（DDL 成功后调用）。
    void publishSchemaVersion();
    std::atomic<uint64_t> schema_generation_{0};
    std::atomic<uint64_t> compat_version_{0};

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
    folly::coro::Mutex anon_prop_mu_;
    std::unordered_map<std::string, uint16_t> anon_prop_cache_;
};

} // namespace eugraph
