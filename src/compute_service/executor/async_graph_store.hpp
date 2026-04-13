#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/io_scheduler.hpp"
#include "storage/graph_store.hpp"

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>

#include <memory>
#include <optional>
#include <vector>

namespace eugraph {
namespace compute {

/// Async wrapper around IGraphStore. All storage calls are dispatched to the IO
/// thread pool via IoScheduler. Scan operations yield batches of results.
class AsyncGraphStore {
public:
    AsyncGraphStore(IGraphStore& store, IoScheduler& io, GraphTxnHandle txn) : store_(&store), io_(&io), txn_(txn) {}

    void setTransaction(GraphTxnHandle txn) {
        txn_ = txn;
    }

    // ==================== Vertex Properties ====================

    folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id) {
        auto result = co_await io_->dispatch(
            [this, vid, label_id]() { return store_->getVertexProperties(txn_, vid, label_id); });
        co_return std::move(result);
    }

    folly::coro::Task<LabelIdSet> getVertexLabels(VertexId vid) {
        auto result = co_await io_->dispatch([this, vid]() { return store_->getVertexLabels(txn_, vid); });
        co_return std::move(result);
    }

    // ==================== Vertex Scan ====================

    folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByLabel(LabelId label_id) {
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

    folly::coro::AsyncGenerator<std::vector<IGraphStore::EdgeIndexEntry>>
    scanEdges(VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter) {
        constexpr size_t BATCH = 1024;
        auto cursor = co_await io_->dispatch([this, vid, direction, label_filter]() {
            return store_->createEdgeScanCursor(txn_, vid, direction, label_filter);
        });

        while (true) {
            std::vector<IGraphStore::EdgeIndexEntry> batch;
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

    folly::coro::AsyncGenerator<std::vector<IGraphStore::EdgeTypeIndexEntry>>
    scanEdgesByType(EdgeLabelId label_id, std::optional<VertexId> src_filter, std::optional<VertexId> dst_filter) {
        constexpr size_t BATCH = 1024;
        auto cursor = co_await io_->dispatch([this, label_id, src_filter, dst_filter]() {
            return store_->createEdgeTypeScanCursor(txn_, label_id, src_filter, dst_filter);
        });

        while (true) {
            std::vector<IGraphStore::EdgeTypeIndexEntry> batch;
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
                                         const PropertyValue* pk_value) {
        auto result = co_await io_->dispatch([this, vid, label_props, pk_value]() -> bool {
            return store_->insertVertex(txn_, vid, label_props, pk_value);
        });
        co_return result;
    }

    folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id, uint64_t seq,
                                       const Properties& props) {
        auto result = co_await io_->dispatch([this, eid, src_id, dst_id, label_id, seq, &props]() -> bool {
            return store_->insertEdge(txn_, eid, src_id, dst_id, label_id, seq, props);
        });
        co_return result;
    }

    // ==================== Direct store access (for label list, etc.) ====================

    IGraphStore& store() {
        return *store_;
    }

private:
    IGraphStore* store_;
    IoScheduler* io_;
    GraphTxnHandle txn_;
};

} // namespace compute
} // namespace eugraph
