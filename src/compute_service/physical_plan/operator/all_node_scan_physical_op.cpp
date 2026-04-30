#include "compute_service/physical_plan/operator/all_node_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

#include <unordered_set>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> AllNodeScanPhysicalOp::execute() {
    // First pass: collect all vertices with all their labels and all per-label properties
    std::unordered_map<VertexId, VertexValue> vertex_map;
    for (const auto& [name, label_id] : label_map_) {
        auto gen = store_.scanVerticesByLabel(label_id);
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch) {
                auto& vv = vertex_map[vid];
                if (vv.id == INVALID_VERTEX_ID) {
                    vv.id = vid;
                    vv.labels = co_await store_.getVertexLabels(vid);
                }
                auto props = co_await store_.getVertexProperties(vid, label_id);
                if (props) {
                    vv.properties[label_id] = std::move(*props);
                }
            }
        }
    }
    // Second pass: emit all collected vertices as rows
    RowBatch output;
    for (auto& [vid, vv] : vertex_map) {
        Row row;
        row.push_back(Value(std::move(vv)));
        output.push_back(std::move(row));
        if (output.size() >= RowBatch::CAPACITY) {
            co_yield std::move(output);
            output = RowBatch{};
        }
    }
    if (!output.empty()) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
