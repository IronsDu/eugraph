#include "compute_service/physical_plan/operator/filter_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> FilterPhysicalOp::execute() {
    auto child_gen = child_->execute();

    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (const auto& row : batch->rows) {
            Value result = evaluator_.evaluate(predicate_, row, schema_);
            if (std::holds_alternative<bool>(result) && std::get<bool>(result)) {
                output.push_back(Row(row)); // copy
            }
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
