#include "compute_service/physical_plan/operator/project_physical_op.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/parser/ast.hpp"

namespace eugraph {
namespace compute {

std::string ProjectPhysicalOp::toString() const {
    std::string s;
    for (size_t i = 0; i < items_.size(); i++) {
        if (i > 0)
            s += ", ";
        s += items_[i].name;
    }
    return "Project(items=[" + s + "])";
}

folly::coro::AsyncGenerator<RowBatch> ProjectPhysicalOp::execute() {
    auto child_gen = child_->execute();

    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (const auto& row : batch->rows) {
            Row new_row;
            new_row.reserve(items_.size());
            for (const auto& item : items_) {
                Value val = evaluator_.evaluate(item.expr, row, input_schema_);
                new_row.push_back(std::move(val));
            }
            output.push_back(std::move(new_row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
