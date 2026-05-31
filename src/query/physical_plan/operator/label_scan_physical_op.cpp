#include "query/physical_plan/operator/label_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> LabelScanPhysicalOp::executeChunk() {
    // Scan the first label's forward index; filter remaining labels at runtime.
    LabelId scan_label = label_ids_[0];
    auto gen = store_.scanVerticesByLabel(scan_label);
    while (auto batch = co_await gen.next()) {
        DataChunk chunk;
        chunk.setSchema(output_types_);
        chunk.reserve(batch->size());

        for (VertexId vid : *batch) {
            auto vlabels = co_await store_.getVertexLabels(vid);
            if (anon_label_id_ != INVALID_LABEL_ID)
                vlabels.erase(anon_label_id_);

            // Multi-label filter: vertex must have ALL specified labels
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
            vv.labels = vlabels;

            // Load properties from all vertex labels (not just scan labels) —
            // a vertex may have additional labels with properties that the query needs.
            for (LabelId lid : vlabels) {
                auto it = label_prop_ids_.find(lid);
                if (it == label_prop_ids_.end() || it->second.empty())
                    continue;
                auto props = co_await store_.getVertexProperties(vid, lid, it->second);
                if (props)
                    vv.properties[lid] = std::move(*props);
            }

            // Load __anon__ label properties
            if (anon_label_id_ != INVALID_LABEL_ID) {
                auto anon_def_it = label_defs_.find(anon_label_id_);
                if (anon_def_it != label_defs_.end() && !anon_def_it->second.properties.empty()) {
                    std::vector<uint16_t> anon_pids;
                    for (const auto& pd : anon_def_it->second.properties)
                        anon_pids.push_back(pd.id);
                    auto anon_props = co_await store_.getVertexProperties(vid, anon_label_id_, anon_pids);
                    if (anon_props)
                        vv.properties[anon_label_id_] = std::move(*anon_props);
                }
            }

            chunk.appendRow({Value(std::move(vv))});
        }
        if (chunk.count > 0)
            co_yield std::move(chunk);
    }
}

} // namespace compute
} // namespace eugraph
