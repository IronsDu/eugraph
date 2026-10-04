#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"
#include "storage/data/index_maintenance.hpp"
#include "storage/index/index_delta_codec.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/kv/value_codec.hpp"

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>
#include <folly/coro/ViaIfAsync.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseManager.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace eugraph {

/// Async wrapper around ISyncGraphDataStore.
/// All storage calls are dispatched to the IO thread pool via IoScheduler.
class AsyncGraphDataStore : public IAsyncGraphDataStore {
public:
    AsyncGraphDataStore(ISyncGraphDataStore& store, IoScheduler& io, GraphTxnHandle txn = INVALID_GRAPH_TXN)
        : store_(store), io_(io) {
        txnRef() = txn; // 事务句柄按**线程**隔离（见下 txnRef() 的说明）
    }

    void setTransaction(GraphTxnHandle txn) override {
        txnRef() = txn;
    }

    std::unique_ptr<IAsyncGraphDataStore> forkTransaction(GraphTxnHandle txn) override {
        return std::make_unique<AsyncGraphDataStore>(store_, io_, txn);
    }

    // ==================== Transaction ====================

    folly::coro::Task<GraphTxnHandle> beginTran() override {
        auto txn = co_await io_.dispatch([this]() { return store_.beginTransaction(); });
        co_return txn;
    }

    /// 结束事务后必须清掉指向它的句柄：
    /// 该句柄是「本对象对外暴露的当前事务」，事务一旦结束即失效。
    /// 之前 commit/rollback 不清句柄，导致后续任何走该句柄的读写都拿它去查事务表，
    /// 命中不到就返回 nullptr session（或读到已释放的表项）—— 这正是「建索引的临时事务
    /// 结束后、批量写入解析主键时崩溃」的根因。
    folly::coro::Task<bool> commitTran(GraphTxnHandle txn) override {
        auto ok = co_await io_.dispatch([this, txn]() { return store_.commitTransaction(txn); });
        if (ok && txnRef() == txn)
            txnRef() = INVALID_GRAPH_TXN;
        co_return ok;
    }

    folly::coro::Task<bool> rollbackTran(GraphTxnHandle txn) override {
        auto ok = co_await io_.dispatch([this, txn]() { return store_.rollbackTransaction(txn); });
        if (txnRef() == txn)
            txnRef() = INVALID_GRAPH_TXN;
        co_return ok;
    }

    /// Synchronous rollback for teardown paths that have no coroutine to await (the
    /// Bolt connection drops abandoned streams from its EventBase thread). Doing the
    /// WT work inline is fine there: the transaction is idle by then, and blocking
    /// the loop is far cheaper than leaking its session forever.
    bool rollbackTranNow(GraphTxnHandle txn) override {
        bool ok = store_.rollbackTransaction(txn);
        if (txnRef() == txn)
            txnRef() = INVALID_GRAPH_TXN;
        return ok;
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
        auto txn = txnRef();
        auto result = co_await io_.dispatch(
            [this, txn, vid, label_id]() { return store_.getVertexProperties(txn, vid, label_id); });
        co_return std::move(result);
    }

    folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id,
                                                                     const std::vector<uint16_t>& projection) override {
        auto txn = txnRef();
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
        auto txn = txnRef();
        auto result = co_await io_.dispatch([this, txn, vid]() { return store_.getVertexLabels(txn, vid); });
        co_return result;
    }

    folly::coro::Task<std::vector<LabelIdSet>> getVertexLabelsBatch(const std::vector<VertexId>& vids) override {
        auto txn = txnRef();
        auto ids = vids;
        auto result = co_await io_.dispatch(
            [this, txn, ids = std::move(ids)]() { return store_.getVertexLabelsBatch(txn, ids); });
        co_return std::move(result);
    }

    folly::coro::Task<std::vector<std::optional<Properties>>>
    batchGetVertexProperties(const std::vector<VertexId>& vids, LabelId label_id,
                             const std::vector<uint16_t>& projection) override {
        auto txn = txnRef();
        auto ids = vids;
        auto proj = projection;
        auto result = co_await io_.dispatch([this, txn, label_id, ids = std::move(ids), proj = std::move(proj)]() {
            if (proj.empty())
                return store_.getVertexPropertiesBatch(txn, label_id, ids);
            // One cursor covers the whole batch instead of one open/close per (row, property):
            // cursor open/close measured at 51.6% of a point lookup
            // (docs/storage/session-cursor-ownership.md, POC-1).
            return store_.getVertexPropertiesBatchProjected(txn, label_id, ids, proj);
        });
        co_return std::move(result);
    }

    // ==================== Edge Properties ====================

    folly::coro::Task<std::optional<Properties>> getEdgeProperties(EdgeLabelId label_id, EdgeId eid) override {
        auto txn = txnRef();
        auto result = co_await io_.dispatch(
            [this, txn, label_id, eid]() { return store_.getEdgeProperties(txn, label_id, eid); });
        co_return std::move(result);
    }

    folly::coro::Task<std::optional<Properties>> getEdgeProperties(EdgeLabelId label_id, EdgeId eid,
                                                                   const std::vector<uint16_t>& projection) override {
        auto txn = txnRef();
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

    folly::coro::Task<std::vector<std::optional<PropertyValue>>>
    getEdgePropertyBatch(EdgeLabelId label_id, const std::vector<EdgeId>& edge_ids, uint16_t prop_id) override {
        auto txn = txnRef();
        auto ids = edge_ids;
        auto result = co_await io_.dispatch([this, txn, label_id, ids = std::move(ids), prop_id]() {
            return store_.getEdgePropertyBatch(txn, label_id, ids, prop_id);
        });
        co_return std::move(result);
    }

    // ==================== Vertex Scan ====================

    folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByLabel(LabelId label_id) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        // The scan cursor belongs to the session of the IO thread that creates it. Handing the
        // cursor to another IO thread would touch that session concurrently with its owner's
        // work -- the shared-session race that WiredTiger aborts on
        // (`WT_SESSION.open_cursor: lock_success == 0`). Pin the stream to the EXACT EventBase
        // that created the cursor and run every batch there: one thread => no concurrent use,
        // and the cursor keeps its position across batches (O(1) next()).
        // `co_viaIfAsync(io_pool)` would NOT be enough: the pool balances over its threads.
        folly::EventBase* bound_evb = nullptr;
        auto cursor = co_await io_.dispatch([this, txn, label_id, &bound_evb]() {
            bound_evb = folly::EventBaseManager::get()->getEventBase();
            return store_.createVertexScanCursor(txn, label_id);
        });
        if (!cursor)
            co_return;

        while (true) {
            // reserve(BATCH): the loop below fills up to BATCH or until the cursor runs
            // out, so one allocation replaces the log2 growths push_back would do.
            std::vector<VertexId> batch;
            batch.reserve(BATCH);
            // Run the batch on the cursor's own EventBase (see the comment above). Wrapped in a
            // task because folly's co_viaIfAsync takes (executor, awaitable).
            co_await folly::coro::co_viaIfAsync(bound_evb, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                                                    for (size_t i = 0; i < BATCH && cursor->valid(); ++i) {
                                                        batch.push_back(cursor->vertexId());
                                                        cursor->next();
                                                    }
                                                    co_return;
                                                }));
            if (batch.empty()) {
                co_return;
            }
            co_yield std::move(batch);
        }
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>> scanAllVertices() override {
        // 与 scanVerticesByLabel 同样的游标式流：vid 升序、每点一次，内存只有一个批。
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        // 同 scanVerticesByLabel：cursor 绑定创建它的那个 IO 线程的 session，必须钉在该线程上使用。
        folly::EventBase* bound_evb = nullptr;
        auto cursor = co_await io_.dispatch([this, txn, &bound_evb]() {
            bound_evb = folly::EventBaseManager::get()->getEventBase();
            return store_.createAllVertexScanCursor(txn);
        });
        if (!cursor)
            co_return;

        while (true) {
            std::vector<VertexId> batch;
            batch.reserve(BATCH); // 同上：填充上限已知
            co_await folly::coro::co_viaIfAsync(bound_evb, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                                                    for (size_t i = 0; i < BATCH && cursor->valid(); ++i) {
                                                        batch.push_back(cursor->vertexId());
                                                        cursor->next();
                                                    }
                                                    co_return;
                                                }));
            if (batch.empty())
                co_return;
            co_yield std::move(batch);
        }
    }

    // ==================== Edge Scan ====================

    folly::coro::AsyncGenerator<std::vector<ISyncGraphDataStore::EdgeIndexEntry>>
    scanEdges(VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter) override {
        constexpr size_t BATCH = 65536;
        auto txn = txnRef();
        // 同 scanVerticesByLabel：cursor 绑定创建它的那个 IO 线程的 session，必须钉在该线程上使用。
        folly::EventBase* bound_evb = nullptr;
        auto cursor = co_await io_.dispatch([this, txn, vid, direction, label_filter, &bound_evb]() {
            bound_evb = folly::EventBaseManager::get()->getEventBase();
            return store_.createEdgeScanCursor(txn, vid, direction, label_filter);
        });
        if (!cursor)
            co_return;

        while (true) {
            std::vector<ISyncGraphDataStore::EdgeIndexEntry> batch;
            batch.reserve(BATCH);
            co_await folly::coro::co_viaIfAsync(bound_evb, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                                                    for (size_t i = 0; i < BATCH && cursor->valid(); ++i) {
                                                        batch.push_back(cursor->entry());
                                                        cursor->next();
                                                    }
                                                    co_return;
                                                }));
            if (batch.empty()) {
                co_return;
            }
            co_yield std::move(batch);
        }
    }

    folly::coro::AsyncGenerator<std::vector<BatchedEdge>>
    scanEdgesBatch(const std::vector<VertexId>& src_ids, Direction direction,
                   std::optional<EdgeLabelId> label_filter) override {
        constexpr size_t BATCH = 65536;
        auto txn = txnRef();
        auto vids = src_ids;
        auto dir = direction;
        auto filter = label_filter;
        size_t offset = 0;
        while (offset < vids.size()) {
            std::vector<BatchedEdge> batch;
            batch.reserve(1024);
            co_await io_.dispatchVoid([this, txn, &vids, &offset, dir, filter, &batch]() {
                while (offset < vids.size() && batch.size() < BATCH) {
                    VertexId vid = vids[offset++];
                    auto cursor = store_.createEdgeScanCursor(txn, vid, dir, filter);
                    if (!cursor)
                        continue;
                    while (cursor->valid() && batch.size() < BATCH) {
                        batch.push_back(BatchedEdge{vid, cursor->entry()});
                        cursor->next();
                    }
                }
            });
            if (batch.empty()) {
                offset = vids.size();
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
        auto txn = txnRef();
        // 同 scanVerticesByLabel：cursor 绑定创建它的那个 IO 线程的 session，必须钉在该线程上使用。
        folly::EventBase* bound_evb = nullptr;
        auto cursor = co_await io_.dispatch([this, txn, label_id, src_filter, dst_filter, &bound_evb]() {
            bound_evb = folly::EventBaseManager::get()->getEventBase();
            return store_.createEdgeTypeScanCursor(txn, label_id, src_filter, dst_filter);
        });
        if (!cursor)
            co_return;

        while (true) {
            std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> batch;
            batch.reserve(BATCH);
            co_await folly::coro::co_viaIfAsync(bound_evb, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                                                    for (size_t i = 0; i < BATCH && cursor->valid(); ++i) {
                                                        batch.push_back(cursor->entry());
                                                        cursor->next();
                                                    }
                                                    co_return;
                                                }));
            if (batch.empty()) {
                co_return;
            }
            co_yield std::move(batch);
        }
    }

    // ==================== Vertex Property Write ====================

    folly::coro::Task<bool> putVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id,
                                              const PropertyValue& value) override {
        auto txn = txnRef();
        auto val = value;
        auto ok = co_await io_.dispatch([this, txn, vid, label_id, prop_id, val = std::move(val)]() {
            return store_.putVertexProperty(txn, vid, label_id, prop_id, val);
        });
        co_return ok;
    }

    folly::coro::Task<bool> putVertexProperties(VertexId vid, LabelId label_id, const Properties& props) override {
        auto txn = txnRef();
        auto p = props;
        auto ok = co_await io_.dispatch([this, txn, vid, label_id, p = std::move(p)]() {
            return store_.putVertexProperties(txn, vid, label_id, p);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id) override {
        auto txn = txnRef();
        auto ok = co_await io_.dispatch(
            [this, txn, vid, label_id, prop_id]() { return store_.deleteVertexProperty(txn, vid, label_id, prop_id); });
        co_return ok;
    }

    // ==================== Vertex Label Write ====================

    folly::coro::Task<bool> addVertexLabel(VertexId vid, LabelId label_id) override {
        auto txn = txnRef();
        auto ok =
            co_await io_.dispatch([this, txn, vid, label_id]() { return store_.addVertexLabel(txn, vid, label_id); });
        co_return ok;
    }

    folly::coro::Task<bool> removeVertexLabel(VertexId vid, LabelId label_id) override {
        auto txn = txnRef();
        auto ok = co_await io_.dispatch(
            [this, txn, vid, label_id]() { return store_.removeVertexLabel(txn, vid, label_id); });
        co_return ok;
    }

    // ==================== Write Operations ====================

    folly::coro::Task<bool> insertVertex(VertexId vid,
                                         std::span<const std::pair<LabelId, Properties>> label_props) override {
        auto txn = txnRef();
        auto result = co_await io_.dispatch(
            [this, txn, vid, label_props]() -> bool { return store_.insertVertex(txn, vid, label_props); });
        co_return result;
    }

    folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id, uint64_t seq,
                                       const Properties& props) override {
        auto txn = txnRef();
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

    folly::coro::Task<bool> putDeltaEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                          uint64_t entity_id, bool is_delete) override {
        auto txn = txnRef();
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), vals = std::move(vals), entity_id, is_delete]() {
            return store_.putDeltaEntry(txn, t, vals, entity_id, is_delete);
        });
        co_return ok;
    }

    folly::coro::Task<bool> putDeltaEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                          uint64_t entity_id, bool is_delete, std::string_view payload) override {
        auto txn = txnRef();
        auto t = table;
        auto vals = values;
        auto pl = std::string(payload);
        auto ok = co_await io_.dispatch(
            [this, txn, t = std::move(t), vals = std::move(vals), entity_id, is_delete, pl = std::move(pl)]() {
                return store_.putDeltaEntry(txn, t, vals, entity_id, is_delete, pl);
            });
        co_return ok;
    }

    folly::coro::Task<size_t> replayDeltaBatch(const std::string& index_table, const std::string& delta_table,
                                               size_t max_rows, std::string& last_key) override {
        auto txn = txnRef();
        auto idx_table = index_table;
        auto d_table = delta_table;
        auto start_after = last_key;
        auto result = co_await io_.dispatch([this, txn, idx_table = std::move(idx_table), d_table = std::move(d_table),
                                             max_rows, start_after = std::move(start_after)]() {
            size_t applied = 0;
            std::string next_key = start_after;
            bool ok = store_.scanDeltaWithKey(
                txn, d_table,
                [&](uint64_t /*entity_id*/, std::string_view key, std::string_view raw) {
                    bool is_delete = false;
                    std::string_view payload;
                    if (!decodeDeltaValue(raw, is_delete, payload))
                        return true; // 损坏记录跳过（不阻塞追赶）
                    if (is_delete)
                        store_.deleteIndexEntryByKey(txn, idx_table, key);
                    else
                        store_.putIndexEntryByKey(txn, idx_table, key, payload);
                    // **应用后从变更表删除**（设计 §6.1 方案①）：否则变更表永不排空，追赶无法终止。
                    store_.deleteIndexEntryByKey(txn, d_table, key);
                    ++applied;
                    return applied < max_rows;
                },
                start_after, &next_key);
            (void)ok;
            return std::pair<size_t, std::string>{applied, next_key};
        });
        last_key = std::move(result.second);
        co_return result.first;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value,
                                             uint64_t entity_id) override {
        auto txn = txnRef();
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), val = std::move(val), entity_id]() {
            return store_.insertIndexEntry(txn, t, val, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id) override {
        auto txn = txnRef();
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), vals = std::move(vals), entity_id]() {
            return store_.insertIndexEntry(txn, t, vals, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id,
                                             std::string payload) override {
        auto txn = txnRef();
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch(
            [this, txn, t = std::move(t), val = std::move(val), entity_id, payload = std::move(payload)]() {
                return store_.insertIndexEntry(txn, t, val, entity_id, payload);
            });
        co_return ok;
    }

    folly::coro::Task<bool> insertIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id, std::string payload) override {
        auto txn = txnRef();
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch(
            [this, txn, t = std::move(t), vals = std::move(vals), entity_id, payload = std::move(payload)]() {
                return store_.insertIndexEntry(txn, t, vals, entity_id, payload);
            });
        co_return ok;
    }

    folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const PropertyValue& value,
                                             uint64_t entity_id) override {
        auto txn = txnRef();
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), val = std::move(val), entity_id]() {
            return store_.deleteIndexEntry(txn, t, val, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const std::vector<PropertyValue>& values,
                                             uint64_t entity_id) override {
        auto txn = txnRef();
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), vals = std::move(vals), entity_id]() {
            return store_.deleteIndexEntry(txn, t, vals, entity_id);
        });
        co_return ok;
    }

    folly::coro::Task<bool> checkUniqueConstraint(const std::string& table, const PropertyValue& value) override {
        auto txn = txnRef();
        auto t = table;
        auto val = value;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), val = std::move(val)]() {
            return store_.checkUniqueConstraint(txn, t, val);
        });
        co_return ok;
    }

    folly::coro::Task<bool> checkUniqueConstraint(const std::string& table,
                                                  const std::vector<PropertyValue>& values) override {
        auto txn = txnRef();
        auto t = table;
        auto vals = values;
        auto ok = co_await io_.dispatch([this, txn, t = std::move(t), vals = std::move(vals)]() {
            return store_.checkUniqueConstraint(txn, t, vals);
        });
        co_return ok;
    }

    // ==================== Index Scan ====================

    folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndex(LabelId label_id, uint16_t prop_id,
                                                                           const PropertyValue& value) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = vidxTable(label_id, prop_id);
        auto val = value;
        // The synchronous index APIs do not expose a resumable cursor. Collect
        // the (index-bounded) match set in one dispatch, then emit chunks.
        std::vector<VertexId> all;
        co_await io_.dispatchVoid([this, txn, &table, &val, &all]() {
            store_.scanIndexEquality(txn, table, val, [&](uint64_t entity_id) {
                all.push_back(entity_id);
                return true;
            });
        });
        for (size_t i = 0; i < all.size(); i += BATCH) {
            size_t end = std::min(i + BATCH, all.size());
            std::vector<VertexId> batch(all.begin() + static_cast<long>(i), all.begin() + static_cast<long>(end));
            co_yield std::move(batch);
        }
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexComposite(LabelId label_id, const std::vector<uint16_t>& prop_ids,
                                 const std::vector<PropertyValue>& values) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = vidxCompositeTable(label_id, prop_ids);
        auto vals = values;
        std::vector<VertexId> all;
        co_await io_.dispatchVoid([this, txn, &table, &vals, &all]() {
            store_.scanIndexEquality(txn, table, vals, [&](uint64_t entity_id) {
                all.push_back(entity_id);
                return true;
            });
        });
        for (size_t i = 0; i < all.size(); i += BATCH) {
            size_t end = std::min(i + BATCH, all.size());
            std::vector<VertexId> batch(all.begin() + static_cast<long>(i), all.begin() + static_cast<long>(end));
            co_yield std::move(batch);
        }
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexRange(LabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                             const std::optional<PropertyValue>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = vidxTable(label_id, prop_id);
        auto s = start;
        auto e = end;
        std::vector<VertexId> all;
        co_await io_.dispatchVoid([this, txn, &table, &s, &e, &all]() {
            store_.scanIndexRange(txn, table, s, e, [&](uint64_t entity_id) {
                all.push_back(entity_id);
                return true;
            });
        });
        for (size_t i = 0; i < all.size(); i += BATCH) {
            size_t end_idx = std::min(i + BATCH, all.size());
            std::vector<VertexId> batch(all.begin() + static_cast<long>(i), all.begin() + static_cast<long>(end_idx));
            co_yield std::move(batch);
        }
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexRangeComposite(LabelId label_id, const std::vector<uint16_t>& prop_ids,
                                      const std::optional<std::vector<PropertyValue>>& start,
                                      const std::optional<std::vector<PropertyValue>>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = vidxCompositeTable(label_id, prop_ids);
        auto s = start;
        auto e = end;
        std::vector<VertexId> all;
        co_await io_.dispatchVoid([this, txn, &table, &s, &e, &all]() {
            store_.scanIndexRange(txn, table, s, e, [&](uint64_t entity_id) {
                all.push_back(entity_id);
                return true;
            });
        });
        for (size_t i = 0; i < all.size(); i += BATCH) {
            size_t end_idx = std::min(i + BATCH, all.size());
            std::vector<VertexId> batch(all.begin() + static_cast<long>(i), all.begin() + static_cast<long>(end_idx));
            co_yield std::move(batch);
        }
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexId(uint32_t index_id, const std::vector<PropertyValue>& values) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = vidxTableById(index_id);
        auto vals = values;
        std::vector<VertexId> all;
        co_await io_.dispatchVoid([this, txn, &table, &vals, &all]() {
            store_.scanIndexEquality(txn, table, vals, [&](uint64_t entity_id) {
                all.push_back(entity_id);
                return true;
            });
        });
        for (size_t i = 0; i < all.size(); i += BATCH) {
            size_t end = std::min(i + BATCH, all.size());
            std::vector<VertexId> batch(all.begin() + static_cast<long>(i), all.begin() + static_cast<long>(end));
            co_yield std::move(batch);
        }
    }

    folly::coro::Task<std::optional<VertexId>>
    lookupVertexByPrimaryKey(uint32_t index_id, const std::vector<PropertyValue>& values) override {
        auto txn = txnRef();
        std::string table = vidxTableById(index_id);
        auto vals = values;
        std::optional<VertexId> best;
        co_await io_.dispatchVoid([this, txn, &table, &vals, &best]() {
            store_.scanIndexEquality(txn, table, vals, [&](uint64_t entity_id) {
                VertexId vid = static_cast<VertexId>(entity_id);
                if (!best.has_value() || vid < *best)
                    best = vid;
                return true; // 需要扫完才能保证拿到最小 vid（前缀命中的条目通常只有 1 条）
            });
        });
        co_return best;
    }

    folly::coro::AsyncGenerator<std::vector<VertexId>>
    scanVerticesByIndexIdRange(uint32_t index_id, const std::optional<std::vector<PropertyValue>>& start,
                               const std::optional<std::vector<PropertyValue>>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = vidxTableById(index_id);
        auto s = start;
        auto e = end;
        std::vector<VertexId> all;
        co_await io_.dispatchVoid([this, txn, &table, &s, &e, &all]() {
            store_.scanIndexRange(txn, table, s, e, [&](uint64_t entity_id) {
                all.push_back(entity_id);
                return true;
            });
        });
        for (size_t i = 0; i < all.size(); i += BATCH) {
            size_t end_idx = std::min(i + BATCH, all.size());
            std::vector<VertexId> batch(all.begin() + static_cast<long>(i), all.begin() + static_cast<long>(end_idx));
            co_yield std::move(batch);
        }
    }

    // ==================== Edge Index Scan ====================

    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndex(EdgeLabelId label_id, uint16_t prop_id, const PropertyValue& value) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = eidxTable(label_id, prop_id);
        auto val = value;
        // 续扫位置：上一批最后一个键。缺了它每批都会从头重扫、满批即停 ⇒ >1 批的结果被静默截断。
        std::string last_key;
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            batch.reserve(BATCH);
            std::string next_key;
            co_await io_.dispatchVoid([this, txn, &table, &val, &batch, &last_key, &next_key]() {
                store_.scanIndexEqualityWithValue(
                    txn, table, val,
                    [&](uint64_t entity_id, std::string_view v) {
                        EdgeIndexScanEntry entry;
                        entry.edge_id = entity_id;
                        ValueCodec::decodeEdgeAdjacency(v, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                        batch.push_back(entry);
                        return batch.size() < BATCH;
                    },
                    last_key, &next_key);
            });
            if (batch.empty())
                co_return;
            const bool full = batch.size() == BATCH; // 必须在 move 之前取长度（moved-from 的 size() 不可靠）
            last_key = std::move(next_key);
            co_yield std::move(batch);
            if (!full)
                co_return; // 满批 ⇒ 从 last_key 之后继续（真正推进）
        }
    }

    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexComposite(EdgeLabelId label_id, const std::vector<uint16_t>& prop_ids,
                              const std::vector<PropertyValue>& values) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = eidxCompositeTable(label_id, prop_ids);
        auto vals = values;
        // 续扫位置：上一批最后一个键（缺了它每批从头重扫 ⇒ >1 批结果被静默截断）
        std::string last_key;
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            batch.reserve(BATCH);
            std::string next_key;
            co_await io_.dispatchVoid([this, txn, &table, &vals, &batch, &last_key, &next_key]() {
                store_.scanIndexEqualityWithValue(
                    txn, table, vals,
                    [&](uint64_t entity_id, std::string_view v) {
                        EdgeIndexScanEntry entry;
                        entry.edge_id = entity_id;
                        ValueCodec::decodeEdgeAdjacency(v, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                        batch.push_back(entry);
                        return batch.size() < BATCH;
                    },
                    last_key, &next_key);
            });
            if (batch.empty())
                co_return;
            const bool full = batch.size() == BATCH; // 必须在 move 之前取长度（moved-from 的 size() 不可靠）
            last_key = std::move(next_key);
            co_yield std::move(batch);
            if (!full)
                co_return; // 满批 ⇒ 从 last_key 之后继续（真正推进）
        }
    }

    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexRange(EdgeLabelId label_id, uint16_t prop_id, const std::optional<PropertyValue>& start,
                          const std::optional<PropertyValue>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = eidxTable(label_id, prop_id);
        auto s = start;
        auto e = end;
        // 续扫位置：上一批最后一个键（缺了它每批从头重扫 ⇒ >1 批结果被静默截断）
        std::string last_key;
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            batch.reserve(BATCH);
            std::string next_key;
            co_await io_.dispatchVoid([this, txn, &table, &s, &e, &batch, &last_key, &next_key]() {
                store_.scanIndexRangeWithValue(
                    txn, table, s, e,
                    [&](uint64_t entity_id, std::string_view v) {
                        EdgeIndexScanEntry entry;
                        entry.edge_id = entity_id;
                        ValueCodec::decodeEdgeAdjacency(v, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                        batch.push_back(entry);
                        return batch.size() < BATCH;
                    },
                    last_key, &next_key);
            });
            if (batch.empty())
                co_return;
            const bool full = batch.size() == BATCH; // 必须在 move 之前取长度（moved-from 的 size() 不可靠）
            last_key = std::move(next_key);
            co_yield std::move(batch);
            if (!full)
                co_return; // 满批 ⇒ 从 last_key 之后继续（真正推进）
        }
    }

    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>>
    scanEdgesByIndexRangeComposite(EdgeLabelId label_id, const std::vector<uint16_t>& prop_ids,
                                   const std::optional<std::vector<PropertyValue>>& start,
                                   const std::optional<std::vector<PropertyValue>>& end) override {
        constexpr size_t BATCH = 1024;
        auto txn = txnRef();
        std::string table = eidxCompositeTable(label_id, prop_ids);
        auto s = start;
        auto e = end;
        // 续扫位置：上一批最后一个键（缺了它每批从头重扫 ⇒ >1 批结果被静默截断）
        std::string last_key;
        while (true) {
            std::vector<EdgeIndexScanEntry> batch;
            batch.reserve(BATCH);
            std::string next_key;
            co_await io_.dispatchVoid([this, txn, &table, &s, &e, &batch, &last_key, &next_key]() {
                store_.scanIndexRangeWithValue(
                    txn, table, s, e,
                    [&](uint64_t entity_id, std::string_view v) {
                        EdgeIndexScanEntry entry;
                        entry.edge_id = entity_id;
                        ValueCodec::decodeEdgeAdjacency(v, entry.src_id, entry.dst_id, entry.seq, entry.label_id);
                        batch.push_back(entry);
                        return batch.size() < BATCH;
                    },
                    last_key, &next_key);
            });
            if (batch.empty())
                co_return;
            const bool full = batch.size() == BATCH; // 必须在 move 之前取长度（moved-from 的 size() 不可靠）
            last_key = std::move(next_key);
            co_yield std::move(batch);
            if (!full)
                co_return; // 满批 ⇒ 从 last_key 之后继续（真正推进）
        }
    }

    // ==================== Delete Operations ====================

    folly::coro::Task<bool> deleteVertex(VertexId vid) override {
        auto txn = txnRef();
        auto ok = co_await io_.dispatch([this, txn, vid]() { return store_.deleteVertex(txn, vid); });
        co_return ok;
    }

    folly::coro::Task<bool> deleteEdge(EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                                       uint64_t seq) override {
        auto txn = txnRef();
        auto ok = co_await io_.dispatch([this, txn, eid, label_id, src_id, dst_id, seq]() {
            return store_.deleteEdge(txn, eid, label_id, src_id, dst_id, seq);
        });
        co_return ok;
    }

    folly::coro::Task<bool> deleteEdgeProperty(EdgeId eid, EdgeLabelId label_id, uint16_t prop_id) override {
        auto txn = txnRef();
        auto ok = co_await io_.dispatch(
            [this, txn, eid, label_id, prop_id]() { return store_.deleteEdgeProperty(txn, label_id, eid, prop_id); });
        co_return ok;
    }

    folly::coro::Task<bool> putEdgeProperty(EdgeId eid, EdgeLabelId label_id, uint16_t prop_id,
                                            const PropertyValue& value) override {
        auto txn = txnRef();
        auto val = value;
        auto ok = co_await io_.dispatch([this, txn, eid, label_id, prop_id, val = std::move(val)]() {
            return store_.putEdgeProperty(txn, label_id, eid, prop_id, val);
        });
        co_return ok;
    }

    // ==================== Batch Write ====================

    folly::coro::Task<void> batchInsertVertices(std::vector<BatchVertexEntry> entries,
                                                const std::unordered_map<LabelId, LabelDef>& label_defs) override {
        co_await io_.dispatchVoid([this, entries = std::move(entries), &label_defs]() {
            auto txn = store_.beginTransaction();
            for (const auto& e : entries) {
                store_.insertVertex(
                    txn, e.vid,
                    std::span<const std::pair<LabelId, Properties>>{e.label_props.data(), e.label_props.size()});
                // 顶点数据与索引条目同事务，避免「顶点已提交、索引没跟上」
                auto index_entries = collectVertexIndexEntriesFromLabelProps(label_defs, e.label_props, e.vid);
                insertVertexIndexEntries(store_, txn, index_entries);
            }
            store_.commitTransaction(txn);
        });
    }

    folly::coro::Task<void>
    batchInsertEdges(EdgeLabelId edge_label_id, std::vector<BatchEdgeEntry> entries,
                     const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs) override {
        co_await io_.dispatchVoid([this, edge_label_id, entries = std::move(entries), &edge_label_defs]() {
            auto txn = store_.beginTransaction();
            for (const auto& e : entries) {
                store_.insertEdge(txn, e.eid, e.src_id, e.dst_id, edge_label_id, e.seq, e.props);
                auto index_entries = collectEdgeIndexEntries(edge_label_defs, edge_label_id, e.eid, e.props);
                insertEdgeIndexEntries(store_, txn, index_entries);
            }
            store_.commitTransaction(txn);
        });
    }

private:
    ISyncGraphDataStore& store_;
    IoScheduler& io_;
    /// **按线程隔离**的事务句柄（与不变量 I10"session 永不共享"一致）。
    /// 此前它是 per-store 成员 ⇒ 后台构建线程与查询线程会共用同一个 txn/session
    /// ⇒ WT `session_dhandle` 损坏 + SIGSEGV（实测，设计 §20.17/§20.18）。
    GraphTxnHandle& txnRef() {
        static thread_local std::unordered_map<const AsyncGraphDataStore*, GraphTxnHandle> per_thread;
        return per_thread[this];
    }
    const GraphTxnHandle& txnRef() const {
        static thread_local std::unordered_map<const AsyncGraphDataStore*, GraphTxnHandle> per_thread;
        auto it = per_thread.find(this);
        return it == per_thread.end() ? kNoTxn_ : it->second;
    }
    static inline const GraphTxnHandle kNoTxn_ = INVALID_GRAPH_TXN;
};

} // namespace eugraph
