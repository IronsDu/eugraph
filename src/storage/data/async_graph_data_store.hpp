#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/data/graph_session.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"
#include "storage/data/index_maintenance.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/kv/value_codec.hpp"

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace eugraph {

/// Async wrapper around ISyncGraphDataStore.
/// All storage calls are dispatched to the IO thread pool via IoScheduler.
class AsyncGraphDataStore : public IAsyncGraphDataStore {
public:
    /// 构造即绑定语句会话 ✓（三个引用；每个语句构造一个，很轻量）
    /// DDL 由 sync store 在方法内部创建短生命周期 WtSession ✓。
    AsyncGraphDataStore(ISyncGraphDataStore& store, IoScheduler& io, GraphSession& session)
        : store_(store), io_(io), session_(session) {}

    /// 为某条语句构造一个轻量、绑定该会话的实例（返回值，不堆分配）✓
    [[nodiscard]] AsyncGraphDataStore for_session(GraphSession& session) const {
        return AsyncGraphDataStore(store_, io_, session);
    }

    // ==================== DDL ====================

    folly::coro::Task<bool> createLabel(LabelId label_id) override {
        auto ok = co_await io_.dispatch([this, label_id]() { return store_.createLabel(label_id); });
        co_return ok;
    }

    folly::coro::Task<bool> createEdgeLabel(EdgeLabelId edge_label_id) override {
        auto ok = co_await io_.dispatch([this, edge_label_id]() { return store_.createEdgeLabel(edge_label_id); });
        co_return ok;
    }

    // ==================== Vertex Properties ====================

    folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id) override {
        auto result = co_await io_.dispatch(
            [this, vid, label_id]() { return store_.getVertexProperties(session_, vid, label_id); });
        co_return std::move(result);
    }

    folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id,
                                                                     const std::vector<uint16_t>& projection) override {
        auto proj = projection; // copy to avoid dangling reference in IO-dispatched lambda
        auto result =
            co_await io_.dispatch([this, vid, label_id, proj = std::move(proj)]() -> std::optional<Properties> {
                Properties props;
                bool found_any = false;
                uint16_t max_id = 0;
                for (auto pid : proj) {
                    if (pid > max_id)
                        max_id = pid;
                }
                props.resize(max_id + 1);
                for (auto pid : proj) {
                    auto val = store_.getVertexProperty(session_, vid, label_id, pid);
                    if (val) {
                        props[pid] = std::move(*val);
                        found_any = true;
                    }
                }
                if (!found_any)
                    return std::nullopt;
                return props;
            });
        co_return std::move(result);
    }

    folly::coro::Task<LabelIdSet> getVertexLabels(VertexId vid) override {
        auto result = co_await io_.dispatch([this, vid]() { return store_.getVertexLabels(session_, vid); });
        co_return result;
    }

    folly::coro::Task<std::vector<LabelIdSet>> getVertexLabelsBatch(const std::vector<VertexId>& vids) override {
        auto ids = vids;
        auto result = co_await io_.dispatch(
            [this, ids = std::move(ids)]() { return store_.getVertexLabelsBatch(session_, ids); });
        co_return std::move(result);
    }

    folly::coro::Task<std::vector<std::optional<Properties>>>
    batchGetVertexProperties(const std::vector<VertexId>& vids, LabelId label_id,
                             const std::vector<uint16_t>& projection) override {
        auto ids = vids;
        auto proj = projection;
        auto result = co_await io_.dispatch([this, label_id, ids = std::move(ids), proj = std::move(proj)]() {
            if (proj.empty())
                return store_.getVertexPropertiesBatch(session_, label_id, ids);
            // One cursor covers the whole batch instead of one open/close per (row, property):
            // cursor open/close measured at 51.6% of a point lookup
            // (docs/storage/session-cursor-ownership.md, POC-1).
            return store_.getVertexPropertiesBatchProjected(session_, label_id, ids, proj);
        });
        co_return std::move(result);
    }

    folly::coro::Task<std::vector<std::vector<PropertyValue>>>
    batchGetVertexCoalesceProperties(const std::vector<VertexId>& vids,
                                     const std::vector<std::pair<LabelId, uint16_t>>& candidates) override {
        auto ids = vids;
        auto cands = candidates;
        auto result = co_await io_.dispatch([this, ids = std::move(ids), cands = std::move(cands)]() {
            return store_.getVertexCoalescePropertiesBatch(session_, ids, cands);
        });
        co_return std::move(result);
    }

    // ==================== Edge Properties ====================

    folly::coro::Task<std::optional<Properties>> getEdgeProperties(EdgeLabelId label_id, EdgeId eid) override {
        auto result = co_await io_.dispatch(
            [this, label_id, eid]() { return store_.getEdgeProperties(session_, label_id, eid); });
        co_return std::move(result);
    }

    folly::coro::Task<std::optional<Properties>> getEdgeProperties(EdgeLabelId label_id, EdgeId eid,
                                                                   const std::vector<uint16_t>& projection) override {
        auto proj = projection;
        auto result =
            co_await io_.dispatch([this, label_id, eid, proj = std::move(proj)]() -> std::optional<Properties> {
                Properties props;
                bool found_any = false;
                uint16_t max_id = 0;
                for (auto pid : proj) {
                    if (pid > max_id)
                        max_id = pid;
                }
                props.resize(max_id + 1);
                for (auto pid : proj) {
                    auto val = store_.getEdgeProperty(session_, label_id, eid, pid);
                    if (val) {
                        props[pid] = std::move(*val);
                        found_any = true;
                    }
                }
                if (!found_any)
                    return std::nullopt;
                return props;
            });
        co_return std::move(result);
    }

    folly::coro::Task<std::vector<std::optional<PropertyValue>>>
    getEdgePropertyBatch(EdgeLabelId label_id, const std::vector<EdgeId>& edge_ids, uint16_t prop_id) override {
        auto ids = edge_ids;
        auto result = co_await io_.dispatch([this, label_id, ids = std::move(ids), prop_id]() {
            return store_.getEdgePropertyBatch(session_, label_id, ids, prop_id);
        });
        co_return std::move(result);
    }

    // ==================== Vertex Scan ====================

    folly::coro::AsyncGenerator<std::span<const VertexId>> scanVerticesByLabel(LabelId label_id,
                                                                               std::vector<VertexId>& out) override {
        return scanCursorStream<VertexId>(
            out, [this, label_id]() { return store_.createVertexScanCursor(session_, label_id); },
            [](ISyncGraphDataStore::IVertexScanCursor& cursor, std::vector<VertexId>& batch) {
                constexpr size_t BATCH = 1024;
                batch.reserve(BATCH);
                for (size_t i = 0; i < BATCH && cursor.valid(); ++i) {
                    batch.push_back(cursor.vertexId());
                    cursor.next();
                }
            });
    }

    folly::coro::AsyncGenerator<std::span<const VertexId>> scanAllVertices(std::vector<VertexId>& out) override {
        // 与 scanVerticesByLabel 同样的游标式流：vid 升序、每点一次，内存只有一个批。
        return scanCursorStream<VertexId>(
            out, [this]() { return store_.createAllVertexScanCursor(session_); },
            [](ISyncGraphDataStore::IVertexScanCursor& cursor, std::vector<VertexId>& batch) {
                constexpr size_t BATCH = 1024;
                batch.reserve(BATCH);
                for (size_t i = 0; i < BATCH && cursor.valid(); ++i) {
                    batch.push_back(cursor.vertexId());
                    cursor.next();
                }
            });
    }

    // ==================== Edge Scan ====================

    folly::coro::AsyncGenerator<std::span<const ISyncGraphDataStore::EdgeIndexEntry>>
    scanEdges(VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter,
              std::vector<ISyncGraphDataStore::EdgeIndexEntry>& out) override {
        return scanCursorStream<ISyncGraphDataStore::EdgeIndexEntry>(
            out,
            [this, vid, direction, label_filter]() {
                return store_.createEdgeScanCursor(session_, vid, direction, label_filter);
            },
            [](ISyncGraphDataStore::IEdgeScanCursor& cursor, std::vector<ISyncGraphDataStore::EdgeIndexEntry>& batch) {
                constexpr size_t BATCH = 4096;
                // Most vertices have very few edges; do not reserve 65k entries per source.
                batch.reserve(64);
                for (size_t i = 0; i < BATCH && cursor.valid(); ++i) {
                    batch.push_back(cursor.entry());
                    cursor.next();
                }
            });
    }

    folly::coro::AsyncGenerator<std::vector<BatchedEdge>>
    scanEdgesBatch(const std::vector<VertexId>& src_ids, Direction direction,
                   std::optional<EdgeLabelId> label_filter) override {
        // One sync cursor per chunk: createEdgeScanCursor per source was the
        // dominant fixed cost on LDBC c5 (98k cursors).
        constexpr size_t kChunkVids = 4096;
        auto vids = src_ids;
        auto dir = direction;
        auto filter = label_filter;
        size_t offset = 0;
        while (offset < vids.size()) {
            const size_t n = std::min(kChunkVids, vids.size() - offset);
            std::vector<VertexId> chunk(vids.begin() + static_cast<std::ptrdiff_t>(offset),
                                        vids.begin() + static_cast<std::ptrdiff_t>(offset + n));
            std::vector<BatchedEdge> batch;
            // Cap the upfront allocation; batches with high-degree sources grow on demand.
            batch.reserve(std::min<size_t>(n, 256));
            co_await io_.dispatchVoid([this, &chunk, dir, filter, &batch]() {
                store_.scanEdgesBatch(session_, chunk, dir, filter,
                                      [&batch](VertexId vid, const ISyncGraphDataStore::EdgeIndexEntry& entry) {
                                          batch.push_back(BatchedEdge{vid, entry});
                                          return true;
                                      });
            });
            offset += n;
            if (!batch.empty())
                co_yield std::move(batch);
        }
    }

    // ==================== Edge Type Scan ====================

    folly::coro::AsyncGenerator<std::span<const ISyncGraphDataStore::EdgeTypeIndexEntry>>
    scanEdgesByType(EdgeLabelId label_id, std::optional<VertexId> src_filter, std::optional<VertexId> dst_filter,
                    std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry>& out) override {
        return scanCursorStream<ISyncGraphDataStore::EdgeTypeIndexEntry>(
            out,
            [this, label_id, src_filter, dst_filter]() {
                return store_.createEdgeTypeScanCursor(session_, label_id, src_filter, dst_filter);
            },
            [](ISyncGraphDataStore::IEdgeTypeScanCursor& cursor,
               std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry>& batch) {
                constexpr size_t BATCH = 1024;
                batch.reserve(BATCH);
                for (size_t i = 0; i < BATCH && cursor.valid(); ++i) {
                    batch.push_back(cursor.entry());
                    cursor.next();
                }
            });
    }

    // ==================== Vertex Property Write ====================

    folly::coro::Task<bool> putVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id,
                                              const PropertyValue& value) override {
        auto val = value;
        auto ok = co_await io_.dispatch([this, vid, label_id, prop_id, val = std::move(val)]() {
            return store_.putVertexProperty(session_, vid, label_id, prop_id, val);
        });
        co_return ok;
    }

    folly::coro::Task<bool> putVertexProperties(VertexId vid, LabelId label_id, const Properties& props) override {
        auto p = props;
        auto ok = co_await io_.dispatch([this, vid, label_id, p = std::move(p)]() {
            return store_.putVertexProperties(session_, vid, label_id, p);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id) override {
        auto ok = co_await io_.dispatch(
            [this, vid, label_id, prop_id]() { return store_.deleteVertexProperty(session_, vid, label_id, prop_id); });
        co_return ok;
    }

    // ==================== Vertex Label Write ====================

    folly::coro::Task<bool> addVertexLabel(VertexId vid, LabelId label_id) override {
        auto ok =
            co_await io_.dispatch([this, vid, label_id]() { return store_.addVertexLabel(session_, vid, label_id); });
        co_return ok;
    }

    folly::coro::Task<bool> removeVertexLabel(VertexId vid, LabelId label_id) override {
        auto ok = co_await io_.dispatch(
            [this, vid, label_id]() { return store_.removeVertexLabel(session_, vid, label_id); });
        co_return ok;
    }

    // ==================== Write Operations ====================

    folly::coro::Task<bool> insertVertex(VertexId vid,
                                         std::span<const std::pair<LabelId, Properties>> label_props) override {
        auto result = co_await io_.dispatch(
            [this, vid, label_props]() -> bool { return store_.insertVertex(session_, vid, label_props); });
        co_return result;
    }

    folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id, uint64_t seq,
                                       const Properties& props) override {
        auto p = props;
        auto result = co_await io_.dispatch([this, eid, src_id, dst_id, label_id, seq, p = std::move(p)]() -> bool {
            return store_.insertEdge(session_, eid, src_id, dst_id, label_id, seq, p);
        });
        co_return result;
    }

    // ==================== Index Operations ====================

    folly::coro::Task<bool> createIndex(const std::string& table_name) override {
        auto name = table_name;
        auto ok = co_await io_.dispatch([this, name = std::move(name)]() { return store_.createIndex(name); });
        co_return ok;
    }

    folly::coro::Task<bool> dropIndex(const std::string& table_name) override {
        auto name = table_name;
        auto ok = co_await io_.dispatch([this, name = std::move(name)]() { return store_.dropIndex(name); });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value,
                                             uint64_t entity_id) override {
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch([this, t = std::move(t), val = std::move(val), entity_id]() {
            return store_.insertIndexEntry(session_, t, val, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id) override {
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, t = std::move(t), vals = std::move(vals), entity_id]() {
            return store_.insertIndexEntry(session_, t, vals, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id,
                                             std::string payload) override {
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch(
            [this, t = std::move(t), val = std::move(val), entity_id, payload = std::move(payload)]() {
                return store_.insertIndexEntry(session_, t, val, entity_id, payload);
            });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id, std::string payload) override {
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch(
            [this, t = std::move(t), vals = std::move(vals), entity_id, payload = std::move(payload)]() {
                return store_.insertIndexEntry(session_, t, vals, entity_id, payload);
            });
        co_return ok;
    }

    folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const PropertyValue& value,
                                             uint64_t entity_id) override {
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch([this, t = std::move(t), val = std::move(val), entity_id]() {
            return store_.deleteIndexEntry(session_, t, val, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id) override {
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, t = std::move(t), vals = std::move(vals), entity_id]() {
            return store_.deleteIndexEntry(session_, t, vals, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> checkUniqueConstraint(const std::string& table, const PropertyValue& value) override {
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch([this, t = std::move(t), val = std::move(val)]() {
            return store_.checkUniqueConstraint(session_, t, val);
        });
        co_return ok;
    }

    folly::coro::Task<bool> checkUniqueConstraint(const std::string& table,
                                                  const std::vector<PropertyValue>& values) override {
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, t = std::move(t), vals = std::move(vals)]() {
            return store_.checkUniqueConstraint(session_, t, vals);
        });
        co_return ok;
    }

    // ==================== Index Scan ====================

    folly::coro::AsyncGenerator<std::span<const VertexId>> scanVerticesByIndex(LabelId label_id, uint16_t prop_id,
                                                                               const PropertyValue& value,
                                                                               std::vector<VertexId>& out) override {
        std::string table = vidxTable(label_id, prop_id);
        auto val = value;
        return scanIndexCursor<VertexId>(
            out, [this, table, val]() { return store_.createIndexEqualityCursor(session_, table, val); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) { return static_cast<VertexId>(c.entityId()); });
    }

    folly::coro::AsyncGenerator<std::span<const VertexId>>
    scanVerticesByIndexComposite(LabelId label_id, const std::vector<uint16_t>& prop_ids,
                                 const std::vector<PropertyValue>& values, std::vector<VertexId>& out) override {
        std::string table = vidxCompositeTable(label_id, prop_ids);
        auto vals = values;
        return scanIndexCursor<VertexId>(
            out, [this, table, vals]() { return store_.createIndexEqualityCursor(session_, table, vals); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) { return static_cast<VertexId>(c.entityId()); });
    }

    folly::coro::AsyncGenerator<std::span<const VertexId>>
    scanVerticesByIndexRange(LabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                             const std::optional<PropertyValue>& end, std::vector<VertexId>& out) override {
        std::string table = vidxTable(label_id, prop_id);
        auto s = start;
        auto e = end;
        return scanIndexCursor<VertexId>(
            out, [this, table, s, e]() { return store_.createIndexRangeCursor(session_, table, s, e); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) { return static_cast<VertexId>(c.entityId()); });
    }

    folly::coro::AsyncGenerator<std::span<const VertexId>> scanVerticesByIndexRangeComposite(
        LabelId label_id, const std::vector<uint16_t>& prop_ids, const std::optional<std::vector<PropertyValue>>& start,
        const std::optional<std::vector<PropertyValue>>& end, std::vector<VertexId>& out) override {
        std::string table = vidxCompositeTable(label_id, prop_ids);
        auto s = start;
        auto e = end;
        return scanIndexCursor<VertexId>(
            out, [this, table, s, e]() { return store_.createIndexRangeCursor(session_, table, s, e); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) { return static_cast<VertexId>(c.entityId()); });
    }

    folly::coro::AsyncGenerator<std::span<const VertexId>>
    scanVerticesByIndexId(uint32_t index_id, const std::vector<PropertyValue>& values,
                          std::vector<VertexId>& out) override {
        std::string table = vidxTableById(index_id);
        auto vals = values;
        return scanIndexCursor<VertexId>(
            out, [this, table, vals]() { return store_.createIndexEqualityCursor(session_, table, vals); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) { return static_cast<VertexId>(c.entityId()); });
    }

    folly::coro::Task<std::optional<VertexId>>
    lookupVertexByPrimaryKey(uint32_t index_id, const std::vector<PropertyValue>& values) override {
        std::string table = vidxTableById(index_id);
        auto vals = values;
        // 同一 sortable value 下 index key 按 entity_id 大端排序 ⇒ 第一条就是最小 vid ✓
        // （与「重复主键 first-wins」语义一致；不必扫完全部重复项）
        auto best = co_await io_.dispatch([this, &table, &vals]() -> std::optional<VertexId> {
            auto cursor = store_.createIndexEqualityCursor(session_, table, vals);
            if (!cursor || !cursor->valid())
                return std::nullopt;
            return static_cast<VertexId>(cursor->entityId());
        });
        co_return best;
    }

    folly::coro::AsyncGenerator<std::span<const VertexId>>
    scanVerticesByIndexIdRange(uint32_t index_id, const std::optional<std::vector<PropertyValue>>& start,
                               const std::optional<std::vector<PropertyValue>>& end,
                               std::vector<VertexId>& out) override {
        std::string table = vidxTableById(index_id);
        auto s = start;
        auto e = end;
        return scanIndexCursor<VertexId>(
            out, [this, table, s, e]() { return store_.createIndexRangeCursor(session_, table, s, e); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) { return static_cast<VertexId>(c.entityId()); });
    }

    // ==================== Edge Index Scan ====================

    folly::coro::AsyncGenerator<std::span<const EdgeIndexScanEntry>>
    scanEdgesByIndex(EdgeLabelId label_id, uint16_t prop_id, const PropertyValue& value,
                     std::vector<EdgeIndexScanEntry>& out) override {
        std::string table = eidxTable(label_id, prop_id);
        auto val = value;
        return scanIndexCursor<EdgeIndexScanEntry>(
            out, [this, table, val]() { return store_.createIndexEqualityCursor(session_, table, val); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) {
                EdgeIndexScanEntry entry;
                entry.edge_id = c.entityId();
                ValueCodec::decodeEdgeAdjacency(c.value(), entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                return entry;
            });
    }

    folly::coro::AsyncGenerator<std::span<const EdgeIndexScanEntry>>
    scanEdgesByIndexComposite(EdgeLabelId label_id, const std::vector<uint16_t>& prop_ids,
                              const std::vector<PropertyValue>& values, std::vector<EdgeIndexScanEntry>& out) override {
        std::string table = eidxCompositeTable(label_id, prop_ids);
        auto vals = values;
        return scanIndexCursor<EdgeIndexScanEntry>(
            out, [this, table, vals]() { return store_.createIndexEqualityCursor(session_, table, vals); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) {
                EdgeIndexScanEntry entry;
                entry.edge_id = c.entityId();
                ValueCodec::decodeEdgeAdjacency(c.value(), entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                return entry;
            });
    }

    folly::coro::AsyncGenerator<std::span<const EdgeIndexScanEntry>>
    scanEdgesByIndexRange(EdgeLabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                          const std::optional<PropertyValue>& end, std::vector<EdgeIndexScanEntry>& out) override {
        std::string table = eidxTable(label_id, prop_id);
        auto s = start;
        auto e = end;
        return scanIndexCursor<EdgeIndexScanEntry>(
            out, [this, table, s, e]() { return store_.createIndexRangeCursor(session_, table, s, e); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) {
                EdgeIndexScanEntry entry;
                entry.edge_id = c.entityId();
                ValueCodec::decodeEdgeAdjacency(c.value(), entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                return entry;
            });
    }

    folly::coro::AsyncGenerator<std::span<const EdgeIndexScanEntry>>
    scanEdgesByIndexRangeComposite(EdgeLabelId label_id, const std::vector<uint16_t>& prop_ids,
                                   const std::optional<std::vector<PropertyValue>>& start,
                                   const std::optional<std::vector<PropertyValue>>& end,
                                   std::vector<EdgeIndexScanEntry>& out) override {
        std::string table = eidxCompositeTable(label_id, prop_ids);
        auto s = start;
        auto e = end;
        return scanIndexCursor<EdgeIndexScanEntry>(
            out, [this, table, s, e]() { return store_.createIndexRangeCursor(session_, table, s, e); },
            [](const ISyncGraphDataStore::IIndexScanCursor& c) {
                EdgeIndexScanEntry entry;
                entry.edge_id = c.entityId();
                ValueCodec::decodeEdgeAdjacency(c.value(), entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                return entry;
            });
    }

    // ==================== Delete Operations ====================

    folly::coro::Task<bool> deleteVertex(VertexId vid) override {
        auto ok = co_await io_.dispatch([this, vid]() { return store_.deleteVertex(session_, vid); });
        co_return ok;
    }

    folly::coro::Task<bool> deleteEdge(EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                                       uint64_t seq) override {
        auto ok = co_await io_.dispatch([this, eid, label_id, src_id, dst_id, seq]() {
            return store_.deleteEdge(session_, eid, label_id, src_id, dst_id, seq);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteEdgeProperty(EdgeId eid, EdgeLabelId label_id, uint16_t prop_id) override {
        auto ok = co_await io_.dispatch(
            [this, eid, label_id, prop_id]() { return store_.deleteEdgeProperty(session_, label_id, eid, prop_id); });
        co_return ok;
    }

    folly::coro::Task<bool> putEdgeProperty(EdgeId eid, EdgeLabelId label_id, uint16_t prop_id,
                                            const PropertyValue& value) override {
        auto val = value;
        auto ok = co_await io_.dispatch([this, eid, label_id, prop_id, val = std::move(val)]() {
            return store_.putEdgeProperty(session_, label_id, eid, prop_id, val);
        });
        co_return ok;
    }

    // ==================== Batch Write ====================

    folly::coro::Task<void> batchInsertVertices(std::vector<BatchVertexEntry> entries,
                                                const std::unordered_map<LabelId, LabelDef>& label_defs) override {
        co_await io_.dispatchVoid([this, entries = std::move(entries), &label_defs]() {
            GraphSession& session = session_;
            for (const auto& e : entries) {
                store_.insertVertex(
                    session, e.vid,
                    std::span<const std::pair<LabelId, Properties>>{e.label_props.data(), e.label_props.size()});
                // 顶点数据与索引条目同事务，避免「顶点已提交、索引没跟上」
                auto index_entries = collectVertexIndexEntriesFromLabelProps(label_defs, e.label_props, e.vid);
                insertVertexIndexEntries(store_, session, index_entries);
            }
        });
    }

    folly::coro::Task<void>
    batchInsertEdges(EdgeLabelId edge_label_id, std::vector<BatchEdgeEntry> entries,
                     const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs) override {
        co_await io_.dispatchVoid([this, edge_label_id, entries = std::move(entries), &edge_label_defs]() {
            GraphSession& session = session_;
            for (const auto& e : entries) {
                store_.insertEdge(session, e.eid, e.src_id, e.dst_id, edge_label_id, e.seq, e.props);
                auto index_entries = collectEdgeIndexEntries(edge_label_defs, edge_label_id, e.eid, e.props);
                insertEdgeIndexEntries(store_, session, index_entries);
            }
        });
    }

private:
    /// 游标式扫描的统一驱动：第一次 dispatch 同时**创建 cursor 并填充首批**，
    /// 后续每批再 dispatch；扫描结果填进调用方传入的 `out`，batch 之间复用
    /// 同一 vector（不每批构造临时 vector），yield 的 span 只在本次 batch 内有效。
    template <typename Entry, typename MakeCursor, typename Fetch>
    folly::coro::AsyncGenerator<std::span<const Entry>> scanCursorStream(std::vector<Entry>& out,
                                                                         MakeCursor make_cursor, Fetch fetch) {
        auto cursor = co_await io_.dispatch([&]() {
            auto created = make_cursor();
            out.clear();
            if (created)
                fetch(*created, out);
            return created;
        });
        if (!cursor || out.empty())
            co_return;
        co_yield std::span<const Entry>(out);
        if (!cursor->valid())
            co_return;

        while (true) {
            const bool has_more = co_await io_.dispatch([&]() {
                out.clear();
                fetch(*cursor, out);
                return !out.empty();
            });
            if (!has_more)
                co_return;
            co_yield std::span<const Entry>(out);
            if (!cursor->valid())
                co_return;
        }
    }

    /// 索引扫描的统一流式驱动：cursor 在 IO 线程创建并填充首批，
    /// 每批只拉 BATCH 条 ⇒ 内存 O(batch) ✓。
    template <typename Entry, typename MakeCursor, typename Convert>
    folly::coro::AsyncGenerator<std::span<const Entry>> scanIndexCursor(std::vector<Entry>& out, MakeCursor make_cursor,
                                                                        Convert convert) {
        return scanCursorStream<Entry>(out, std::move(make_cursor),
                                       [convert = std::move(convert)](auto& cursor, std::vector<Entry>& batch) {
                                           constexpr size_t BATCH = 1024;
                                           batch.reserve(BATCH);
                                           for (size_t i = 0; i < BATCH && cursor.valid(); ++i) {
                                               batch.push_back(convert(cursor));
                                               cursor.next();
                                           }
                                       });
    }

    ISyncGraphDataStore& store_;
    IoScheduler& io_;
    GraphSession& session_;
};

} // namespace eugraph
