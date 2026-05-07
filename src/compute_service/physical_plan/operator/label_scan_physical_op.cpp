#include "compute_service/physical_plan/operator/label_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/data_chunk.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> LabelScanPhysicalOp::executeChunk() {
    auto gen = store_.scanVerticesByLabel(label_id_);
    while (auto batch = co_await gen.next()) {
        DataChunk chunk;
        chunk.setSchema(output_types_);
        chunk.reserve(batch->size());

        for (VertexId vid : *batch) {
            auto labels = co_await store_.getVertexLabels(vid);
            VertexValue vv;
            vv.id = vid;
            vv.labels = labels;
            for (LabelId lid : labels) {
                auto props = co_await store_.getVertexProperties(vid, lid);
                if (props) {
                    vv.properties[lid] = std::move(*props);
                }
            }
            chunk.appendRow({Value(std::move(vv))});
        }
        if (chunk.count > 0) {
            co_yield std::move(chunk);
        }
    }
}

} // namespace compute
} // namespace eugraph
