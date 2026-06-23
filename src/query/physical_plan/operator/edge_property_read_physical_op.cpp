#include "query/physical_plan/operator/edge_property_read_physical_op.hpp"
#include "common/types/graph_types.hpp"

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
        size_t row_count = chunk->numRows();

        // Build the enriched column first (need to read from chunk).
        auto kind = output_types_[col_idx_].kind;
        auto enriched = Column::flat(kind, row_count);

        // Row-major: load properties, write directly to enriched column.
        for (size_t row = 0; row < row_count; ++row) {
            auto v = chunk->columns[col_idx_].getValue(row);
            EdgeValue ev;
            if (std::holds_alternative<EdgeKey>(v)) {
                auto& ek = std::get<EdgeKey>(v);
                ev.id = ek.id;
                ev.src_id = ek.src_id;
                ev.dst_id = ek.dst_id;
                ev.label_id = ek.label_id;
                ev.seq = ek.seq;
            } else if (std::holds_alternative<EdgeValue>(v)) {
                ev = std::get<EdgeValue>(v);
            } else
                continue;

            if (!edge_prop_ids_.empty()) {
                auto props = co_await store_.getEdgeProperties(ev.label_id, ev.id, edge_prop_ids_);
                if (props)
                    ev.properties = std::move(*props);
            }

            enriched.setValue(row, Value(std::move(ev)));
        }

        // Move upstream columns, replace target column.
        DataChunk output;
        output.columns = std::move(chunk->columns);
        output.columns[col_idx_] = std::move(enriched);
        output.count = row_count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
