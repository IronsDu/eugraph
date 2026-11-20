#pragma once
/// 同步版写执行器：供**批量装载**（`batchInsertVertices/Edges`）等 sync 路径使用。
///
/// 与 `index_write_applier.hpp`（异步协程版）共用：
///   * `index_write_router.hpp` 的决策（去向/表名/准入/是否查重）；
///   * `index_write_types.hpp` 的条目与状态。
///
/// 差异：FINALIZE 期**不等待**（sync 路径在 IO 线程上，不能阻塞 sleep）——
/// 返回 `kRetryLater`，由调用方让整批失败（loader 场景索引早已 PUBLIC，不会走到这里）。

#include "storage/data/i_sync_graph_data_store.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/index/index_write_router.hpp"
#include "storage/index/index_write_types.hpp"
#include "storage/session/graph_session.hpp"

#include <memory>

namespace eugraph {

inline IndexWriteStatus applyIndexWriteSync(ISyncGraphDataStore& store, GraphSession& session,
                                            IndexRuntimeRegistry* registry, const IndexWriteItem& item) {
    const auto legacy = [&]() -> IndexWriteStatus {
        if (item.is_delete) {
            return store.deleteIndexEntry(session, item.legacy_table, item.values, item.entity_id)
                       ? IndexWriteStatus::kApplied
                       : IndexWriteStatus::kRetryLater;
        }
        if (item.unique && !store.checkUniqueConstraint(session, item.legacy_table, item.values))
            return IndexWriteStatus::kUniqueConflict;
        return store.insertIndexEntry(session, item.legacy_table, item.values, item.entity_id, item.payload)
                   ? IndexWriteStatus::kApplied
                   : IndexWriteStatus::kRetryLater;
    };

    if (registry == nullptr || item.index_id == 0 || registry->find(item.index_id) == nullptr)
        return legacy();

    IndexWriteRouter router(*registry);
    auto decision = router.route(item.index_id);
    switch (decision.destination) {
    case IndexWriteDestination::kDelta: {
        if (!decision.guard)
            return IndexWriteStatus::kRetryLater;
        auto held = std::make_shared<IndexWriteGuard>(std::move(*decision.guard));
        session.onTransactionEnd([held] { held->reset(); }); // 覆盖整个批量事务（I6）
        const bool ok =
            store.putDeltaEntry(session, decision.table, item.values, item.entity_id, item.is_delete, item.payload);
        if (ok) {
            if (auto runtime = registry->find(item.index_id))
                runtime->adjustDeltaBacklog(1);
            return IndexWriteStatus::kApplied;
        }
        return IndexWriteStatus::kRetryLater;
    }
    case IndexWriteDestination::kIndexTable: {
        if (item.is_delete) {
            return store.deleteIndexEntry(session, decision.table, item.values, item.entity_id)
                       ? IndexWriteStatus::kApplied
                       : IndexWriteStatus::kRetryLater;
        }
        if (decision.check_unique && !store.checkUniqueConstraint(session, decision.table, item.values))
            return IndexWriteStatus::kUniqueConflict;
        return store.insertIndexEntry(session, decision.table, item.values, item.entity_id, item.payload)
                   ? IndexWriteStatus::kApplied
                   : IndexWriteStatus::kRetryLater;
    }
    case IndexWriteDestination::kSkip:
        return IndexWriteStatus::kSkipped;
    case IndexWriteDestination::kRetryLater:
        return IndexWriteStatus::kRetryLater;
    }
    return IndexWriteStatus::kRetryLater;
}

} // namespace eugraph
