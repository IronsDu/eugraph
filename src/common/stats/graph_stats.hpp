#pragma once
/// CBO 使用的近似统计快照与事务 delta。
///
/// 统计是 advisory：不参与用户事务提交/回滚判定；允许过期/近似，但必须显式标记。
/// 与 GraphSchema 解耦：统计更新不递增 schema_generation，也不发布 schema。

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <unordered_map>

namespace eugraph {

struct LabelStats {
    int64_t vertices = 0;
};

struct EdgeLabelStats {
    int64_t edges = 0;
    double avg_out_degree = 0.0;
};

struct IndexStats {
    int64_t rows = 0;
    int64_t null_keys = 0;
    bool ready = false;
};

struct GraphStatsSnapshot {
    uint64_t epoch = 0;
    uint64_t applied_journal_seq = 0;
    bool approximate = true;
    std::unordered_map<LabelId, LabelStats> labels;
    std::unordered_map<EdgeLabelId, EdgeLabelStats> edge_labels;
    std::unordered_map<uint32_t, IndexStats> indexes;
};

/// 单个事务内累积的统计增量；commit 成功后合并，rollback 丢弃。
struct StatsDelta {
    std::unordered_map<LabelId, int64_t> vertex_delta;
    std::unordered_map<EdgeLabelId, int64_t> edge_delta;
    std::unordered_map<uint32_t, int64_t> index_delta;

    bool empty() const noexcept {
        return vertex_delta.empty() && edge_delta.empty() && index_delta.empty();
    }
};

} // namespace eugraph
