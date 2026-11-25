#include "query/physical_plan/operator/index_scan_physical_op.hpp"

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include <stdexcept>

namespace eugraph {
namespace compute {

// Single-property constructor
IndexScanPhysicalOp::IndexScanPhysicalOp(std::string variable, LabelId label_id, uint16_t prop_id, ScanMode mode,
                                         PropertyValue search_value, std::optional<PropertyValue> range_end,
                                         std::vector<binder::BoundType> output_types, IAsyncGraphDataStore& store,
                                         std::shared_ptr<const GraphSchema> schema, size_t prefix_col)
    : variable_(std::move(variable)), label_id_(label_id), prop_ids_({prop_id}), mode_(mode), eq_values_(),
      prefix_col_(prefix_col), range_start_(std::nullopt), range_end_(std::nullopt),
      output_types_(std::move(output_types)), store_(store), schema_(std::move(schema)) {
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
                                         std::shared_ptr<const GraphSchema> schema, size_t prefix_col)
    : variable_(std::move(variable)), label_id_(label_id), index_id_(0), prop_ids_(std::move(prop_ids)), mode_(mode),
      eq_values_(std::move(eq_values)), prefix_col_(prefix_col), range_start_(std::move(range_start)),
      range_end_(std::move(range_end)), output_types_(std::move(output_types)), store_(store),
      schema_(std::move(schema)) {}

// Index-id based composite constructor
IndexScanPhysicalOp::IndexScanPhysicalOp(std::string variable, uint32_t index_id, std::vector<uint16_t> prop_ids,
                                         ScanMode mode, std::vector<PropertyValue> eq_values,
                                         std::optional<std::vector<PropertyValue>> range_start,
                                         std::optional<std::vector<PropertyValue>> range_end,
                                         std::vector<binder::BoundType> output_types, IAsyncGraphDataStore& store,
                                         std::shared_ptr<const GraphSchema> schema, size_t prefix_col)
    : variable_(std::move(variable)), label_id_(INVALID_LABEL_ID), index_id_(index_id), prop_ids_(std::move(prop_ids)),
      mode_(mode), eq_values_(std::move(eq_values)), prefix_col_(prefix_col), range_start_(std::move(range_start)),
      range_end_(std::move(range_end)), output_types_(std::move(output_types)), store_(store),
      schema_(std::move(schema)) {}

folly::coro::AsyncGenerator<DataChunk> IndexScanPhysicalOp::executeChunk() {
    // 读准入：DROP 会等 readers 归零后才删表（设计 §9.3）。
    if (index_id_ != 0) {
        if (auto* registry = store_.indexRegistry()) {
            read_handle_ = registry->acquireRead(index_id_);
            if (!read_handle_)
                throw std::runtime_error("index is being dropped (read admission failed)");
        }
    }
    std::vector<VertexId> scan_batch;
    folly::coro::AsyncGenerator<std::span<const VertexId>> gen;
    if (index_id_ != 0) {
        if (mode_ == ScanMode::EQUALITY) {
            gen = cancellable(store_.scanVerticesByIndexId(index_id_, eq_values_, scan_batch));
        } else if (mode_ == ScanMode::PREFIX) {
            gen = cancellable(store_.scanVerticesByIndexIdPrefix(index_id_, eq_values_, prefix_col_, scan_batch));
        } else {
            gen = cancellable(store_.scanVerticesByIndexIdRange(index_id_, range_start_, range_end_, scan_batch));
        }
    } else if (mode_ == ScanMode::EQUALITY) {
        if (prop_ids_.size() == 1) {
            gen = cancellable(store_.scanVerticesByIndex(label_id_, prop_ids_[0], eq_values_[0], scan_batch));
        } else {
            gen = cancellable(store_.scanVerticesByIndexComposite(label_id_, prop_ids_, eq_values_, scan_batch));
        }
    } else if (mode_ == ScanMode::PREFIX) {
        if (prop_ids_.size() == 1) {
            gen = cancellable(store_.scanVerticesByIndexPrefix(label_id_, prop_ids_[0], eq_values_[0], scan_batch));
        } else {
            gen = cancellable(
                store_.scanVerticesByIndexCompositePrefix(label_id_, prop_ids_, eq_values_, prefix_col_, scan_batch));
        }
    } else {
        if (prop_ids_.size() == 1) {
            std::optional<PropertyValue> start;
            std::optional<PropertyValue> end;
            if (range_start_.has_value())
                start = (*range_start_)[0];
            if (range_end_.has_value())
                end = (*range_end_)[0];
            gen = cancellable(store_.scanVerticesByIndexRange(label_id_, prop_ids_[0], start, end, scan_batch));
        } else {
            gen = cancellable(
                store_.scanVerticesByIndexRangeComposite(label_id_, prop_ids_, range_start_, range_end_, scan_batch));
        }
    }

    while (auto batch = co_await gen.next()) {
        DataChunk chunk;
        chunk.setSchema(output_types_);
        chunk.reserve(batch->size());
        // 单列 VERTEX_REF：列取一次复用，行内不再做 columns[0]（chunk 每批重建，引用随之更新）
        Column& out = chunk.columns[0];

        for (VertexId vid : *batch) {
            chunk.appendVertexRefRow(out, vid);
        }
        if (chunk.count > 0) {
            chunk.sel = SelectionVector::identity(chunk.count);
            co_yield std::move(chunk);
        }
    }
}

std::string IndexScanPhysicalOp::toString() const {
    std::string label_name = std::to_string(label_id_);
    auto it = schema_->labels.find(label_id_);
    if (it != schema_->labels.end()) {
        label_name = it->second.name;
    }
    std::string prop_list;
    for (size_t i = 0; i < prop_ids_.size(); ++i) {
        if (i > 0)
            prop_list += ", ";
        if (it != schema_->labels.end()) {
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
    std::string modestr = (mode_ == ScanMode::EQUALITY) ? "eq" : (mode_ == ScanMode::PREFIX ? "prefix" : "range");
    return "IndexScan(variable=" + variable_ + ", label=" + label_name + ", props=[" + prop_list +
           "], mode=" + modestr + ")";
}

} // namespace compute
} // namespace eugraph

namespace eugraph {
namespace compute {

void IndexScanPhysicalOp::explain(ExplainNode& node, const ExplainContext& ctx) const {
    ExplainContext local = ctx;
    if (schema_ != nullptr)
        local.schema = schema_.get();
    node.operator_name = "IndexScan";
    node.details.push_back("variable=" + explainVariableName(variable_));

    LabelId effective_label = label_id_;
    std::vector<uint16_t> effective_props = prop_ids_;
    std::string index_name;
    std::vector<std::string> accessor_names;
    if (schema_ != nullptr) {
        if (effective_label == INVALID_LABEL_ID && index_id_ != 0) {
            for (const auto& [lid, def] : schema_->labels) {
                for (const auto& idx : def.indexes) {
                    if (idx.index_id != index_id_)
                        continue;
                    effective_label = lid;
                    if (effective_props.empty())
                        effective_props = idx.prop_ids;
                    index_name = idx.name;
                    for (const auto& acc : idx.accessors) {
                        if (!acc.property_name.empty())
                            accessor_names.push_back(acc.property_name);
                    }
                    break;
                }
                if (effective_label != INVALID_LABEL_ID)
                    break;
            }
        } else {
            auto it = schema_->labels.find(effective_label);
            if (it != schema_->labels.end()) {
                for (const auto& idx : it->second.indexes) {
                    if (idx.index_id != index_id_)
                        continue;
                    if (effective_props.empty())
                        effective_props = idx.prop_ids;
                    index_name = idx.name;
                    for (const auto& acc : idx.accessors) {
                        if (!acc.property_name.empty())
                            accessor_names.push_back(acc.property_name);
                    }
                    break;
                }
            }
        }
    }
    node.details.push_back("label=" + explainLabelName(effective_label, local));
    if (!effective_props.empty()) {
        node.details.push_back("properties=[" + explainPropertyList(effective_label, effective_props, local) + "]");
    } else if (!accessor_names.empty()) {
        std::string props;
        for (size_t i = 0; i < accessor_names.size(); ++i) {
            if (i > 0)
                props += ", ";
            props += accessor_names[i];
        }
        node.details.push_back("properties=[" + props + "]");
    }
    node.details.push_back(std::string("mode=") + (mode_ == ScanMode::EQUALITY ? "eq"
                                                   : mode_ == ScanMode::PREFIX ? "prefix"
                                                                               : "range"));
    std::string key_desc;
    for (size_t i = 0; i < eq_values_.size(); ++i) {
        if (i > 0)
            key_desc += ", ";
        key_desc += formatExplainPropertyValue(eq_values_[i]);
    }
    if (!key_desc.empty()) {
        node.details.push_back("keyValues=[" + key_desc + "]");
    } else if (range_start_.has_value() || range_end_.has_value()) {
        std::string start = range_start_.has_value() && !range_start_->empty()
                                ? formatExplainPropertyValue(range_start_->front())
                                : "-inf";
        std::string end =
            range_end_.has_value() && !range_end_->empty() ? formatExplainPropertyValue(range_end_->front()) : "+inf";
        node.details.push_back("range=[" + start + ".." + end + "]");
    }
    if (!index_name.empty())
        node.details.push_back("index=" + index_name);
    else if (index_id_ != 0)
        node.details.push_back("index_id=" + std::to_string(index_id_));
}

} // namespace compute
} // namespace eugraph
