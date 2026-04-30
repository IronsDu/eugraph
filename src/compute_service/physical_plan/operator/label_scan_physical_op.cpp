#include "compute_service/physical_plan/operator/label_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> LabelScanPhysicalOp::execute() {
    auto gen = store_.scanVerticesByLabel(label_id_);
    while (auto batch = co_await gen.next()) {
        RowBatch output;
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
            Row row;
            row.push_back(Value(std::move(vv)));
            output.push_back(std::move(row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
