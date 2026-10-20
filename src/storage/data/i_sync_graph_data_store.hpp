#pragma once

#include "common/types/graph_types.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eugraph {

/// Synchronous graph data storage interface.
///
/// **纯读写**（docs/storage/session-cursor-pool-design.md §2）：每个数据操作都接收调用方的
/// `GraphSession&`，内部只用 `session.wt_session()` 与
/// `session.acquire_cursor(table)`（point）/ `acquire_scan_cursor(table)`（扫描）✓。
/// 本接口**不**建连接 ✗、**不**开/提交/回滚事务 ✗、**不**持有事务表 ✗。
class GraphSession;

class ISyncGraphDataStore {
public:
    virtual ~ISyncGraphDataStore() = default;

    // ==================== DDL ====================

    /// Create storage tables for a new label.
    virtual bool createLabel(LabelId label_id) = 0;

    /// Drop storage tables for a label.
    virtual bool dropLabel(LabelId label_id) = 0;

    /// Create storage tables for a new edge label.
    virtual bool createEdgeLabel(EdgeLabelId edge_label_id) = 0;

    /// Drop storage tables for an edge label.
    virtual bool dropEdgeLabel(EdgeLabelId edge_label_id) = 0;

    // ==================== Vertex ====================

    virtual bool insertVertex(GraphSession& session, VertexId vid,
                              std::span<const std::pair<LabelId, Properties>> label_props) = 0;
    virtual bool deleteVertex(GraphSession& session, VertexId vid) = 0;

    // ==================== Vertex Properties ====================

    virtual std::optional<Properties> getVertexProperties(GraphSession& session, VertexId vid, LabelId label_id) = 0;
    virtual std::optional<PropertyValue> getVertexProperty(GraphSession& session, VertexId vid, LabelId label_id,
                                                           uint16_t prop_id) = 0;
    virtual bool putVertexProperty(GraphSession& session, VertexId vid, LabelId label_id, uint16_t prop_id,
                                   const PropertyValue& value) = 0;
    virtual bool putVertexProperties(GraphSession& session, VertexId vid, LabelId label_id,
                                     const Properties& props) = 0;
    virtual bool deleteVertexProperty(GraphSession& session, VertexId vid, LabelId label_id, uint16_t prop_id) = 0;

    // ==================== Vertex Labels ====================

    virtual LabelIdSet getVertexLabels(GraphSession& session, VertexId vid) = 0;
    virtual std::vector<LabelIdSet> getVertexLabelsBatch(GraphSession& session, const std::vector<VertexId>& vids) = 0;
    virtual std::vector<std::optional<Properties>> getVertexPropertiesBatch(GraphSession& session, LabelId label_id,
                                                                            const std::vector<VertexId>& vids) = 0;
    /// Batch fetch of a SUBSET of properties. Default implementation falls back to one
    /// point lookup per (row, property); implementations may override it to read the whole
    /// batch with a single cursor. Semantics must match the fallback exactly: `props` is
    /// sized max(proj)+1, only the requested ids are filled, and a row with none of them
    /// present yields nullopt.
    virtual std::vector<std::optional<Properties>>
    getVertexPropertiesBatchProjected(GraphSession& session, LabelId label_id, const std::vector<VertexId>& vids,
                                      const std::vector<uint16_t>& proj) {
        std::vector<std::optional<Properties>> out;
        out.reserve(vids.size());
        for (VertexId vid : vids) {
            Properties props;
            props.resize(*std::max_element(proj.begin(), proj.end()) + 1);
            bool found = false;
            for (uint16_t pid : proj) {
                auto pv = getVertexProperty(session, vid, label_id, pid);
                if (pv) {
                    props[pid] = std::move(*pv);
                    found = true;
                }
            }
            out.emplace_back(found ? std::optional<Properties>{std::move(props)} : std::nullopt);
        }
        return out;
    }
    virtual bool addVertexLabel(GraphSession& session, VertexId vid, LabelId label_id) = 0;
    virtual bool removeVertexLabel(GraphSession& session, VertexId vid, LabelId label_id) = 0;

    // ==================== Edge Traversal Types ====================

    struct EdgeIndexEntry {
        VertexId neighbor_id;
        EdgeLabelId edge_label_id;
        uint64_t seq;
        EdgeId edge_id;
    };

    struct EdgeTypeIndexEntry {
        VertexId src_vertex_id;
        VertexId dst_vertex_id;
        uint64_t seq;
        EdgeId edge_id;
    };

    // ==================== Scan Cursors ====================

