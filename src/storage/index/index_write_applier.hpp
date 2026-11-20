#pragma once
/// 索引维护写入的**唯一执行点**：由 router 决策、由本层执行（设计 I2/I3/I7）。
///
/// 分层：
///   * `index_write_router.hpp`：纯决策（去向 + 准入 + 表名 + 是否查重），不碰存储；
///   * 本文件：执行决策（写 delta / 写索引 / 等待 / 跳过），只依赖
///     `IAsyncGraphDataStore`（数据门面）与 `IndexRuntimeRegistry`（运行时权威）。
///
/// 语义：
///   * registry 为空或 index_id==0 ⇒ **legacy**：直接写 `legacy_table`（与改造前逐位一致）；
///   * BUILDING ⇒ 写变更表（准入名额挂到本事务，I6）；
///   * PUBLIC   ⇒ 直写索引表；unique 先查重；
///   * FINALIZE ⇒ 无事务准入时等闸门（baton），有事务准入时继续写 delta（绝不直写索引表）；
///   * ERROR/PURGING/不存在 ⇒ 跳过。

#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/index/index_write_router.hpp"
#include "storage/index/index_write_types.hpp"
#include "storage/session/graph_session.hpp"

#include <folly/coro/Task.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {

namespace index_write_detail {

inline folly::coro::Task<IndexWriteStatus> applyLegacy(IAsyncGraphDataStore& store, const IndexWriteItem& item) {
    if (item.is_delete) {
        co_return (co_await store.deleteIndexEntry(item.legacy_table, item.values, item.entity_id))
            ? IndexWriteStatus::kApplied
            : IndexWriteStatus::kRetryLater;
    }
    if (item.unique && !(co_await store.checkUniqueConstraint(item.legacy_table, item.values)))
        co_return IndexWriteStatus::kUniqueConflict;
    co_return (co_await store.insertIndexEntry(item.legacy_table, item.values, item.entity_id, item.payload))
        ? IndexWriteStatus::kApplied
        : IndexWriteStatus::kRetryLater;
}

/// 把准入名额绑定到本事务：事务 commit/rollback/close 时统一释放（I6）。
inline void bindAdmissionToTransaction(IAsyncGraphDataStore& store, uint32_t index_id, IndexWriteGuard&& guard) {
    auto* session = store.graphSession();
    if (session == nullptr) {
        // 没有会话（理论上只出现在极简测试装配）：只能在当前协程内释放，属降级路径。
        guard.reset();
        return;
    }
    session->bindIndexAdmission(index_id, std::move(guard));
}

/// 事务已经持有该索引准入时，即使 admission 已关也继续写 delta：
/// FINALIZE 的 closeAndWait 等的是整个事务，不会在事务提交前 PUBLISH。
inline folly::coro::Task<IndexWriteStatus> applyAdmittedDelta(IAsyncGraphDataStore& store, IndexRuntime& runtime,
                                                              const IndexWriteItem& item) {
    if (runtime.phase() == IndexPhase::kError || runtime.lifecycle() != IndexLifecycle::kActive)
        co_return IndexWriteStatus::kSkipped;
    const bool ok =
        co_await store.putDeltaEntry(runtime.deltaTable(), item.values, item.entity_id, item.is_delete, item.payload);
    if (!ok)
        co_return IndexWriteStatus::kRetryLater;
    runtime.adjustDeltaBacklog(1);
    co_return IndexWriteStatus::kApplied;
}

} // namespace index_write_detail

inline folly::coro::Task<IndexWriteStatus> applyIndexWrite(IAsyncGraphDataStore& store, const IndexWriteItem& item) {
    auto* registry = store.indexRegistry();
    if (registry == nullptr || item.index_id == 0 || registry->find(item.index_id) == nullptr)
        co_return co_await index_write_detail::applyLegacy(store, item);

    auto* session = store.graphSession();
    IndexWriteRouter router(*registry);
    while (true) {
        // 关键：同一事务已经准入该索引 ⇒ 即使闸门已关也继续写 delta。
        // 否则事务等 PUBLIC、FINALIZE 等事务结束，会死锁。
        if (session != nullptr && session->hasIndexAdmission(item.index_id)) {
            auto runtime = registry->find(item.index_id);
            if (!runtime)
                co_return IndexWriteStatus::kSkipped;
            co_return co_await index_write_detail::applyAdmittedDelta(store, *runtime, item);
        }

        auto decision = router.route(item.index_id);
        switch (decision.destination) {
        case IndexWriteDestination::kDelta: {
            if (!decision.guard)
                co_return IndexWriteStatus::kRetryLater; // 不应发生（route 已保证）
            index_write_detail::bindAdmissionToTransaction(store, item.index_id, std::move(*decision.guard));
            const bool ok =
                co_await store.putDeltaEntry(decision.table, item.values, item.entity_id, item.is_delete, item.payload);
            if (ok) {
                if (auto runtime = registry->find(item.index_id))
                    runtime->adjustDeltaBacklog(1);
                co_return IndexWriteStatus::kApplied;
            }
            co_return IndexWriteStatus::kRetryLater;
        }
        case IndexWriteDestination::kIndexTable: {
            if (item.is_delete) {
                co_return (co_await store.deleteIndexEntry(decision.table, item.values, item.entity_id))
                    ? IndexWriteStatus::kApplied
                    : IndexWriteStatus::kRetryLater;
            }
            if (decision.check_unique && !(co_await store.checkUniqueConstraint(decision.table, item.values)))
                co_return IndexWriteStatus::kUniqueConflict;
            co_return (co_await store.insertIndexEntry(decision.table, item.values, item.entity_id, item.payload))
                ? IndexWriteStatus::kApplied
                : IndexWriteStatus::kRetryLater;
        }
        case IndexWriteDestination::kSkip:
            co_return IndexWriteStatus::kSkipped;
        case IndexWriteDestination::kRetryLater: {
            // 事务已经持有别的索引准入时不能等：FINALIZE 可能正在等本事务结束。
            if (session != nullptr && session->hasAnyIndexAdmission())
                co_return IndexWriteStatus::kRetryLater;
            auto runtime = registry->find(item.index_id);
            if (!runtime)
                co_return IndexWriteStatus::kRetryLater;
            co_await runtime->waitForAdmission();
            continue;
        }
        }
        co_return IndexWriteStatus::kRetryLater; // 不可达
    }
}

} // namespace eugraph
