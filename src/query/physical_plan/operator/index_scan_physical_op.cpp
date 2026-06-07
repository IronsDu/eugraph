#include "query/physical_plan/operator/index_scan_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"

namespace eugraph {
namespace compute {

// Single-property constructor
IndexScanPhysicalOp::IndexScanPhysicalOp(std::string variable, LabelId label_id, uint16_t prop_id, ScanMode mode,
                                         PropertyValue search_value, std::optional<PropertyValue> range_end,
                                         std::vector<binder::BoundType> output_types, IAsyncGraphDataStore& store,
                                         std::unordered_map<LabelId, LabelDef> label_defs)
    : variable_(std::move(variable)), label_id_(label_id), prop_ids_({prop_id}), mode_(mode), eq_values_(),
      range_start_(std::nullopt), range_end_(std::nullopt), output_types_(std::move(output_types)), store_(store),
      label_defs_(std::move(label_defs)) {
    if (mode == ScanMode::EQUALITY) {
        eq_values_.push_back(std::move(search_value));
    } else {
        range_start_ = std::vector<PropertyValue>{std::move(search_value)};
        if (range_end.has_value())
            range_end_ = std::vector<PropertyValue>{std::move(*range_end)};
    }
}

// Composite constructor
IndexScanPhysicalOp::IndexScanPhysicalOp(std::string variable, LabelId label_id, std::vector<uint16_t> prop_ids,
                                         ScanMode mode, std::vector<PropertyValue> eq_values,
                                         std::optional<std::vector<PropertyValue>> range_start,
                                         std::optional<std::vector<PropertyValue>> range_end,
                                         std::vector<binder::BoundType> output_types, IAsyncGraphDataStore& store,
                                         std::unordered_map<LabelId, LabelDef> label_defs)
    : variable_(std::move(variable)), label_id_(label_id), prop_ids_(std::move(prop_ids)), mode_(mode),
      eq_values_(std::move(eq_values)), range_start_(std::move(range_start)), range_end_(std::move(range_end)),
      output_types_(std::move(output_types)), store_(store), label_defs_(std::move(label_defs)) {}

folly::coro::AsyncGenerator<DataChunk> IndexScanPhysicalOp::executeChunk() {
    folly::coro::AsyncGenerator<std::vector<VertexId>> gen;
    if (mode_ == ScanMode::EQUALITY) {
        if (prop_ids_.size() == 1) {
            gen = store_.scanVerticesByIndex(label_id_, prop_ids_[0], eq_values_[0]);
        } else {
            gen = store_.scanVerticesByIndexComposite(label_id_, prop_ids_, eq_values_);
        }
    } else {
        if (prop_ids_.size() == 1) {
            std::optional<PropertyValue> start;
            std::optional<PropertyValue> end;
            if (range_start_.has_value())
                start = (*range_start_)[0];
            if (range_end_.has_value())
                end = (*range_end_)[0];
            gen = store_.scanVerticesByIndexRange(label_id_, prop_ids_[0], start, end);
        } else {
            gen = store_.scanVerticesByIndexRangeComposite(label_id_, prop_ids_, range_start_, range_end_);
        }
    }

    while (auto batch = co_await gen.next()) {
        DataChunk chunk;
        chunk.setSchema(output_types_);
        chunk.reserve(batch->size());

        for (VertexId vid : *batch) {
            VertexValue vv;
            vv.id = vid;
            chunk.appendRow({Value(std::move(vv))});
        }
        if (chunk.count > 0) {
            co_yield std::move(chunk);
        }
    }
}

std::string IndexScanPhysicalOp::toString() const {
    std::string label_name = std::to_string(label_id_);
    auto it = label_defs_.find(label_id_);
    if (it != label_defs_.end()) {
        label_name = it->second.name;
    }
    std::string prop_list;
    for (size_t i = 0; i < prop_ids_.size(); ++i) {
        if (i > 0)
            prop_list += ", ";
        if (it != label_defs_.end()) {
            bool found = false;
            for (const auto& pd : it->second.properties) {
                if (pd.id == prop_ids_[i]) {
                    prop_list += pd.name;
                    found = true;
                    break;
                }
            }
            if (!found)
                prop_list += std::to_string(prop_ids_[i]);
        } else {
            prop_list += std::to_string(prop_ids_[i]);
        }
    }
    std::string modestr = (mode_ == ScanMode::EQUALITY) ? "eq" : "range";
    return "IndexScan(variable=" + variable_ + ", label=" + label_name + ", props=[" + prop_list +
           "], mode=" + modestr + ")";
}

} // namespace compute
} // namespace eugraph
