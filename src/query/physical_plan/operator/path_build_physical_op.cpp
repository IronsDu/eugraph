#include "query/physical_plan/operator/path_build_physical_op.hpp"

namespace eugraph {
namespace compute {

std::string PathBuildPhysicalOp::toString() const {
    std::string s = "PathBuild(path=" + path_var_ + ", elements=[";
    for (size_t i = 0; i < element_vars_.size(); ++i) {
        if (i > 0)
            s += ", ";
        s += element_vars_[i];
    }
    s += "])";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> PathBuildPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        const size_t input_cols = chunk->numColumns();
        const size_t row_count = chunk->numRows();

        DataChunk output;
        output.setSchema(output_types_);
        output.reserve(row_count);

        // Read cells straight from the chunk instead of materialising a
        // std::vector<Row> copy of it; every access below is a single indexed
        // read, which getValue(row) serves directly.
        for (size_t row_idx = 0; row_idx < row_count; ++row_idx) {
            PathTopology pt;
            VertexId last_vertex = INVALID_VERTEX_ID;

            auto append_vertex = [&](VertexId vid) {
                if (pt.vertex_ids.empty() || pt.vertex_ids.back() != vid) {
                    pt.vertex_ids.push_back(vid);
                }
                last_vertex = vid;
            };
            auto append_edge = [&](VertexId src, VertexId dst, EdgeId eid, EdgeLabelId elid, uint64_t seq) {
                pt.edge_ids.push_back(eid);
                pt.edge_label_ids.push_back(elid);
                pt.seqs.push_back(seq);
                pt.edge_src_ids.push_back(src);
                pt.edge_dst_ids.push_back(dst);
                VertexId neighbor = (last_vertex == INVALID_VERTEX_ID || src == last_vertex) ? dst : src;
                append_vertex(neighbor);
            };
            auto append_edge_value = [&](const Value& edge_value) {
                if (std::holds_alternative<EdgeKey>(edge_value)) {
                    const auto& ek = std::get<EdgeKey>(edge_value);
                    append_edge(ek.src_id, ek.dst_id, ek.id, ek.label_id, ek.seq);
                } else if (std::holds_alternative<EdgeValue>(edge_value)) {
                    const auto& ev = std::get<EdgeValue>(edge_value);
                    append_edge(ev.src_id, ev.dst_id, ev.id, ev.label_id, ev.seq);
                }
            };

            for (size_t i = 0; i < element_vars_.size(); ++i) {
                int col = element_cols_[i];
                if (col < 0 || static_cast<size_t>(col) >= input_cols)
                    continue;
                const Value& val = chunk->columns[col].getValue(row_idx);
                if (std::holds_alternative<VertexRef>(val)) {
                    append_vertex(std::get<VertexRef>(val).id);
                } else if (std::holds_alternative<VertexValue>(val)) {
                    append_vertex(std::get<VertexValue>(val).id);
                } else if (std::holds_alternative<ListValue>(val)) {
                    for (const auto& elem : std::get<ListValue>(val).elements)
                        append_edge_value(elem.value);
                } else {
                    append_edge_value(val);
                }
            }

            // Forward the input columns, then append the built path column.
            output.count = row_idx + 1;
            for (size_t c = 0; c < input_cols; ++c) {
                if (c >= output.columns.size())
                    break;
                output.columns[c].setValue(row_idx, chunk->columns[c].getValue(row_idx));
            }
            if (output.columns.size() > input_cols)
                output.columns[input_cols].setValue(row_idx, Value(std::move(pt)));
        }
        if (output.count > 0) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
