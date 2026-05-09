#include "compute_service/physical_plan/operator/all_node_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/data_chunk.hpp"

#include <unordered_set>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> AllNodeScanPhysicalOp::executeChunk() {
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
                auto it = label_prop_ids_.find(label_id);
                if (it != label_prop_ids_.end() && !it->second.empty()) {
                    auto props = co_await store_.getVertexProperties(vid, label_id, it->second);
                    if (props) {
                        vv.properties[label_id] = std::move(*props);
                    }
                }
            }
        }
    }

    DataChunk chunk;
    chunk.setSchema(output_types_);
    chunk.reserve(DataChunk::DEFAULT_CAPACITY);

    for (auto& [vid, vv] : vertex_map) {
        chunk.appendRow({Value(std::move(vv))});
        if (chunk.count >= DataChunk::DEFAULT_CAPACITY) {
            co_yield std::move(chunk);
            chunk = DataChunk{};
            chunk.setSchema(output_types_);
            chunk.reserve(DataChunk::DEFAULT_CAPACITY);
        }
    }
    if (chunk.count > 0) {
        co_yield std::move(chunk);
    }
}

} // namespace compute
} // namespace eugraph
