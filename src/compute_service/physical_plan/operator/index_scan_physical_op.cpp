#include "compute_service/physical_plan/operator/index_scan_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> IndexScanPhysicalOp::execute() {
    folly::coro::AsyncGenerator<std::vector<VertexId>> gen;
    if (mode_ == ScanMode::EQUALITY) {
        gen = store_.scanVerticesByIndex(label_id_, prop_id_, search_value_);
    } else {
        gen = store_.scanVerticesByIndexRange(label_id_, prop_id_, search_value_, range_end_);
    }

    while (auto batch = co_await gen.next()) {
        RowBatch output;
        for (VertexId vid : *batch) {
            auto props = co_await store_.getVertexProperties(vid, label_id_);
            auto labels = co_await store_.getVertexLabels(vid);
            VertexValue vv;
            vv.id = vid;
            vv.labels = std::move(labels);
            if (props) {
                vv.properties[label_id_] = std::move(*props);
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

std::string IndexScanPhysicalOp::toString() const {
    std::string label_name = std::to_string(label_id_);
    std::string prop_name = std::to_string(prop_id_);
    auto it = label_defs_.find(label_id_);
    if (it != label_defs_.end()) {
        label_name = it->second.name;
        for (const auto& pd : it->second.properties) {
            if (pd.id == prop_id_) {
                prop_name = pd.name;
                break;
            }
        }
    }
    return "IndexScan(variable=" + variable_ + ", label=" + label_name + ", prop=" + prop_name + ", " +
           (mode_ == ScanMode::EQUALITY ? "EQ" : "RANGE") + ")";
}

} // namespace compute
} // namespace eugraph
