#pragma once
/// 每图统计存储：append-only 槽位 + 原子计数器 + 不可变快照。
///
/// 不在热路径做 map 的 read-modify-write；提交时把 StatsDelta 的增量
/// fetch_add 到稳定槽位，因此并发合并不会丢更新。

#include "common/stats/graph_stats.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace eugraph {

class GraphStatsStore {
public:
    GraphStatsStore() = default;
    GraphStatsStore(const GraphStatsStore&) = delete;
    GraphStatsStore& operator=(const GraphStatsStore&) = delete;

    /// 提交成功后的增量合并。并发安全；单槽位使用原子加，不丢更新。
    void applyCommitted(const StatsDelta& delta);

    /// 发布/读取近似快照。快照读取不阻塞增量合并。
    std::shared_ptr<const GraphStatsSnapshot> snapshot() const;

    /// 当前 epoch（诊断/测试）。
    uint64_t epoch() const noexcept {
        return epoch_.load(std::memory_order_acquire);
    }

private:
    struct LabelSlot {
        std::atomic<int64_t> vertices{0};
    };
    struct EdgeSlot {
        std::atomic<int64_t> edges{0};
    };
    struct IndexSlot {
        std::atomic<int64_t> rows{0};
        std::atomic<int64_t> null_keys{0};
        std::atomic<bool> ready{false};
    };

    mutable std::shared_mutex slots_mu_;
    std::unordered_map<LabelId, std::shared_ptr<LabelSlot>> labels_;
    std::unordered_map<EdgeLabelId, std::shared_ptr<EdgeSlot>> edge_labels_;
    std::unordered_map<uint32_t, std::shared_ptr<IndexSlot>> indexes_;

    mutable std::atomic<uint64_t> epoch_{0};

    std::shared_ptr<LabelSlot> labelSlot(LabelId id);
    std::shared_ptr<EdgeSlot> edgeSlot(EdgeLabelId id);
    std::shared_ptr<IndexSlot> indexSlot(uint32_t id);
};

} // namespace eugraph
