#pragma once

#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"

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
        : store_(&store), io_(&io), txn_(txn) {}

    void setTransaction(GraphTxnHandle txn) override {
        txn_ = txn;
    }

    // ==================== Vertex Properties ====================

    folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id) override {
        auto result = co_await io_->dispatch(
            [this, vid, label_id]() { return store_->getVertexProperties(txn_, vid, label_id); });
        co_return std::move(result);
    }

    folly::coro::Task<LabelIdSet> getVertexLabels(VertexId vid) override {
        auto result = co_await io_->dispatch([this, vid]() { return store_->getVertexLabels(txn_, vid); });
        co_return std::move(result);
    }

    // ==================== Vertex Scan ====================

    folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByLabel(LabelId label_id) override {
        constexpr size_t BATCH = 1024;
        auto cursor =
            co_await io_->dispatch([this, label_id]() { return store_->createVertexScanCursor(txn_, label_id); });

        while (true) {
            std::vector<VertexId> batch;
            co_await io_->dispatchVoid([&]() {
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
        auto cursor = co_await io_->dispatch([this, vid, direction, label_filter]() {
            return store_->createEdgeScanCursor(txn_, vid, direction, label_filter);
        });

        while (true) {
            std::vector<ISyncGraphDataStore::EdgeIndexEntry> batch;
            co_await io_->dispatchVoid([&]() {
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
        auto cursor = co_await io_->dispatch([this, label_id, src_filter, dst_filter]() {
            return store_->createEdgeTypeScanCursor(txn_, label_id, src_filter, dst_filter);
        });

        while (true) {
            std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> batch;
            co_await io_->dispatchVoid([&]() {
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

    // ==================== Write Operations ====================

    folly::coro::Task<bool> insertVertex(VertexId vid, std::span<const std::pair<LabelId, Properties>> label_props,
                                         const PropertyValue* pk_value) override {
        auto result = co_await io_->dispatch([this, vid, label_props, pk_value]() -> bool {
            return store_->insertVertex(txn_, vid, label_props, pk_value);
        });
        co_return result;
    }

    folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id, uint64_t seq,
                                       const Properties& props) override {
        auto result = co_await io_->dispatch([this, eid, src_id, dst_id, label_id, seq, &props]() -> bool {
            return store_->insertEdge(txn_, eid, src_id, dst_id, label_id, seq, props);
        });
        co_return result;
    }

private:
    ISyncGraphDataStore* store_;
    IoScheduler* io_;
    GraphTxnHandle txn_;
};

} // namespace eugraph