    class IVertexScanCursor {
    public:
        virtual ~IVertexScanCursor() = default;
        virtual bool valid() const = 0;
        virtual VertexId vertexId() const = 0;
        virtual void next() = 0;
    };

    class IEdgeScanCursor {
    public:
        virtual ~IEdgeScanCursor() = default;
        virtual bool valid() const = 0;
        virtual const EdgeIndexEntry& entry() const = 0;
        virtual void next() = 0;
    };

    class IEdgeTypeScanCursor {
    public:
        virtual ~IEdgeTypeScanCursor() = default;
        virtual bool valid() const = 0;
        virtual const EdgeTypeIndexEntry& entry() const = 0;
        virtual void next() = 0;
    };

    // ==================== Label Index Scan ====================

    virtual void scanVerticesByLabel(GraphSession& session, LabelId label_id,
                                     const std::function<bool(VertexId)>& callback) = 0;
    virtual std::unique_ptr<IVertexScanCursor> createVertexScanCursor(GraphSession& session, LabelId label_id) = 0;
    /// 全顶点游标（vid 升序、每个点一次）。scanAllVertices() 的回调式接口无法跨批次续扫，
    /// 需要流式输出的算子（AllNodeScan）用它，避免把全图 vid 先收进内存。
    virtual std::unique_ptr<IVertexScanCursor> createAllVertexScanCursor(GraphSession& session) = 0;

    virtual void scanAllVertices(GraphSession& session, const std::function<bool(VertexId)>& callback) = 0;

    // ==================== Edge ====================

    virtual bool insertEdge(GraphSession& session, EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                            uint64_t seq, const Properties& props) = 0;
    virtual bool deleteEdge(GraphSession& session, EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                            uint64_t seq) = 0;

    // ==================== Edge Properties ====================

    virtual std::optional<Properties> getEdgeProperties(GraphSession& session, EdgeLabelId label_id, EdgeId eid) = 0;
    virtual std::optional<PropertyValue> getEdgeProperty(GraphSession& session, EdgeLabelId label_id, EdgeId eid,
                                                         uint16_t prop_id) = 0;
    virtual std::vector<std::optional<PropertyValue>> getEdgePropertyBatch(GraphSession& session, EdgeLabelId label_id,
                                                                           const std::vector<EdgeId>& edge_ids,
                                                                           uint16_t prop_id) = 0;
    virtual bool putEdgeProperty(GraphSession& session, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                                 const PropertyValue& value) = 0;
    virtual bool deleteEdgeProperty(GraphSession& session, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) = 0;

    // ==================== Edge Traversal ====================

    virtual void scanEdges(GraphSession& session, VertexId vid, Direction direction,
                           std::optional<EdgeLabelId> label_filter,
                           const std::function<bool(const EdgeIndexEntry&)>& callback) = 0;
    virtual std::unique_ptr<IEdgeScanCursor> createEdgeScanCursor(GraphSession& session, VertexId vid,
                                                                  Direction direction,
                                                                  std::optional<EdgeLabelId> label_filter) = 0;
    virtual void scanEdgesByType(GraphSession& session, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                                 std::optional<VertexId> dst_filter,
                                 const std::function<bool(const EdgeTypeIndexEntry&)>& callback) = 0;
    virtual std::unique_ptr<IEdgeTypeScanCursor> createEdgeTypeScanCursor(GraphSession& session, EdgeLabelId label_id,
                                                                          std::optional<VertexId> src_filter,
                                                                          std::optional<VertexId> dst_filter) = 0;

    // ==================== Statistics ====================

    virtual uint64_t countVerticesByLabel(GraphSession& session, LabelId label_id) = 0;
    virtual uint64_t countDegree(GraphSession& session, VertexId vid, Direction direction,
                                 std::optional<EdgeLabelId> label_filter) = 0;

    // ==================== Index DDL ====================

    virtual bool createIndex(const std::string& table_name) = 0;
    virtual bool dropIndex(const std::string& table_name) = 0;

    // ==================== Index Entry Operations ====================

