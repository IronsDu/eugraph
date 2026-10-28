#include "query/physical_plan/operator/path_element_property_read_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

std::string PathElementPropertyReadPhysicalOp::toString() const {
    return "PathElementPropertyRead(path=" + path_variable_ + ")";
}

folly::coro::AsyncGenerator<DataChunk> PathElementPropertyReadPhysicalOp::executeChunk() {
    auto child_gen = cancellable(child_->executeChunk());

    while (auto chunk = co_await child_gen.next()) {
        const size_t input_cols = chunk->numColumns();
        const size_t row_count = chunk->numRows();

        std::vector<Value> path_out(row_count);
        std::vector<VertexId> pending_vids;
        std::unordered_map<VertexId, size_t> pending_pos;

        // Pass 1: build PathValue and collect every vertex that still needs
        // labels/properties. Loading them per element used one dispatch per
        // vertex (c14: ~90k); batch them into one IO dispatch per chunk.
        for (size_t i = 0; i < row_count; ++i) {
            if (path_col_idx_ >= input_cols)
                continue;
            Value val = chunk->columns[path_col_idx_].getValue(i);
            if (std::holds_alternative<PathTopologyPtr>(val)) {
                auto& pt = (*std::get<PathTopologyPtr>(val));
                PathValue pv;
                pv.elements.reserve(pt.vertexCount() + pt.hopCount());
                for (size_t j = 0; j < pt.vertex_ids.size(); ++j) {
                    ValueStorage ve;
                    ve.value = Value(mk<VertexValue>(VertexValue{pt.vertex_ids[j], {}, {}, false}));
                    pv.elements.push_back(std::move(ve));
                    if (j < pt.edge_ids.size()) {
                        VertexId src = j < pt.edge_src_ids.size() ? pt.edge_src_ids[j] : pt.vertex_ids[j];
                        VertexId dst = j < pt.edge_dst_ids.size() ? pt.edge_dst_ids[j] : pt.vertex_ids[j + 1];
                        ValueStorage ee;
                        ee.value = Value(mk<EdgeValue>(EdgeValue{pt.edge_ids[j], src, dst, pt.edge_label_ids[j],
                                                                 pt.seqs[j], std::nullopt, false}));
                        pv.elements.push_back(std::move(ee));
                    }
                }
                val = Value(mk<PathValue>(std::move(pv)));
            }
            if (!std::holds_alternative<PathValuePtr>(val)) {
                path_out[i] = std::move(val);
                continue;
            }
            auto& pv = (*std::get<PathValuePtr>(val));
            for (auto& elem : pv.elements) {
                if (!std::holds_alternative<VertexValuePtr>(elem.value))
                    continue;
                auto& vv = (*std::get<VertexValuePtr>(elem.value));
                if (vv.labels.has_value() && !vv.properties.empty())
                    continue;
                auto [it, inserted] = pending_pos.try_emplace(vv.id, pending_vids.size());
                if (inserted)
                    pending_vids.push_back(vv.id);
            }
            path_out[i] = std::move(val);
        }

        std::unordered_map<VertexId, ISyncGraphDataStore::VertexDataBatchEntry> vertex_data;
        if (!pending_vids.empty()) {
            auto batch = co_await store_.batchGetVertexData(pending_vids);
            vertex_data.reserve(pending_vids.size());
            for (size_t j = 0; j < pending_vids.size() && j < batch.size(); ++j)
                vertex_data.emplace(pending_vids[j], std::move(batch[j]));
        }

        // Pass 2: fill the path vertices from the batch, edges still use the
        // per-edge path (c14 has no edge-property reads on the hot path).
        for (size_t i = 0; i < row_count; ++i) {
            auto& val = path_out[i];
            if (!std::holds_alternative<PathValuePtr>(val))
                continue;
            auto& pv = (*std::get<PathValuePtr>(val));
            for (auto& elem : pv.elements) {
                if (std::holds_alternative<VertexValuePtr>(elem.value)) {
                    auto& vv = (*std::get<VertexValuePtr>(elem.value));
                    if (vv.labels.has_value() && !vv.properties.empty())
                        continue;
                    auto it = vertex_data.find(vv.id);
                    if (it == vertex_data.end())
                        continue;
                    if (!vv.labels.has_value())
                        vv.labels = it->second.labels;
                    if (vv.properties.empty())
                        vv.properties = it->second.properties;
                } else if (std::holds_alternative<EdgeValuePtr>(elem.value)) {
                    auto& ev = (*std::get<EdgeValuePtr>(elem.value));
                    if (!ev.properties.has_value() || ev.properties->empty()) {
                        auto props = co_await store_.getEdgeProperties(ev.label_id, ev.id);
                        if (props)
                            ev.properties = std::move(*props);
                    }
                }
            }
        }

        DataChunk output;
        output.columns.reserve(output_types_.size());

        for (size_t c = 0; c < input_cols; ++c) {
            if (c == path_col_idx_) {
                auto col = Column::flat(output_types_[c].kind, row_count);
                for (size_t i = 0; i < row_count; ++i)
                    col.setValue(i, path_out[i]);
                output.columns.push_back(std::move(col));
            } else {
                auto& src_col = chunk->columns[c];
                if (src_col.form == VectorForm::DICTIONARY && src_col.buffer) {
                    output.columns.push_back(Column::dict(src_col.buffer, SelectionVector(src_col.dict_sel)));
                } else if (src_col.form == VectorForm::FLAT && src_col.buffer) {
                    SelectionVector id_sel;
                    id_sel.is_identity = false;
                    id_sel.indices.reserve(row_count);
                    for (size_t i = 0; i < row_count; ++i)
                        id_sel.indices.push_back(static_cast<uint32_t>(i));
                    id_sel.count = row_count;
                    output.columns.push_back(Column::dict(src_col.buffer, id_sel));
                } else if (src_col.form == VectorForm::CONSTANT) {
                    output.columns.push_back(Column::constant(src_col.constant_value));
                } else {
                    output.columns.push_back(Column::flat(src_col.type, row_count));
                }
            }
        }

        output.count = row_count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
