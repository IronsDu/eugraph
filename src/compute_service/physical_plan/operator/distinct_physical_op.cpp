#include "compute_service/physical_plan/operator/distinct_physical_op.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> DistinctPhysicalOp::execute() {
    std::unordered_set<Row, RowHash, RowEqual> seen;

    auto child_gen = child_->execute();
    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (auto& row : batch->rows) {
            if (seen.insert(row).second) {
                output.push_back(std::move(row));
            }
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