    // 索引条目写入/删除/唯一性检查都必须在**调用方的事务**里完成：
    // 否则条目与实体数据落在不同事务，批量导入时会出现「顶点已提交、索引没跟上」
    // 或者并发装载下唯一性预检读到旧快照（first-wins 语义失效）。
    virtual bool insertIndexEntry(GraphSession& session, const std::string& table, const PropertyValue& value,
                                  uint64_t entity_id) = 0;
    virtual bool insertIndexEntry(GraphSession& session, const std::string& table,
                                  const std::vector<PropertyValue>& values, uint64_t entity_id) = 0;
    virtual bool insertIndexEntry(GraphSession& session, const std::string& table, const PropertyValue& value,
                                  uint64_t entity_id, std::string_view payload) = 0;
    virtual bool insertIndexEntry(GraphSession& session, const std::string& table,
                                  const std::vector<PropertyValue>& values, uint64_t entity_id,
                                  std::string_view payload) = 0;
    virtual bool deleteIndexEntry(GraphSession& session, const std::string& table, const PropertyValue& value,
                                  uint64_t entity_id) = 0;
    virtual bool deleteIndexEntry(GraphSession& session, const std::string& table,
                                  const std::vector<PropertyValue>& values, uint64_t entity_id) = 0;
    virtual bool checkUniqueConstraint(GraphSession& session, const std::string& table, const PropertyValue& value) = 0;
    virtual bool checkUniqueConstraint(GraphSession& session, const std::string& table,
                                       const std::vector<PropertyValue>& values) = 0;

    // ==================== Index Scan ====================

    virtual void scanIndexEquality(GraphSession& session, const std::string& table, const PropertyValue& value,
                                   const std::function<bool(uint64_t)>& callback) = 0;
    virtual void scanIndexEquality(GraphSession& session, const std::string& table,
                                   const std::vector<PropertyValue>& values,
                                   const std::function<bool(uint64_t)>& callback) = 0;
    virtual void scanIndexRange(GraphSession& session, const std::string& table,
                                const std::optional<PropertyValue>& start, const std::optional<PropertyValue>& end,
                                const std::function<bool(uint64_t)>& callback) = 0;
    virtual void scanIndexRange(GraphSession& session, const std::string& table,
                                const std::optional<std::vector<PropertyValue>>& start,
                                const std::optional<std::vector<PropertyValue>>& end,
                                const std::function<bool(uint64_t)>& callback) = 0;

    // ==================== 可恢复的索引扫描 cursor ====================
    // 索引扫描可能命中大量条目；callback 式接口无法跨批次续扫 ⇒ async 层只能先物化全集 ✗。
    // cursor 式接口让 async 层每次只拉 BATCH 条，内存 O(batch) ✓。
    class IIndexScanCursor {
    public:
        virtual ~IIndexScanCursor() = default;
        virtual bool valid() const = 0;
        virtual uint64_t entityId() const = 0;
        /// 索引 value（边索引 = adjacency payload；顶点索引通常为空）✓
        virtual std::string_view value() const = 0;
        virtual void next() = 0;
    };

    virtual std::unique_ptr<IIndexScanCursor> createIndexEqualityCursor(GraphSession& session, const std::string& table,
                                                                        const PropertyValue& value) = 0;
    virtual std::unique_ptr<IIndexScanCursor> createIndexEqualityCursor(GraphSession& session, const std::string& table,
                                                                        const std::vector<PropertyValue>& values) = 0;
    /// range：start 为**排他**下界、end 为排他上界（与既有 scanIndexRange 语义一致）✓
    virtual std::unique_ptr<IIndexScanCursor> createIndexRangeCursor(GraphSession& session, const std::string& table,
                                                                     const std::optional<PropertyValue>& start,
                                                                     const std::optional<PropertyValue>& end) = 0;
    virtual std::unique_ptr<IIndexScanCursor>
    createIndexRangeCursor(GraphSession& session, const std::string& table,
                           const std::optional<std::vector<PropertyValue>>& start,
                           const std::optional<std::vector<PropertyValue>>& end) = 0;

    // Index scan that also returns the stored value (for edge indexes with adjacency info)
    virtual void scanIndexEqualityWithValue(GraphSession& session, const std::string& table, const PropertyValue& value,
                                            const std::function<bool(uint64_t, std::string_view)>& callback) = 0;
    virtual void scanIndexEqualityWithValue(GraphSession& session, const std::string& table,
                                            const std::vector<PropertyValue>& values,
                                            const std::function<bool(uint64_t, std::string_view)>& callback) = 0;
    virtual void scanIndexRangeWithValue(GraphSession& session, const std::string& table,
                                         const std::optional<PropertyValue>& start,
                                         const std::optional<PropertyValue>& end,
                                         const std::function<bool(uint64_t, std::string_view)>& callback) = 0;
    virtual void scanIndexRangeWithValue(GraphSession& session, const std::string& table,
                                         const std::optional<std::vector<PropertyValue>>& start,
                                         const std::optional<std::vector<PropertyValue>>& end,
                                         const std::function<bool(uint64_t, std::string_view)>& callback) = 0;

    // ==================== Index Cleanup ====================
};

} // namespace eugraph
