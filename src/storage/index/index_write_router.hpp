#pragma once
/// 写入路由：把"一次实体写入影响的索引"映射成**唯一的写入去向**（设计 I2/I3/I7）。
///
/// 设计依据：docs/storage/online-index-build-design.md §6 / §10 / §12。
///
/// 这一层只做决策，不碰存储：
///   * 查询注册表拿 phase/lifecycle；
///   * 需要写变更表时做**两阶段准入**（拿不到就返回 `RetryLater`，绝不直写索引表）；
///   * PUBLIC 时返回索引表，并告知是否要查唯一约束（只有 PUBLIC 的 unique 索引）。
///
/// 调用方（DML 算子 / 批量装载）负责真正的 `putDeltaEntry` / `insertIndexEntry`；
/// 真正的 store 写入在 P1a 接入，P0 只用单测把这套判定钉死。

#include "storage/index/index_runtime_registry.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {

/// 单个索引的写入决策。
struct IndexRouteDecision {
    uint32_t index_id = 0;
    IndexWriteDestination destination = IndexWriteDestination::kSkip;
    /// `kDelta` ⇒ 变更表名；`kIndexTable` ⇒ 索引表名；其余为空。
    std::string table;
    /// 仅 `kDelta` 时持有：准入凭证，必须挂在**用户事务**上（设计 I6）。
    std::optional<IndexWriteGuard> guard;
    /// 仅 `kIndexTable` 且索引 unique 时为 true（构建期不查唯一）。
    bool check_unique = false;

    /// 是否有实际写入动作（`kRetryLater`/`kSkip` 为 false）。
    explicit operator bool() const noexcept {
        return destination == IndexWriteDestination::kDelta || destination == IndexWriteDestination::kIndexTable;
    }
};

class IndexWriteRouter {
public:
    explicit IndexWriteRouter(IndexRuntimeRegistry& registry) : registry_(registry) {}

    /// 单个索引的去向（不做等待；拿不到准入返回 `RetryLater`）。
    IndexRouteDecision route(uint32_t index_id) {
        IndexRouteDecision decision;
        decision.index_id = index_id;

        auto runtime = registry_.find(index_id);
        if (!runtime)
            return decision; // kSkip：未注册/已清理

        const auto destination = indexWriteDestination(runtime->phase(), runtime->lifecycle());
        decision.destination = destination;

        if (destination == IndexWriteDestination::kDelta) {
            auto guard = runtime->tryEnter();
            if (!guard) {
                // 收尾中/已关闸：等待重试，**不得**直写索引表（I3）。
                decision.destination = IndexWriteDestination::kRetryLater;
                return decision;
            }
            decision.table = runtime->deltaTable();
            decision.guard = std::move(guard);
            return decision;
        }

        if (destination == IndexWriteDestination::kIndexTable) {
            decision.table = runtime->indexTable();
            decision.check_unique = indexUniqueEnforced(runtime->phase()) && runtime->unique();
            return decision;
        }

        return decision; // kRetryLater / kSkip
    }

    /// 一条语句涉及多个索引时：**按 index_id 升序**逐个准入（设计 §11.1 锁序），并去重。
    std::vector<IndexRouteDecision> routeAll(const std::vector<uint32_t>& index_ids) {
        auto ids = index_ids;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        std::vector<IndexRouteDecision> decisions;
        decisions.reserve(ids.size());
        for (uint32_t id : ids)
            decisions.push_back(route(id));
        return decisions;
    }

private:
    IndexRuntimeRegistry& registry_;
};

} // namespace eugraph
