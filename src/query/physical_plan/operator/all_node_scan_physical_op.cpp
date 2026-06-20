#include "query/physical_plan/operator/all_node_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"

#include <unordered_set>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> AllNodeScanPhysicalOp::executeChunk() {
    std::unordered_set<VertexId> seen_vids;

    for (const auto& [name, label_id] : label_map_) {
        if (label_id == INVALID_LABEL_ID)
            continue;
        auto gen = store_.scanVerticesByLabel(label_id);
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch)
                seen_vids.insert(vid);
        }
    }

    DataChunk chunk;
    chunk.setSchema(output_types_);
    chunk.reserve(DataChunk::DEFAULT_CAPACITY);

    for (VertexId vid : seen_vids) {
        VertexRef vr{vid};
        chunk.appendRow({Value(vr)});
        if (chunk.count >= DataChunk::DEFAULT_CAPACITY) {
            co_yield std::move(chunk);
            chunk = DataChunk{};
            chunk.setSchema(output_types_);
            chunk.reserve(DataChunk::DEFAULT_CAPACITY);
        }
    }
    if (chunk.count > 0)
        co_yield std::move(chunk);
}

} // namespace compute
} // namespace eugraph
