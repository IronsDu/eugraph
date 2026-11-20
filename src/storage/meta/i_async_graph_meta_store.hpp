#pragma once

#include "common/schema/graph_schema.hpp"
#include "common/schema/schema_version.hpp"
#include "common/types/graph_types.hpp"

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
    /// 创建标签（幂等）。pk_props 为**主键属性名**（有序，顺序 = 主键元组顺序）；
    /// 空 = 无主键。标签已存在时：属性与主键声明一致则返回既有 id，不一致则抛异常。
    virtual folly::coro::Task<LabelId> createLabel(const std::string& name,
                                                   const std::vector<PropertyDef>& properties = {},
                                                   const std::vector<std::string>& pk_props = {},
                                                   bool merge_properties = false) = 0;
    virtual folly::coro::Task<bool>
    addVertexLabelProperties(const std::string& name,
                             const std::vector<std::pair<std::string, PropertyType>>& prop_defs) = 0;

    /// Lightweight prop_id allocation for __anon__ label.
    /// Thread-safe: uses internal mutex + cache. First encounter of a new property name
    /// allocates a new prop_id and persists asynchronously. Subsequent calls return cached id.
    virtual folly::coro::Task<uint16_t> getOrCreateAnonPropId(const std::string& prop_name, PropertyType prop_type) = 0;
    virtual folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) = 0;
    virtual folly::coro::Task<std::vector<LabelDef>> listLabels() = 0;

    // EdgeLabel management
    virtual folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name,
                                                           const std::vector<PropertyDef>& properties = {}) = 0;
    virtual folly::coro::Task<bool>
    addEdgeLabelProperties(const std::string& name,
                           const std::vector<std::pair<std::string, PropertyType>>& prop_defs) = 0;
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
    /// 待清理的索引表记录（pending_drop / orphan）。启动时逐条重试删除。
    struct IndexCleanupRecord {
        std::string kind; ///< "pending_drop" | "orphan"
        uint32_t index_id = 0;
        std::vector<std::string> tables;
    };

    struct IndexInfo {
        uint32_t index_id = 0;
        std::string name;
        std::string label_name;
        std::vector<std::string> property_names;
        bool unique;
        bool is_edge;
        IndexState state;
    };

    virtual folly::coro::Task<bool> createVertexIndex(const std::string& name, const std::string& label_name,
                                                      const std::vector<std::string>& prop_names, bool unique) = 0;
    virtual folly::coro::Task<bool> createVertexIndexWithAccessors(const std::string& name,
                                                                   const std::string& label_name,
                                                                   const std::vector<IndexAccessorDef>& accessors,
                                                                   bool unique) = 0;
    virtual folly::coro::Task<bool> createEdgeIndex(const std::string& name, const std::string& edge_label_name,
                                                    const std::vector<std::string>& prop_names, bool unique) = 0;
    virtual folly::coro::Task<bool> updateIndexState(const std::string& name, IndexState new_state) = 0;
    virtual folly::coro::Task<bool> dropIndex(const std::string& name) = 0;
    virtual folly::coro::Task<std::vector<IndexInfo>> listIndexes() = 0;
    virtual folly::coro::Task<std::optional<IndexInfo>> getIndex(const std::string& name) = 0;

    // ==================== 清理记录（DROP/重建后遗留的表）====================
    virtual folly::coro::Task<bool> putCleanupRecord(const std::string& kind, uint32_t index_id,
                                                     const std::vector<std::string>& tables) = 0;
    virtual folly::coro::Task<std::vector<IndexCleanupRecord>> listCleanupRecords() = 0;
    virtual folly::coro::Task<bool> removeCleanupRecord(const std::string& kind, uint32_t index_id) = 0;

    // Schema access
    /// 当前 schema 版本的只读智能指针（O(1)，不拷贝）；事务 BEGIN 时取一次并持有。
    virtual std::shared_ptr<const GraphSchema> schemaPtr() const = 0;
    /// 原子返回 {published schema, generation, compat_version}：三者必须来自同一个发布点。
    virtual SchemaVersion schemaVersion() const = 0;
    /// 兼容旧调用：深拷贝当前版本（测试/一次性用途；热路径用 schemaPtr）。
    virtual GraphSchema schemaSnapshot() const = 0;

    // ==================== schema 版本（在线索引构建）====================
    /// 快照代数（每次定义发布递增）。
    virtual uint64_t schemaGeneration() const = 0;
    /// 兼容性版本（只在不兼容变更时递增）。
    virtual uint64_t compatVersion() const = 0;
};

} // namespace eugraph
