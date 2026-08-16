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
        auto rows = chunk->toRows();

        DataChunk output;
        output.setSchema(output_types_);
        output.reserve(rows.size());

        for (auto& row : rows) {
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
                if (col < 0 || static_cast<size_t>(col) >= row.size())
                    continue;
                const auto& val = row[col];
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

            std::vector<Value> out_values;
            out_values.reserve(row.size() + 1);
            for (auto& val : row) {
                out_values.push_back(std::move(val));
            }
            out_values.push_back(Value(std::move(pt)));
            output.appendRow(out_values);
        }
        if (output.count > 0) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
