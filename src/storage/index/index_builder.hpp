#pragma once
/// 索引构建的**扫描 + 最终化**（设计 §8）：SCAN 回填 → 关闸 → 等在飞写者 → 追平 → 校验。
///
/// 独立性：只依赖 storage 层门面（data store / session pool / registry），
/// 不依赖 QueryExecutor/DDL 解析 ⇒ 可脱离执行器单测，也避免后台线程重入执行器。
///
/// 边界：
///   * `runIndexBuild` 负责 SCAN（顶点/边两条路径）+ FINALIZE（关闸/排空/空索引/唯一性），
///     不写 meta 状态、不回收变更表；
///   * DDL 层只负责建定义/表/运行时、解析 spec、投递/等待、落 PUBLIC/ERROR；
///   * 单测可直接构造 spec + context 调用（见 `BuilderRunsWithoutQueryExecutor`）。
///
/// 依赖方向：本文件只依赖 storage 层（data store/session/registry），不依赖 query 层。

#include "common/types/index_runtime.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/index/index_test_hooks.hpp"
#include "storage/session/session_pool.hpp"

#include <folly/coro/Task.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {

/// 顶点索引的（已解析）访问器：strong 直接读源标签列，weak 需要按属性名找唯一 home。
struct IndexBuildAccessor {
    bool is_strong = false;
    LabelId source_label_id = INVALID_LABEL_ID;
    uint16_t source_prop_id = UINT16_MAX;
    std::string property_name;
};

struct IndexBuildSpec {
    uint32_t index_id = 0;
    std::string index_name;
    LabelId label_id = INVALID_LABEL_ID; ///< 顶点标签或边类型
    bool is_edge = false;
    bool unique = false;
    std::string index_table;
    std::string delta_table;
    std::vector<IndexBuildAccessor> accessors;    ///< 顶点；按索引键顺序
    std::vector<uint16_t> edge_prop_ids;          ///< 边；按索引键顺序
    uint64_t total_entities = 0;                  ///< 已知总数（顶点进度；0=未知）
    std::unordered_map<LabelId, LabelDef> labels; ///< schema 快照（weak 解析/anon 用）
    LabelId anon_label_id = INVALID_LABEL_ID;
};

struct IndexBuildContext {
    AsyncGraphDataStore& data;
    SessionPool& sessions;
    IndexRuntimeRegistry* registry = nullptr;
    std::function<bool()> is_cancelled; ///< DROP/关图取消令牌（可为空）
};

struct IndexBuildOutcome {
    bool ok = false;
    bool cancelled = false;
    std::string error;
};

/// 执行完整构建（SCAN + FINALIZE）。成功只代表数据侧完成；meta PUBLIC 由调用方落库。
folly::coro::Task<IndexBuildOutcome> runIndexBuild(const IndexBuildContext& ctx, const IndexBuildSpec& spec);

struct IndexFinalizeContext {
    AsyncGraphDataStore& data;      ///< 图级门面（for_session 会绑定池里的语句会话）
    IndexRuntimeRegistry* registry; ///< 每图注册表（可为空 ⇒ 跳过关闸，保持旧行为）
    SessionPool& sessions;          ///< 追平/校验用的会话池
    uint32_t index_id = 0;
    std::string index_table;
    std::string delta_table;
    bool unique = false;
    size_t scanned_with_all_props = 0; ///< SCAN 阶段的护栏计数
    size_t emitted_entries = 0;
    uint32_t gate_timeout_ms = 5000;
};

struct IndexFinalizeResult {
    bool ok = false;
    std::string error;
};

namespace index_builder_detail {

inline folly::coro::Task<IndexFinalizeResult> drainDelta(const IndexFinalizeContext& ctx) {
    constexpr size_t kReplayBatch = 1024;
    constexpr int kMaxRounds = 100000; // 防御：正常几轮就结束
    for (int round = 0; round < kMaxRounds; ++round) {
        auto lease = ctx.sessions.acquire();
        if (!lease)
            co_return IndexFinalizeResult{false, "drain delta: session pool exhausted"};
        auto store = ctx.data.for_session(**lease);
        if (!co_await store.beginSession(true))
            co_return IndexFinalizeResult{false, "drain delta: begin transaction failed"};

        size_t applied_total = 0;
        std::string last_key;
        while (true) {
            const auto result =
                co_await store.replayDeltaBatch(ctx.index_table, ctx.delta_table, kReplayBatch, last_key);
            applied_total += result.applied;
            if (ctx.registry != nullptr && result.applied > 0) {
                if (auto runtime = ctx.registry->find(ctx.index_id))
                    runtime->adjustDeltaBacklog(-static_cast<int64_t>(result.applied));
            }
            last_key = result.last_key;
            if (result.applied < kReplayBatch)
                break;
        }
        const auto outcome = co_await store.commitSession();
        if (!outcome.committed()) {
            // WT_ROLLBACK：整批回滚后重试（设计 §7.2：不能只重试 cursor 操作）。
            continue;
        }
        if (applied_total == 0)
            co_return IndexFinalizeResult{true, {}};
    }
    co_return IndexFinalizeResult{false, "drain delta: too many rounds"};
}

inline folly::coro::Task<IndexFinalizeResult> validateUnique(const IndexFinalizeContext& ctx) {
    auto lease = ctx.sessions.acquire();
    if (!lease)
        co_return IndexFinalizeResult{false, "unique validation: session pool exhausted"};
    auto store = ctx.data.for_session(**lease);
    auto duplicates = co_await store.indexHasDuplicateKeys(ctx.index_table);
    if (!duplicates.has_value())
        co_return IndexFinalizeResult{false, "unique validation: scan failed"};
    if (*duplicates)
        co_return IndexFinalizeResult{false, "unique validation: duplicate keys found after catch-up"};
    co_return IndexFinalizeResult{true, {}};
}

} // namespace index_builder_detail

/// 执行 FINALIZE 协议（不含 meta 状态写入）。
inline folly::coro::Task<IndexFinalizeResult> finalizeIndexBuild(const IndexFinalizeContext& ctx) {
    if (ctx.registry != nullptr) {
        if (auto runtime = ctx.registry->find(ctx.index_id)) {
            runtime->transitionPhase(IndexPhase::kCatchup);
            runtime->transitionPhase(IndexPhase::kFinalize);
            if (!runtime->closeAndWait(ctx.gate_timeout_ms)) {
                co_return IndexFinalizeResult{false, "finalize gate timed out waiting for in-flight writers"};
            }
        }
    }
    index_hooks::point("finalize.gate_closed", ctx.index_id);

    if (indexShouldFailEmpty(ctx.scanned_with_all_props, ctx.emitted_entries))
        co_return IndexFinalizeResult{false, "empty index guard: entities with all indexed properties exist but 0 "
                                             "entries were emitted"};

    auto drained = co_await index_builder_detail::drainDelta(ctx);
    if (!drained.ok)
        co_return drained;

    if (ctx.unique) {
        auto validated = co_await index_builder_detail::validateUnique(ctx);
        if (!validated.ok)
            co_return validated;
    }
    co_return IndexFinalizeResult{true, {}};
}

} // namespace eugraph
