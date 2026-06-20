#include "query/physical_plan/operator/path_property_extract_physical_op.hpp"

#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"

namespace eugraph {
namespace compute {

std::string PathPropertyExtractPhysicalOp::toString() const {
    std::string s = "PathPropertyExtract(path=" + path_variable_;
    if (!vertex_prop_list_.empty()) {
        s += ", vprops=[";
        for (size_t i = 0; i < vertex_prop_list_.size(); ++i) {
            if (i > 0)
                s += ",";
            s += std::to_string(vertex_prop_list_[i].first) + "." + std::to_string(vertex_prop_list_[i].second);
        }
        s += "]";
    }
    if (!edge_prop_list_.empty()) {
        s += ", eprops=[";
        for (size_t i = 0; i < edge_prop_list_.size(); ++i) {
            if (i > 0)
                s += ",";
            s += std::to_string(edge_prop_list_[i].first) + "." + std::to_string(edge_prop_list_[i].second);
        }
        s += "]";
    }
    s += ")";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> PathPropertyExtractPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        auto rows = chunk->toRows();
        size_t input_cols = chunk->numColumns();
        size_t row_count = rows.size();

        // Collect all (vid, label_id, prop_id) queries and (eid, label_id, prop_id) queries.
        struct VidQuery {
            size_t row;
            size_t vpos;
            VertexId vid;
        };
        struct EidQuery {
            size_t row;
            size_t epos;
            EdgeId eid;
            EdgeLabelId elid;
        };
        std::vector<VidQuery> vqueries;
        std::vector<EidQuery> equeries;

        // Flatten: for each row, for each property, collect all vertex/edge IDs.
        std::vector<std::vector<VertexId>> row_vids(row_count);
        std::vector<std::vector<EdgeId>> row_eids(row_count);
        std::vector<std::vector<EdgeLabelId>> row_elids(row_count);

        for (size_t i = 0; i < row_count; ++i) {
            if (path_col_idx_ >= rows[i].size())
                continue;
            const auto& val = rows[i][path_col_idx_];
            if (std::holds_alternative<PathTopology>(val)) {
                const auto& pt = std::get<PathTopology>(val);
                row_vids[i] = pt.vertex_ids;
                row_eids[i] = pt.edge_ids;
                row_elids[i] = pt.edge_label_ids;
            } else if (std::holds_alternative<PathValue>(val)) {
                const auto& pv = std::get<PathValue>(val);
                for (const auto& elem : pv.elements) {
                    if (std::holds_alternative<VertexValue>(elem.value))
                        row_vids[i].push_back(std::get<VertexValue>(elem.value).id);
                    else if (std::holds_alternative<EdgeValue>(elem.value)) {
                        const auto& ev = std::get<EdgeValue>(elem.value);
                        row_eids[i].push_back(ev.id);
                        row_elids[i].push_back(ev.label_id);
                    }
                }
            }
        }

        // Load vertex properties.
        std::vector<std::vector<std::vector<Value>>> vprops_per_row(row_count);
        vprops_per_row.resize(row_count);
        for (size_t p = 0; p < vertex_prop_list_.size(); ++p) {
            auto [lid, pid] = vertex_prop_list_[p];
            for (size_t i = 0; i < row_count; ++i) {
                vprops_per_row[i].resize(vertex_prop_list_.size());
                vprops_per_row[i][p].resize(row_vids[i].size());
                for (size_t j = 0; j < row_vids[i].size(); ++j) {
                    auto pv = co_await store_.getVertexProperty(row_vids[i][j], lid, pid);
                    if (pv.has_value())
                        vprops_per_row[i][p][j] = propertyValueToValue(*pv);
                }
            }
        }

        // Load edge properties.
        std::vector<std::vector<std::vector<Value>>> eprops_per_row(row_count);
        eprops_per_row.resize(row_count);
        for (size_t p = 0; p < edge_prop_list_.size(); ++p) {
            auto [lid, pid] = edge_prop_list_[p];
            for (size_t i = 0; i < row_count; ++i) {
                eprops_per_row[i].resize(edge_prop_list_.size());
                eprops_per_row[i][p].resize(row_eids[i].size());
                for (size_t j = 0; j < row_eids[i].size(); ++j) {
                    auto pv = co_await store_.getEdgeProperty(row_elids[i][j], row_eids[i][j], pid);
                    if (pv.has_value())
                        eprops_per_row[i][p][j] = propertyValueToValue(*pv);
                }
            }
        }

        // Build output DataChunk.
        DataChunk output;
        output.columns.reserve(output_types_.size());

        for (size_t c = 0; c < input_cols; ++c) {
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

        // Append vertex property List columns.
        for (size_t p = 0; p < vertex_prop_list_.size(); ++p) {
            auto col = Column::flat(binder::BoundTypeKind::LIST, row_count);
            for (size_t i = 0; i < row_count; ++i) {
                ListValue lv;
                for (auto& v : vprops_per_row[i][p])
                    lv.elements.push_back(ValueStorage{std::move(v)});
                col.setValue(i, Value(std::move(lv)));
            }
            output.columns.push_back(std::move(col));
        }

        // Append edge property List columns.
        for (size_t p = 0; p < edge_prop_list_.size(); ++p) {
            auto col = Column::flat(binder::BoundTypeKind::LIST, row_count);
            for (size_t i = 0; i < row_count; ++i) {
                ListValue lv;
                for (auto& v : eprops_per_row[i][p])
                    lv.elements.push_back(ValueStorage{std::move(v)});
                col.setValue(i, Value(std::move(lv)));
            }
            output.columns.push_back(std::move(col));
        }

        output.count = row_count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
