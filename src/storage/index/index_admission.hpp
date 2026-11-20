#pragma once
/// 批量写入口的事务级准入预取。
///
/// 批量写入是 sync 路径，不能在 IO 线程里等待 FINALIZE。因此在第一个数据写入前，
/// 把本批会触达的索引准入一次性拿到，并绑定到事务：
///   * 拿到后即使 FINALIZE 关闸，sync applier 也能继续写 delta；
///   * FINALIZE 的 closeAndWait 会等整个批量事务结束，不会 PUBLISH 到一半；
///   * 如果闸门已关，协程在这里等待 PUBLIC/ERROR，再开始写数据，不产生半成品。

#include "storage/index/index_runtime_registry.hpp"
#include "storage/session/graph_session.hpp"

#include <folly/coro/Task.h>

#include <cstdint>
#include <vector>

namespace eugraph {

/// 等一个索引可写；PUBLIC/ERROR/DROPPING 不需要准入，直接返回。
inline folly::coro::Task<bool> acquireIndexAdmission(GraphSession& session,
                                                     const std::shared_ptr<IndexRuntime>& runtime) {
    if (!runtime)
        co_return true;
    const uint32_t index_id = runtime->indexId();
    if (session.hasIndexAdmission(index_id))
        co_return true;

    while (true) {
        const auto phase = runtime->phase();
        if (phase == IndexPhase::kPublic || phase == IndexPhase::kError)
            co_return true;
        if (runtime->lifecycle() != IndexLifecycle::kActive)
            co_return true;

        if (auto guard = runtime->tryEnter()) {
            session.bindIndexAdmission(index_id, std::move(*guard));
            co_return true;
        }

        // 闸门已关：等 FINALIZE 结束。若构建失败，phase 会变 ERROR 并被唤醒。
        co_await runtime->waitForAdmission();
    }
}

/// 预取一批标签（顶点或边）相关的全部索引准入。
inline folly::coro::Task<void> acquireLabelIndexAdmissions(GraphSession& session, IndexRuntimeRegistry* registry,
                                                           std::vector<uint32_t> label_ids, bool is_edge) {
    if (registry == nullptr)
        co_return;
    for (uint32_t label_id : label_ids) {
        auto runtimes = registry->forLabel(label_id, is_edge);
        for (const auto& runtime : runtimes) {
            co_await acquireIndexAdmission(session, runtime);
        }
    }
}

} // namespace eugraph
