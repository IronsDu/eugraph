#include "query/physical_plan/operator/edge_property_read_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

std::string EdgePropertyReadPhysicalOp::toString() const {
    std::string s = "EdgePropertyRead(var=" + variable_;
    if (!edge_prop_ids_.empty()) {
        s += ", props=[";
        for (size_t i = 0; i < edge_prop_ids_.size(); ++i) {
            if (i > 0)
                s += ",";
            s += std::to_string(edge_prop_ids_[i]);
        }
        s += "]";
    }
    s += ")";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> EdgePropertyReadPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        auto rows = chunk->toRows();
        size_t input_cols = chunk->numColumns();
        size_t row_count = rows.size();

        // Load edge properties for each row
        // TODO: batch co_await
        for (size_t i = 0; i < row_count; ++i) {
            if (col_idx_ >= rows[i].size())
                continue;
            const auto& val = rows[i][col_idx_];
            if (!std::holds_alternative<EdgeValue>(val))
                continue;
            EdgeValue ev = std::get<EdgeValue>(val);

            if (!edge_prop_ids_.empty()) {
                auto props = co_await store_.getEdgeProperties(ev.label_id, ev.id, edge_prop_ids_);
                if (props)
                    ev.properties = std::move(*props);
            }

            rows[i][col_idx_] = Value(std::move(ev));
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
