#include "compute_service/physical_plan/operator/project_physical_op.hpp"

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

folly::coro::AsyncGenerator<DataChunk> ProjectPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        DataChunk output;
        output.columns.reserve(items_.size());

        VectorizedEvaluator evaluator;
        for (const auto& item : items_) {
            auto& out_col = output.addColumn(binder::getBoundExprType(item.expr).kind);
            out_col.reserve(n);
            evaluator.evaluate(item.expr, *chunk, out_col);
        }
        output.count = n;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
