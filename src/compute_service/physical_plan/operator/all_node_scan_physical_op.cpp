#include "compute_service/physical_plan/operator/all_node_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> AllNodeScanPhysicalOp::execute() {
    for (const auto& [name, label_id] : label_map_) {
        auto gen = store_.scanVerticesByLabel(label_id);
        while (auto batch = co_await gen.next()) {
            RowBatch output;
            for (VertexId vid : *batch) {
                auto props = co_await store_.getVertexProperties(vid, label_id);
                VertexValue vv;
                vv.id = vid;
                vv.labels = LabelIdSet{label_id};
                vv.properties = std::move(props);
                Row row;
                row.push_back(Value(std::move(vv)));
                output.push_back(std::move(row));
            }
            if (!output.empty()) {
                co_yield std::move(output);
            }
        }
    }
}

} // namespace compute
} // namespace eugraph
