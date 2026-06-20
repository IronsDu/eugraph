#include "query/physical_plan/operator/edge_property_extract_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"

namespace eugraph {
namespace compute {

std::string EdgePropertyExtractPhysicalOp::toString() const {
    std::string s = "EdgePropertyExtract(var=" + variable_ + ", props=[";
    for (size_t i = 0; i < edge_prop_ids_.size(); ++i) {
        if (i > 0)
            s += ",";
        s += std::to_string(edge_prop_ids_[i]);
    }
    s += "])";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> EdgePropertyExtractPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        auto rows = chunk->toRows();
        size_t input_cols = chunk->numColumns();
        size_t row_count = rows.size();

        struct EdgeQuery {
            EdgeId id;
            EdgeLabelId label_id;
        };
        std::vector<EdgeQuery> queries(row_count, {INVALID_EDGE_ID, INVALID_EDGE_LABEL_ID});
        for (size_t i = 0; i < row_count; ++i) {
            if (col_idx_ >= rows[i].size())
                continue;
            const auto& val = rows[i][col_idx_];
            if (std::holds_alternative<EdgeKey>(val)) {
                const auto& ek = std::get<EdgeKey>(val);
                queries[i] = {ek.id, ek.label_id};
            } else if (std::holds_alternative<EdgeValue>(val)) {
                const auto& ev = std::get<EdgeValue>(val);
                queries[i] = {ev.id, ev.label_id};
            }
        }

        std::vector<std::vector<Value>> prop_values(edge_prop_ids_.size());
        for (size_t p = 0; p < edge_prop_ids_.size(); ++p) {
            prop_values[p].resize(row_count);
            uint16_t pid = edge_prop_ids_[p];
            for (size_t i = 0; i < row_count; ++i) {
                if (queries[i].id == INVALID_EDGE_ID)
                    continue;
                auto pv = co_await store_.getEdgeProperty(queries[i].label_id, queries[i].id, pid);
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

        for (size_t p = 0; p < edge_prop_ids_.size(); ++p) {
            auto col_idx = input_cols + p;
            auto kind = binder::BoundTypeKind::ANY;
            if (col_idx < output_types_.size())
                kind = output_types_[col_idx].kind;
            else {
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
