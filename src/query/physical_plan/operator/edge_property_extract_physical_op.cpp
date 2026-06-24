#include "query/physical_plan/operator/edge_property_extract_physical_op.hpp"
#include "common/types/graph_types.hpp"
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
        size_t input_cols = chunk->numColumns();
        size_t row_count = chunk->numRows();

        // Build property columns first (need to read from chunk).
        std::vector<Column> prop_cols(edge_prop_ids_.size());
        for (size_t p = 0; p < edge_prop_ids_.size(); ++p) {
            auto kind = output_types_[input_cols + p].kind;
            prop_cols[p] = Column::flat(kind, row_count);
        }

        // Row-major: load properties, write directly to output columns.
        EdgeId cached_eid = INVALID_EDGE_ID;
        EdgeLabelId cached_label = INVALID_EDGE_LABEL_ID;
        size_t cached_col = SIZE_MAX;
        for (size_t row = 0; row < row_count; ++row) {
            for (size_t p = 0; p < edge_prop_ids_.size(); ++p) {
                EdgeId eid;
                EdgeLabelId elid;
                if (col_idx_ == cached_col) {
                    eid = cached_eid;
                    elid = cached_label;
                } else {
                    auto v = chunk->columns[col_idx_].getValue(row);
                    if (std::holds_alternative<EdgeKey>(v)) {
                        auto& ek = std::get<EdgeKey>(v);
                        eid = ek.id;
                        elid = ek.label_id;
                    } else if (std::holds_alternative<EdgeValue>(v)) {
                        auto& ev = std::get<EdgeValue>(v);
                        eid = ev.id;
                        elid = ev.label_id;
                    } else {
                        eid = INVALID_EDGE_ID;
                        elid = INVALID_EDGE_LABEL_ID;
                    }
                    cached_eid = eid;
                    cached_label = elid;
                    cached_col = col_idx_;
                }
                if (eid == INVALID_EDGE_ID)
                    continue;

                auto pv = co_await store_.getEdgeProperty(elid, eid, edge_prop_ids_[p]);
                if (pv.has_value())
                    prop_cols[p].setValue(row, propertyValueToValue(*pv));
            }
        }

        // Move upstream columns, append property columns.
        DataChunk output;
        output.columns = std::move(chunk->columns);
        for (auto& pc : prop_cols)
            output.columns.push_back(std::move(pc));
        output.count = row_count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
