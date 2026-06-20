#include "query/physical_plan/operator/vertex_property_extract_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"

namespace eugraph {
namespace compute {

std::string VertexPropertyExtractPhysicalOp::toString() const {
    std::string s = "VertexPropertyExtract(var=" + variable_ + ", props=[";
    bool first = true;
    for (const auto& [lid, pids] : label_prop_ids_) {
        for (uint16_t pid : pids) {
            if (!first)
                s += ",";
            first = false;
            s += std::to_string(lid) + "." + std::to_string(pid);
        }
    }
    s += "])";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> VertexPropertyExtractPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        auto rows = chunk->toRows();
        size_t input_cols = chunk->numColumns();
        size_t row_count = rows.size();

        std::vector<VertexId> vids(row_count, INVALID_VERTEX_ID);
        for (size_t i = 0; i < row_count; ++i) {
            if (col_idx_ >= rows[i].size())
                continue;
            const auto& val = rows[i][col_idx_];
            if (std::holds_alternative<VertexRef>(val))
                vids[i] = std::get<VertexRef>(val).id;
            else if (std::holds_alternative<VertexValue>(val))
                vids[i] = std::get<VertexValue>(val).id;
        }

        std::vector<std::vector<Value>> prop_values(prop_list_.size());
        for (size_t p = 0; p < prop_list_.size(); ++p) {
            prop_values[p].resize(row_count);
            auto [lid, pid] = prop_list_[p];
            for (size_t i = 0; i < row_count; ++i) {
                if (vids[i] == INVALID_VERTEX_ID)
                    continue;
                auto pv = co_await store_.getVertexProperty(vids[i], lid, pid);
                if (pv.has_value())
                    prop_values[p][i] = propertyValueToValue(*pv);
            }
        }

        DataChunk output;
        output.columns.reserve(output_types_.size());
        for (size_t c = 0; c < input_cols; ++c) {
            auto& src_col = chunk->columns[c];
            if (src_col.form == VectorForm::DICTIONARY && src_col.buffer)
                output.columns.push_back(Column::dict(src_col.buffer, SelectionVector(src_col.dict_sel)));
            else if (src_col.form == VectorForm::FLAT && src_col.buffer) {
                SelectionVector id_sel;
                id_sel.is_identity = false;
                id_sel.indices.reserve(row_count);
                for (size_t i = 0; i < row_count; ++i)
                    id_sel.indices.push_back(static_cast<uint32_t>(i));
                id_sel.count = row_count;
                output.columns.push_back(Column::dict(src_col.buffer, id_sel));
            } else if (src_col.form == VectorForm::CONSTANT)
                output.columns.push_back(Column::constant(src_col.constant_value));
            else
                output.columns.push_back(Column::flat(src_col.type, row_count));
        }

        for (size_t p = 0; p < prop_list_.size(); ++p) {
            auto col_idx = input_cols + p;
            auto kind = binder::BoundTypeKind::ANY;
            if (col_idx < output_types_.size())
                kind = output_types_[col_idx].kind;
            else {
                // Infer type from the first non-empty value at runtime.
                for (size_t i = 0; i < row_count; ++i) {
                    const auto& v = prop_values[p][i];
                    if (std::holds_alternative<bool>(v)) {
                        kind = binder::BoundTypeKind::BOOL;
                        break;
                    }
                    if (std::holds_alternative<int64_t>(v)) {
                        kind = binder::BoundTypeKind::INT64;
                        break;
                    }
                    if (std::holds_alternative<double>(v)) {
                        kind = binder::BoundTypeKind::DOUBLE;
                        break;
                    }
                    if (std::holds_alternative<std::string>(v)) {
                        kind = binder::BoundTypeKind::STRING;
                        break;
                    }
                    if (std::holds_alternative<ListValue>(v)) {
                        kind = binder::BoundTypeKind::LIST;
                        break;
                    }
                }
            }
            auto col = Column::flat(kind, row_count);
            for (size_t i = 0; i < row_count; ++i)
                col.setValue(i, prop_values[p][i]);
            output.columns.push_back(std::move(col));
        }

        output.count = row_count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
