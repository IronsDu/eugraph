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
            PathValue pv;
            for (size_t i = 0; i < element_vars_.size(); ++i) {
                int col = element_cols_[i];
                if (col >= 0 && static_cast<size_t>(col) < row.size()) {
                    pv.elements.push_back(ValueStorage{row[col]});
                }
            }

            std::vector<Value> out_values;
            out_values.reserve(row.size() + 1);
            for (auto& val : row) {
                out_values.push_back(std::move(val));
            }
            out_values.push_back(Value(std::move(pv)));
            output.appendRow(out_values);
        }
        if (output.count > 0) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
