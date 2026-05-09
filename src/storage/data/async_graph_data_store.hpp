#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/kv/value_codec.hpp"

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>

#include <memory>
#include <optional>
#include <vector>

namespace eugraph {

/// Async wrapper around ISyncGraphDataStore.
/// All storage calls are dispatched to the IO thread pool via IoScheduler.
class AsyncGraphDataStore : public IAsyncGraphDataStore {
public:
    AsyncGraphDataStore(ISyncGraphDataStore& store, IoScheduler& io, GraphTxnHandle txn = INVALID_GRAPH_TXN)
        : store_(store), io_(io), txn_(txn) {}

    void setTransaction(GraphTxnHandle txn) override {
        txn_ = txn;
    }

    // ==================== Transaction ====================

    folly::coro::Task<GraphTxnHandle> beginTran() override {
        auto txn = co_await io_.dispatch([this]() { return store_.beginTransaction(); });
        co_return txn;
    }

    folly::coro::Task<bool> commitTran(GraphTxnHandle txn) override {
        auto ok = co_await io_.dispatch([this, txn]() { return store_.commitTransaction(txn); });
        co_return ok;
    }

    folly::coro::Task<bool> rollbackTran(GraphTxnHandle txn) override {
        auto ok = co_await io_.dispatch([this, txn]() { return store_.rollbackTransaction(txn); });
        co_return ok;
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
        auto txn = txn_;
        auto result = co_await io_.dispatch(
            [this, txn, vid, label_id]() { return store_.getVertexProperties(txn, vid, label_id); });
        co_return std::move(result);
    }

    folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id,
                                                                     const std::vector<uint16_t>& projection) override {
        auto txn = txn_;
        auto proj = projection; // copy to avoid dangling reference in IO-dispatched lambda
        auto result =
            co_await io_.dispatch([this, txn, vid, label_id, proj = std::move(proj)]() -> std::optional<Properties> {
                Properties props;
                bool found_any = false;
                uint16_t max_id = 0;
                for (auto pid : proj) {
                    if (pid > max_id)
                        max_id = pid;
                }
                props.resize(max_id + 1);
                for (auto pid : proj) {
                    auto val = store_.getVertexProperty(txn, vid, label_id, pid);
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
        auto txn = txn_;
        auto result = co_await io_.dispatch([this, txn, vid]() { return store_.getVertexLabels(txn, vid); });
        co_return result;
    }

    // ==================== Edge Properties ====================

    folly::coro::Task<std::optional<Properties>> getEdgeProperties(EdgeLabelId label_id, EdgeId eid) override {
        auto txn = txn_;
        auto result = co_await io_.dispatch(
            [this, txn, label_id, eid]() { return store_.getEdgeProperties(txn, label_id, eid); });
        co_return std::move(result);
    }

    folly::coro::Task<std::optional<Properties>> getEdgeProperties(EdgeLabelId label_id, EdgeId eid,
                                                                   const std::vector<uint16_t>& projection) override {
        auto txn = txn_;
        auto proj = projection;
        auto result =
            co_await io_.dispatch([this, txn, label_id, eid, proj = std::move(proj)]() -> std::optional<Properties> {
                Properties props;
                bool found_any = false;
                uint16_t max_id = 0;
                for (auto pid : proj) {
                    if (pid > max_id)
                        max_id = pid;
                }
                props.resize(max_id + 1);
                for (auto pid : proj) {
                    auto val = store_.getEdgeProperty(txn, label_id, eid, pid);
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

    // ==================== Vertex Scan ====================

    folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByLabel(LabelId label_id) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        auto cursor =
            co_await io_.dispatch([this, txn, label_id]() { return store_.createVertexScanCursor(txn, label_id); });
        if (!cursor)
            co_return;

        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([&]() {
                for (size_t i = 0; i < BATCH && cursor->valid(); ++i) {
                    batch.push_back(cursor->vertexId());
                    cursor->next();
                }
            });
            if (batch.empty()) {
                co_return;
            }
            co_yield std::move(batch);
        }
    }

    // ==================== Edge Scan ====================

    folly::coro::AsyncGenerator<std::vector<ISyncGraphDataStore::EdgeIndexEntry>>
    scanEdges(VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        auto cursor = co_await io_.dispatch([this, txn, vid, direction, label_filter]() {
            return store_.createEdgeScanCursor(txn, vid, direction, label_filter);
        });
        if (!cursor)
            co_return;

        while (true) {
            std::vector<ISyncGraphDataStore::EdgeIndexEntry> batch;
            co_await io_.dispatchVoid([&]() {
                for (size_t i = 0; i < BATCH && cursor->valid(); ++i) {
                    batch.push_back(cursor->entry());
                    cursor->next();
                }
            });
            if (batch.empty()) {
                co_return;
            }
            co_yield std::move(batch);
        }
    }

    // ==================== Edge Type Scan ====================

    folly::coro::AsyncGenerator<std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry>>
    scanEdgesByType(EdgeLabelId label_id, std::optional<VertexId> src_filter,
                    std::optional<VertexId> dst_filter) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        auto cursor = co_await io_.dispatch([this, txn, label_id, src_filter, dst_filter]() {
            return store_.createEdgeTypeScanCursor(txn, label_id, src_filter, dst_filter);
        });
        if (!cursor)
            co_return;

        while (true) {
            std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> batch;
            co_await io_.dispatchVoid([&]() {
                for (size_t i = 0; i < BATCH && cursor->valid(); ++i) {
                    batch.push_back(cursor->entry());
                    cursor->next();
                }
            });
            if (batch.empty()) {
                co_return;
            }
            co_yield std::move(batch);
        }
    }

    // ==================== Vertex Property Write ====================

    folly::coro::Task<bool> putVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id,
                                              const PropertyValue& value) override {
        auto txn = txn_;
        auto val = value;
        auto ok = co_await io_.dispatch([this, txn, vid, label_id, prop_id, val = std::move(val)]() {
            return store_.putVertexProperty(txn, vid, label_id, prop_id, val);
        });
        co_return ok;
    }

    folly::coro::Task<bool> putVertexProperties(VertexId vid, LabelId label_id, const Properties& props) override {
        auto txn = txn_;
        auto p = props;
        auto ok = co_await io_.dispatch([this, txn, vid, label_id, p = std::move(p)]() {
            return store_.putVertexProperties(txn, vid, label_id, p);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id) override {
        auto txn = txn_;
        auto ok = co_await io_.dispatch(
            [this, txn, vid, label_id, prop_id]() { return store_.deleteVertexProperty(txn, vid, label_id, prop_id); });
        co_return ok;
    }

    // ==================== Vertex Label Write ====================

    folly::coro::Task<bool> addVertexLabel(VertexId vid, LabelId label_id) override {
        auto txn = txn_;
        auto ok =
            co_await io_.dispatch([this, txn, vid, label_id]() { return store_.addVertexLabel(txn, vid, label_id); });
        co_return ok;
    }

    folly::coro::Task<bool> removeVertexLabel(VertexId vid, LabelId label_id) override {
        auto txn = txn_;
        auto ok = co_await io_.dispatch(
            [this, txn, vid, label_id]() { return store_.removeVertexLabel(txn, vid, label_id); });
        co_return ok;
    }

    // ==================== Write Operations ====================

    folly::coro::Task<bool> insertVertex(VertexId vid,
                                         std::span<const std::pair<LabelId, Properties>> label_props) override {
        auto txn = txn_;
        auto result = co_await io_.dispatch(
            [this, txn, vid, label_props]() -> bool { return store_.insertVertex(txn, vid, label_props); });
        co_return result;
    }

    folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id, uint64_t seq,
                                       const Properties& props) override {
        auto txn = txn_;
        auto p = props;
        auto result =
            co_await io_.dispatch([this, txn, eid, src_id, dst_id, label_id, seq, p = std::move(p)]() -> bool {
                return store_.insertEdge(txn, eid, src_id, dst_id, label_id, seq, p);
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
        auto txn = txn_;
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), val = std::move(val), entity_id]() {
            return store_.insertIndexEntry(t, val, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id) override {
        auto txn = txn_;
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), vals = std::move(vals), entity_id]() {
            return store_.insertIndexEntry(t, vals, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id,
                                             std::string payload) override {
        auto txn = txn_;
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch(
            [this, txn, t = std::move(t), val = std::move(val), entity_id, payload = std::move(payload)]() {
                return store_.insertIndexEntry(t, val, entity_id, payload);
            });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id, std::string payload) override {
        auto txn = txn_;
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch(
            [this, txn, t = std::move(t), vals = std::move(vals), entity_id, payload = std::move(payload)]() {
                return store_.insertIndexEntry(t, vals, entity_id, payload);
            });
        co_return ok;
    }

    folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const PropertyValue& value,
                                             uint64_t entity_id) override {
        auto txn = txn_;
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), val = std::move(val), entity_id]() {
            return store_.deleteIndexEntry(t, val, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id) override {
        auto txn = txn_;
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), vals = std::move(vals), entity_id]() {
            return store_.deleteIndexEntry(t, vals, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> checkUniqueConstraint(const std::string& table, const PropertyValue& value) override {
        auto txn = txn_;
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch(
            [this, txn, t = std::move(t), val = std::move(val)]() { return store_.checkUniqueConstraint(t, val); });
        co_return ok;
    }

    folly::coro::Task<bool> checkUniqueConstraint(const std::string& table,
                                                  const std::vector<PropertyValue>& values) override {
        auto txn = txn_;
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch(
            [this, txn, t = std::move(t), vals = std::move(vals)]() { return store_.checkUniqueConstraint(t, vals); });
        co_return ok;
    }

    // ==================== Index Scan ====================

    folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndex(LabelId label_id, uint16_t prop_id,
                                                                           const PropertyValue& value) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        std::string table = vidxTable(label_id, prop_id);
        auto val = value;
        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([this, txn, &table, &val, &batch]() {
                store_.scanIndexEquality(txn, table, val, [&](uint64_t entity_id) {
                    batch.push_back(entity_id);
                    return batch.size() < BATCH;
                });
            });
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
            if (batch.size() < BATCH)
                co_return;
        }
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexComposite(LabelId label_id, const std::vector<uint16_t>& prop_ids,
                                 const std::vector<PropertyValue>& values) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        std::string table = vidxCompositeTable(label_id, prop_ids);
        auto vals = values;
        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([this, txn, &table, &vals, &batch]() {
                store_.scanIndexEquality(txn, table, vals, [&](uint64_t entity_id) {
                    batch.push_back(entity_id);
                    return batch.size() < BATCH;
                });
            });
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
            if (batch.size() < BATCH)
                co_return;
        }
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexRange(LabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                             const std::optional<PropertyValue>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        std::string table = vidxTable(label_id, prop_id);
        auto s = start;
        auto e = end;
        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([this, txn, &table, &s, &e, &batch]() {
                store_.scanIndexRange(txn, table, s, e, [&](uint64_t entity_id) {
                    batch.push_back(entity_id);
                    return batch.size() < BATCH;
                });
            });
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
            if (batch.size() < BATCH)
                co_return;
        }
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexRangeComposite(LabelId label_id, const std::vector<uint16_t>& prop_ids,
                                      const std::optional<std::vector<PropertyValue>>& start,
                                      const std::optional<std::vector<PropertyValue>>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        std::string table = vidxCompositeTable(label_id, prop_ids);
        auto s = start;
        auto e = end;
        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([this, txn, &table, &s, &e, &batch]() {
                store_.scanIndexRange(txn, table, s, e, [&](uint64_t entity_id) {
                    batch.push_back(entity_id);
                    return batch.size() < BATCH;
                });
            });
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
            if (batch.size() < BATCH)
                co_return;
        }
    }

    // ==================== Edge Index Scan ====================

    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndex(EdgeLabelId label_id, uint16_t prop_id, const PropertyValue& value) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        std::string table = eidxTable(label_id, prop_id);
        auto val = value;
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            co_await io_.dispatchVoid([this, txn, &table, &val, &batch]() {
                store_.scanIndexEqualityWithValue(txn, table, val, [&](uint64_t entity_id, std::string_view v) {
                    EdgeIndexScanEntry entry;
                    entry.edge_id = entity_id;
                    ValueCodec::decodeEdgeAdjacency(v, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                    batch.push_back(entry);
                    return batch.size() < BATCH;
                });
            });
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
            if (batch.size() < BATCH)
                co_return;
        }
    }

    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexComposite(EdgeLabelId label_id, const std::vector<uint16_t>& prop_ids,
                              const std::vector<PropertyValue>& values) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        std::string table = eidxCompositeTable(label_id, prop_ids);
        auto vals = values;
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            co_await io_.dispatchVoid([this, txn, &table, &vals, &batch]() {
                store_.scanIndexEqualityWithValue(txn, table, vals, [&](uint64_t entity_id, std::string_view v) {
                    EdgeIndexScanEntry entry;
                    entry.edge_id = entity_id;
                    ValueCodec::decodeEdgeAdjacency(v, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                    batch.push_back(entry);
                    return batch.size() < BATCH;
                });
            });
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
            if (batch.size() < BATCH)
                co_return;
        }
    }

    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexRange(EdgeLabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                          const std::optional<PropertyValue>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        std::string table = eidxTable(label_id, prop_id);
        auto s = start;
        auto e = end;
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            co_await io_.dispatchVoid([this, txn, &table, &s, &e, &batch]() {
                store_.scanIndexRangeWithValue(txn, table, s, e, [&](uint64_t entity_id, std::string_view v) {
                    EdgeIndexScanEntry entry;
                    entry.edge_id = entity_id;
                    ValueCodec::decodeEdgeAdjacency(v, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                    batch.push_back(entry);
                    return batch.size() < BATCH;
                });
            });
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
            if (batch.size() < BATCH)
                co_return;
        }
    }

    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexRangeComposite(EdgeLabelId label_id, const std::vector<uint16_t>& prop_ids,
                                   const std::optional<std::vector<PropertyValue>>& start,
                                   const std::optional<std::vector<PropertyValue>>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txn_;
        std::string table = eidxCompositeTable(label_id, prop_ids);
        auto s = start;
        auto e = end;
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            co_await io_.dispatchVoid([this, txn, &table, &s, &e, &batch]() {
                store_.scanIndexRangeWithValue(txn, table, s, e, [&](uint64_t entity_id, std::string_view v) {
                    EdgeIndexScanEntry entry;
                    entry.edge_id = entity_id;
                    ValueCodec::decodeEdgeAdjacency(v, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                    batch.push_back(entry);
                    return batch.size() < BATCH;
                });
            });
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
            if (batch.size() < BATCH)
                co_return;
        }
    }

    // ==================== Batch Write ====================

    folly::coro::Task<void> batchInsertVertices(LabelId label_id, std::vector<BatchVertexEntry> entries) override {
        co_await io_.dispatchVoid([this, label_id, entries = std::move(entries)]() {
            auto txn = store_.beginTransaction();
            for (const auto& e : entries) {
                std::pair<LabelId, Properties> lp{label_id, e.props};
                store_.insertVertex(txn, e.vid, std::span<const std::pair<LabelId, Properties>>{&lp, 1});
            }
            store_.commitTransaction(txn);
        });
    }

    folly::coro::Task<void> batchInsertEdges(EdgeLabelId edge_label_id, std::vector<BatchEdgeEntry> entries) override {
        co_await io_.dispatchVoid([this, edge_label_id, entries = std::move(entries)]() {
            auto txn = store_.beginTransaction();
            for (const auto& e : entries) {
                store_.insertEdge(txn, e.eid, e.src_id, e.dst_id, edge_label_id, e.seq, e.props);
            }
            store_.commitTransaction(txn);
        });
    }

private:
    ISyncGraphDataStore& store_;
    IoScheduler& io_;
    GraphTxnHandle txn_;
};

} // namespace eugraph
