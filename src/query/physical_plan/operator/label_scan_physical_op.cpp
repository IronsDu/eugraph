#include "query/physical_plan/operator/label_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> LabelScanPhysicalOp::executeChunk() {
    bool multi_label = label_ids_.size() > 1;
    LabelId scan_label = label_ids_[0];
    auto gen = store_.scanVerticesByLabel(scan_label);
    while (auto batch = co_await gen.next()) {
        DataChunk chunk;
        chunk.setSchema(output_types_);
        chunk.reserve(batch->size());

        for (VertexId vid : *batch) {
            if (multi_label) {
                auto vlabels = co_await store_.getVertexLabels(vid);
                if (anon_label_id_ != INVALID_LABEL_ID)
                    vlabels.erase(anon_label_id_);
                bool has_all = true;
                for (LabelId lid : label_ids_) {
                    if (!vlabels.contains(lid)) {
                        has_all = false;
                        break;
                    }
                }
                if (!has_all)
                    continue;
                VertexValue vv;
                vv.id = vid;
                vv.labels = std::move(vlabels);
                chunk.appendRow({Value(std::move(vv))});
            } else {
                VertexValue vv;
                vv.id = vid;
                vv.labels = LabelIdSet(label_ids_.begin(), label_ids_.end());
                chunk.appendRow({Value(std::move(vv))});
            }
        }
        if (chunk.count > 0)
            co_yield std::move(chunk);
    }
}

} // namespace compute
} // namespace eugraph
