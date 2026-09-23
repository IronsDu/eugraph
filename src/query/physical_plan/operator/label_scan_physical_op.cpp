#include "query/physical_plan/operator/label_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> LabelScanPhysicalOp::executeChunk() {
    if (label_ids_.empty())
        co_return; // all specified labels were nonexistent → zero rows

    bool multi_label = label_ids_.size() > 1;
    LabelId scan_label = label_ids_[0];
    auto gen = cancellable(store_.scanVerticesByLabel(scan_label));
    while (auto batch = co_await gen.next()) {
        DataChunk chunk;
        chunk.setSchema(output_types_);
        chunk.reserve(batch->size());
        // 单列 VERTEX_REF：列取一次复用，行内不再做 columns[0]（chunk 每批重建，引用随之更新）
        Column& out = chunk.columns[0];

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
            }
            chunk.appendVertexRefRow(out, vid);
        }
        if (chunk.count > 0) {
            chunk.sel = SelectionVector::identity(chunk.count);
            co_yield std::move(chunk);
        }
    }
}

} // namespace compute
} // namespace eugraph
