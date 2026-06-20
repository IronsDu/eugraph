#include "query/physical_plan/operator/path_element_property_read_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

std::string PathElementPropertyReadPhysicalOp::toString() const {
    return "PathElementPropertyRead(path=" + path_variable_ + ")";
}

folly::coro::AsyncGenerator<DataChunk> PathElementPropertyReadPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        auto rows = chunk->toRows();
        size_t input_cols = chunk->numColumns();
        size_t row_count = rows.size();

        for (size_t i = 0; i < row_count; ++i) {
            if (path_col_idx_ >= rows[i].size())
                continue;
            auto& val = rows[i][path_col_idx_];
            // Phase D: accept both PathTopology (topology-stage) and PathValue
            // (legacy RBO path). Upgrade PathTopology to PathValue in-place.
            if (std::holds_alternative<PathTopology>(val)) {
                auto& pt = std::get<PathTopology>(val);
                PathValue pv;
                pv.elements.reserve(pt.vertexCount() + pt.hopCount());
                for (size_t j = 0; j < pt.vertex_ids.size(); ++j) {
                    ValueStorage ve;
                    ve.value = VertexValue{pt.vertex_ids[j], {}, {}, false};
                    pv.elements.push_back(std::move(ve));
                    if (j < pt.edge_ids.size()) {
                        ValueStorage ee;
                        ee.value = EdgeValue{pt.edge_ids[j],
                                             pt.vertex_ids[j],
                                             pt.vertex_ids[j + 1],
                                             pt.edge_label_ids[j],
                                             pt.seqs[j],
                                             std::nullopt,
                                             false};
                        pv.elements.push_back(std::move(ee));
                    }
                }
                rows[i][path_col_idx_] = Value(std::move(pv));
            }
            if (!std::holds_alternative<PathValue>(val))
                continue;
            auto& pv = std::get<PathValue>(val);

            for (auto& elem : pv.elements) {
                if (std::holds_alternative<VertexValue>(elem.value)) {
                    auto& vv = std::get<VertexValue>(elem.value);
                    if (vv.properties.empty()) {
                        auto labels = co_await store_.getVertexLabels(vv.id);
                        for (auto lid : labels) {
                            auto props = co_await store_.getVertexProperties(vv.id, lid);
                            if (props)
                                vv.properties[lid] = std::move(*props);
                        }
                    }
                } else if (std::holds_alternative<EdgeValue>(elem.value)) {
                    auto& ev = std::get<EdgeValue>(elem.value);
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
                    col.setValue(i, rows[i][c]);
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
