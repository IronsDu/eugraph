#include "query/physical_plan/operator/vertex_property_read_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

std::string VertexPropertyReadPhysicalOp::toString() const {
    std::string s = "VertexPropertyRead(var=" + variable_;
    if (!label_prop_ids_.empty()) {
        s += ", props={";
        bool first = true;
        for (const auto& [lid, pids] : label_prop_ids_) {
            if (!first)
                s += ", ";
            first = false;
            s += std::to_string(lid) + ":[";
            for (size_t j = 0; j < pids.size(); ++j) {
                if (j > 0)
                    s += ",";
                s += std::to_string(pids[j]);
            }
            s += "]";
        }
        s += "}";
    }
    s += ")";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> VertexPropertyReadPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        auto rows = chunk->toRows();
        size_t input_cols = chunk->numColumns();
        size_t row_count = rows.size();

        // Load properties for each row
        // TODO: batch co_await — collect all vids first, then single batch call per label
        for (size_t i = 0; i < row_count; ++i) {
            if (col_idx_ >= rows[i].size())
                continue;
            const auto& val = rows[i][col_idx_];
            VertexValue vv;
            if (std::holds_alternative<VertexRef>(val)) {
                vv.id = std::get<VertexRef>(val).id;
            } else if (std::holds_alternative<VertexValue>(val)) {
                vv = std::get<VertexValue>(val);
            } else {
                continue;
            }

            for (const auto& [lid, prop_ids] : label_prop_ids_) {
                auto props = co_await store_.getVertexProperties(vv.id, lid, prop_ids);
                if (props)
                    vv.properties[lid] = std::move(*props);
            }

            rows[i][col_idx_] = Value(std::move(vv));
        }

        // Build output DataChunk
        DataChunk output;
        output.columns.reserve(output_types_.size());

        for (size_t c = 0; c < input_cols; ++c) {
            if (c == col_idx_) {
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
