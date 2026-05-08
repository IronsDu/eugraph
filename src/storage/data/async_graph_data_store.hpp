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
        auto result =
            co_await io_.dispatch([this, vid, label_id]() { return store_.getVertexProperties(txn_, vid, label_id); });
        co_return std::move(result);
    }

    folly::coro::Task<std::optional<Properties>>
    getVertexProperties(VertexId vid, LabelId label_id, const std::vector<uint16_t>& projection) override {
        auto result = co_await io_.dispatch([this, vid, label_id, &projection]() -> std::optional<Properties> {
            Properties props;
            bool found_any = false;
            uint16_t max_id = 0;
            for (auto pid : projection) {
                if (pid > max_id)
                    max_id = pid;
            }
            props.resize(max_id + 1);
            for (auto pid : projection) {
                auto val = store_.getVertexProperty(txn_, vid, label_id, pid);
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
        auto result = co_await io_.dispatch([this, vid]() { return store_.getVertexLabels(txn_, vid); });
        co_return std::move(result);
    }

    // ==================== Edge Properties ====================

    folly::coro::Task<std::optional<Properties>> getEdgeProperties(EdgeLabelId label_id, EdgeId eid) override {
        auto result =
            co_await io_.dispatch([this, label_id, eid]() { return store_.getEdgeProperties(txn_, label_id, eid); });
        co_return std::move(result);
    }

    folly::coro::Task<std::optional<Properties>>
    getEdgeProperties(EdgeLabelId label_id, EdgeId eid, const std::vector<uint16_t>& projection) override {
        auto result = co_await io_.dispatch([this, label_id, eid, &projection]() -> std::optional<Properties> {
            Properties props;
            bool found_any = false;
            uint16_t max_id = 0;
            for (auto pid : projection) {
                if (pid > max_id)
                    max_id = pid;
            }
            props.resize(max_id + 1);
            for (auto pid : projection) {
                auto val = store_.getEdgeProperty(txn_, label_id, eid, pid);
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
        auto cursor =
            co_await io_.dispatch([this, label_id]() { return store_.createVertexScanCursor(txn_, label_id); });

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
        auto cursor = co_await io_.dispatch([this, vid, direction, label_filter]() {
            return store_.createEdgeScanCursor(txn_, vid, direction, label_filter);
        });

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
        auto cursor = co_await io_.dispatch([this, label_id, src_filter, dst_filter]() {
            return store_.createEdgeTypeScanCursor(txn_, label_id, src_filter, dst_filter);
        });

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
        auto ok = co_await io_.dispatch([this, vid, label_id, prop_id, &value]() {
            return store_.putVertexProperty(txn_, vid, label_id, prop_id, value);
        });
        co_return ok;
    }

    folly::coro::Task<bool> putVertexProperties(VertexId vid, LabelId label_id, const Properties& props) override {
        auto ok = co_await io_.dispatch(
            [this, vid, label_id, &props]() { return store_.putVertexProperties(txn_, vid, label_id, props); });
        co_return ok;
    }

    folly::coro::Task<bool> deleteVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id) override {
        auto ok = co_await io_.dispatch(
            [this, vid, label_id, prop_id]() { return store_.deleteVertexProperty(txn_, vid, label_id, prop_id); });
        co_return ok;
    }

    // ==================== Vertex Label Write ====================

    folly::coro::Task<bool> addVertexLabel(VertexId vid, LabelId label_id) override {
        auto ok = co_await io_.dispatch([this, vid, label_id]() { return store_.addVertexLabel(txn_, vid, label_id); });
        co_return ok;
    }

    folly::coro::Task<bool> removeVertexLabel(VertexId vid, LabelId label_id) override {
        auto ok =
            co_await io_.dispatch([this, vid, label_id]() { return store_.removeVertexLabel(txn_, vid, label_id); });
        co_return ok;
    }

    // ==================== Write Operations ====================

    folly::coro::Task<bool> insertVertex(VertexId vid,
                                         std::span<const std::pair<LabelId, Properties>> label_props) override {
        auto result = co_await io_.dispatch(
            [this, vid, label_props]() -> bool { return store_.insertVertex(txn_, vid, label_props); });
        co_return result;
    }

    folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id, uint64_t seq,
                                       const Properties& props) override {
        auto result = co_await io_.dispatch([this, eid, src_id, dst_id, label_id, seq, &props]() -> bool {
            return store_.insertEdge(txn_, eid, src_id, dst_id, label_id, seq, props);
        });
        co_return result;
    }

    // ==================== Index Operations ====================

    folly::coro::Task<bool> createIndex(const std::string& table_name) override {
        auto ok = co_await io_.dispatch([this, &table_name]() { return store_.createIndex(table_name); });
        co_return ok;
    }

    folly::coro::Task<bool> dropIndex(const std::string& table_name) override {
        auto ok = co_await io_.dispatch([this, &table_name]() { return store_.dropIndex(table_name); });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value,
                                             uint64_t entity_id) override {
        auto ok = co_await io_.dispatch(
            [this, &table, &value, entity_id]() { return store_.insertIndexEntry(table, value, entity_id); });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id) override {
        auto ok = co_await io_.dispatch(
            [this, &table, &values, entity_id]() { return store_.insertIndexEntry(table, values, entity_id); });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id,
                                             std::string payload) override {
        auto ok = co_await io_.dispatch([this, &table, &value, entity_id, payload = std::move(payload)]() {
            return store_.insertIndexEntry(table, value, entity_id, payload);
        });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id, std::string payload) override {
        auto ok = co_await io_.dispatch([this, &table, &values, entity_id, payload = std::move(payload)]() {
            return store_.insertIndexEntry(table, values, entity_id, payload);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const PropertyValue& value,
                                             uint64_t entity_id) override {
        auto ok = co_await io_.dispatch(
            [this, &table, &value, entity_id]() { return store_.deleteIndexEntry(table, value, entity_id); });
        co_return ok;
    }

    folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id) override {
        auto ok = co_await io_.dispatch(
            [this, &table, &values, entity_id]() { return store_.deleteIndexEntry(table, values, entity_id); });
        co_return ok;
    }

    folly::coro::Task<bool> checkUniqueConstraint(const std::string& table, const PropertyValue& value) override {
        auto ok =
            co_await io_.dispatch([this, &table, &value]() { return store_.checkUniqueConstraint(table, value); });
        co_return ok;
    }

    folly::coro::Task<bool> checkUniqueConstraint(const std::string& table,
                                                  const std::vector<PropertyValue>& values) override {
        auto ok =
            co_await io_.dispatch([this, &table, &values]() { return store_.checkUniqueConstraint(table, values); });
        co_return ok;
    }

    // ==================== Index Scan ====================

    folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndex(LabelId label_id, uint16_t prop_id,
                                                                           const PropertyValue& value) override {
        constexpr size_t BATCH = 1024;
        std::string table = vidxTable(label_id, prop_id);
        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([&]() {
                store_.scanIndexEquality(txn_, table, value, [&](uint64_t entity_id) {
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
        std::string table = vidxCompositeTable(label_id, prop_ids);
        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([&]() {
                store_.scanIndexEquality(txn_, table, values, [&](uint64_t entity_id) {
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
        std::string table = vidxTable(label_id, prop_id);
        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([&]() {
                store_.scanIndexRange(txn_, table, start, end, [&](uint64_t entity_id) {
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
        std::string table = vidxCompositeTable(label_id, prop_ids);
        while (true) {
            std::vector<VertexId> batch;
            co_await io_.dispatchVoid([&]() {
                store_.scanIndexRange(txn_, table, start, end, [&](uint64_t entity_id) {
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
        std::string table = eidxTable(label_id, prop_id);
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            co_await io_.dispatchVoid([&]() {
                store_.scanIndexEqualityWithValue(txn_, table, value, [&](uint64_t entity_id, std::string_view val) {
                    EdgeIndexScanEntry entry;
                    entry.edge_id = entity_id;
                    ValueCodec::decodeEdgeAdjacency(val, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
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
        std::string table = eidxCompositeTable(label_id, prop_ids);
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            co_await io_.dispatchVoid([&]() {
                store_.scanIndexEqualityWithValue(txn_, table, values, [&](uint64_t entity_id, std::string_view val) {
                    EdgeIndexScanEntry entry;
                    entry.edge_id = entity_id;
                    ValueCodec::decodeEdgeAdjacency(val, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
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
        std::string table = eidxTable(label_id, prop_id);
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            co_await io_.dispatchVoid([&]() {
                store_.scanIndexRangeWithValue(txn_, table, start, end, [&](uint64_t entity_id, std::string_view val) {
                    EdgeIndexScanEntry entry;
                    entry.edge_id = entity_id;
                    ValueCodec::decodeEdgeAdjacency(val, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
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
        std::string table = eidxCompositeTable(label_id, prop_ids);
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            co_await io_.dispatchVoid([&]() {
                store_.scanIndexRangeWithValue(txn_, table, start, end, [&](uint64_t entity_id, std::string_view val) {
                    EdgeIndexScanEntry entry;
                    entry.edge_id = entity_id;
                    ValueCodec::decodeEdgeAdjacency(val, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
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
