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
            for (size_t i = 0; i < element_vars_.size(); ++i) {
                int col = element_cols_[i];
                if (col >= 0 && static_cast<size_t>(col) < row.size()) {
                    const auto& val = row[col];
                    if (std::holds_alternative<VertexRef>(val)) {
                        pt.vertex_ids.push_back(std::get<VertexRef>(val).id);
                    } else if (std::holds_alternative<VertexValue>(val)) {
                        pt.vertex_ids.push_back(std::get<VertexValue>(val).id);
                    } else if (std::holds_alternative<EdgeKey>(val)) {
                        const auto& ek = std::get<EdgeKey>(val);
                        pt.edge_ids.push_back(ek.id);
                        pt.edge_label_ids.push_back(ek.label_id);
                        pt.seqs.push_back(ek.seq);
                        pt.edge_src_ids.push_back(ek.src_id);
                        pt.edge_dst_ids.push_back(ek.dst_id);
                    } else if (std::holds_alternative<EdgeValue>(val)) {
                        const auto& ev = std::get<EdgeValue>(val);
                        pt.edge_ids.push_back(ev.id);
                        pt.edge_label_ids.push_back(ev.label_id);
                        pt.seqs.push_back(ev.seq);
                        pt.edge_src_ids.push_back(ev.src_id);
                        pt.edge_dst_ids.push_back(ev.dst_id);
                    }
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
