#include "compute_service/physical_plan/operator/edge_index_scan_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

// Single-property constructor
EdgeIndexScanPhysicalOp::EdgeIndexScanPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var,
                                                 EdgeLabelId label_id, uint16_t prop_id, ScanMode mode,
                                                 PropertyValue search_value, std::optional<PropertyValue> range_end,
                                                 IAsyncGraphDataStore& store,
                                                 std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs)
    : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), edge_var_(std::move(edge_var)),
      label_id_(label_id), prop_ids_({prop_id}), mode_(mode), eq_values_(), range_start_(std::nullopt),
      range_end_(std::nullopt), store_(store), edge_label_defs_(std::move(edge_label_defs)) {
    if (mode == ScanMode::EQUALITY) {
        eq_values_.push_back(std::move(search_value));
    } else {
        range_start_ = std::vector<PropertyValue>{std::move(search_value)};
        if (range_end.has_value())
            range_end_ = std::vector<PropertyValue>{std::move(*range_end)};
    }
}

// Composite constructor
EdgeIndexScanPhysicalOp::EdgeIndexScanPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var,
                                                 EdgeLabelId label_id, std::vector<uint16_t> prop_ids, ScanMode mode,
                                                 std::vector<PropertyValue> eq_values,
                                                 std::optional<std::vector<PropertyValue>> range_start,
                                                 std::optional<std::vector<PropertyValue>> range_end,
                                                 IAsyncGraphDataStore& store,
                                                 std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs)
    : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), edge_var_(std::move(edge_var)),
      label_id_(label_id), prop_ids_(std::move(prop_ids)), mode_(mode), eq_values_(std::move(eq_values)),
      range_start_(std::move(range_start)), range_end_(std::move(range_end)), store_(store),
      edge_label_defs_(std::move(edge_label_defs)) {}

folly::coro::AsyncGenerator<RowBatch> EdgeIndexScanPhysicalOp::execute() {
    folly::coro::AsyncGenerator<std::vector<EdgeIndexScanEntry>> gen;
    if (mode_ == ScanMode::EQUALITY) {
        if (prop_ids_.size() == 1) {
            gen = store_.scanEdgesByIndex(label_id_, prop_ids_[0], eq_values_[0]);
        } else {
            gen = store_.scanEdgesByIndexComposite(label_id_, prop_ids_, eq_values_);
        }
    } else {
        if (prop_ids_.size() == 1) {
            std::optional<PropertyValue> start;
            std::optional<PropertyValue> end;
            if (range_start_.has_value()) start = (*range_start_)[0];
            if (range_end_.has_value()) end = (*range_end_)[0];
            gen = store_.scanEdgesByIndexRange(label_id_, prop_ids_[0], start, end);
        } else {
            gen = store_.scanEdgesByIndexRangeComposite(label_id_, prop_ids_, range_start_, range_end_);
        }
    }

    while (auto batch = co_await gen.next()) {
        RowBatch output;
        for (const auto& entry : *batch) {
            // Build source vertex value
            VertexValue src_vv;
            src_vv.id = entry.src_id;
            auto src_labels = co_await store_.getVertexLabels(entry.src_id);
            src_vv.labels = src_labels;

            // Build destination vertex value
            VertexValue dst_vv;
            dst_vv.id = entry.dst_id;
            auto dst_labels = co_await store_.getVertexLabels(entry.dst_id);
            dst_vv.labels = dst_labels;

            // Build edge value
            EdgeValue ev;
            ev.id = entry.edge_id;
            ev.src_id = entry.src_id;
            ev.dst_id = entry.dst_id;
            ev.label_id = entry.label_id;

            Row row;
            if (!src_var_.empty()) row.push_back(Value(std::move(src_vv)));
            if (!dst_var_.empty()) row.push_back(Value(std::move(dst_vv)));
            if (!edge_var_.empty()) row.push_back(Value(std::move(ev)));
            output.push_back(std::move(row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

std::string EdgeIndexScanPhysicalOp::toString() const {
    std::string label_name = std::to_string(label_id_);
    auto it = edge_label_defs_.find(label_id_);
    if (it != edge_label_defs_.end()) {
        label_name = it->second.name;
    }
    std::string prop_list;
    if (it != edge_label_defs_.end()) {
        for (size_t i = 0; i < prop_ids_.size(); ++i) {
            if (i > 0) prop_list += ", ";
            bool found = false;
            for (const auto& pd : it->second.properties) {
                if (pd.id == prop_ids_[i]) {
                    prop_list += pd.name;
                    found = true;
                    break;
                }
            }
            if (!found) prop_list += std::to_string(prop_ids_[i]);
        }
    }
    return "EdgeIndexScan(src=" + src_var_ + ", dst=" + dst_var_ + ", edge=" + edge_var_ +
           ", label=" + label_name + ", props=[" + prop_list + "], " +
           (mode_ == ScanMode::EQUALITY ? "EQ" : "RANGE") + ")";
}

} // namespace compute
} // namespace eugraph
