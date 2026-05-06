#include "compute_service/physical_plan/operator/path_build_physical_op.hpp"

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

folly::coro::AsyncGenerator<RowBatch> PathBuildPhysicalOp::execute() {
    auto child_gen = child_->execute();

    while (auto child_batch = co_await child_gen.next()) {
        RowBatch output;
        for (auto& row : child_batch->rows) {
            PathValue pv;
            for (size_t i = 0; i < element_vars_.size(); ++i) {
                int col = element_cols_[i];
                if (col >= 0 && static_cast<size_t>(col) < row.size()) {
                    pv.elements.push_back(ValueStorage{row[col]});
                }
            }

            Row new_row = std::move(row);
            new_row.push_back(Value(std::move(pv)));
            output.push_back(std::move(new_row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
