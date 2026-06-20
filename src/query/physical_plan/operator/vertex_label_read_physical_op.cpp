#include "query/physical_plan/operator/vertex_label_read_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

std::string VertexLabelReadPhysicalOp::toString() const {
    return "VertexLabelRead(var=" + variable_ + ")";
}

folly::coro::AsyncGenerator<DataChunk> VertexLabelReadPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        auto rows = chunk->toRows();
        size_t input_cols = chunk->numColumns();
        size_t row_count = rows.size();

        // Load labels for each row
        // TODO: batch co_await — collect all vids first, then single batch call
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

            auto labels = co_await store_.getVertexLabels(vv.id);
            if (anon_label_id_ != INVALID_LABEL_ID)
                labels.erase(anon_label_id_);
            vv.labels = std::move(labels);

            rows[i][col_idx_] = Value(std::move(vv));
        }

        // Build output DataChunk: passthrough columns as DICTIONARY, enriched column as FLAT
        DataChunk output;
        output.columns.reserve(output_types_.size());

        for (size_t c = 0; c < input_cols; ++c) {
            if (c == col_idx_) {
                // Enriched vertex column: new FLAT
                auto col = Column::flat(output_types_[c].kind, row_count);
                for (size_t i = 0; i < row_count; ++i)
                    col.setValue(i, rows[i][c]);
                output.columns.push_back(std::move(col));
            } else {
                auto& src_col = chunk->columns[c];
                if (src_col.form == VectorForm::DICTIONARY && src_col.buffer) {
                    // Preserve original dict_sel — the buffer may have fewer
                    // physical rows than our logical row_count
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
