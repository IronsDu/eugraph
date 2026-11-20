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
///   * FINALIZE ⇒ 有界等待（协程 sleep），超时返回 `kRetryLater`（绝不直写索引表）；
///   * ERROR/PURGING/不存在 ⇒ 跳过。

#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/index/index_write_router.hpp"
#include "storage/index/index_write_types.hpp"
#include "storage/session/graph_session.hpp"

#include <folly/coro/Task.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace eugraph {

/// 收尾期等待上限（P1b 接配置项）。
inline constexpr uint32_t kIndexWriteWaitMs = 1000;

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

/// 把准入名额绑定到本事务：事务 commit/rollback/close 时自动释放（I6）。
inline void bindAdmissionToTransaction(IAsyncGraphDataStore& store, IndexWriteGuard&& guard) {
    auto* session = store.graphSession();
    if (session == nullptr) {
        // 没有会话（理论上只出现在极简测试装配）：只能在当前协程内释放，属降级路径。
        guard.reset();
        return;
    }
    auto held = std::make_shared<IndexWriteGuard>(std::move(guard));
    session->onTransactionEnd([held] { held->reset(); });
}

} // namespace index_write_detail

inline folly::coro::Task<IndexWriteStatus> applyIndexWrite(IAsyncGraphDataStore& store, const IndexWriteItem& item) {
    auto* registry = store.indexRegistry();
    if (registry == nullptr || item.index_id == 0 || registry->find(item.index_id) == nullptr)
        co_return co_await index_write_detail::applyLegacy(store, item);

    IndexWriteRouter router(*registry);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kIndexWriteWaitMs);
    while (true) {
        auto decision = router.route(item.index_id);
        switch (decision.destination) {
        case IndexWriteDestination::kDelta: {
            if (!decision.guard) {
                co_return IndexWriteStatus::kRetryLater; // 不应发生（route 已保证）
            }
            index_write_detail::bindAdmissionToTransaction(store, std::move(*decision.guard));
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
        case IndexWriteDestination::kRetryLater:
            if (std::chrono::steady_clock::now() >= deadline)
                co_return IndexWriteStatus::kRetryLater;
            // 不用 folly::coro::sleep：它依赖 Timekeeper singleton；最小化测试装配
            // （gtest_main，无 folly::init）在收尾窗口首次触发时会直接 abort。
            // 这里等待只发生在 admission=CLOSED 的 FINALIZE 窗口，1ms 阻塞可接受。
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        co_return IndexWriteStatus::kRetryLater; // 不可达
    }
}

} // namespace eugraph
