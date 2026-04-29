#include "compute_service/physical_plan/operator/limit_physical_op.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> LimitPhysicalOp::execute() {
    auto child_gen = child_->execute();
    int64_t remaining = limit_;

    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (auto& row : batch->rows) {
            if (remaining <= 0) {
                if (!output.empty()) {
                    co_yield std::move(output);
                }
                co_return;
            }
            output.push_back(std::move(row));
            --remaining;
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
        if (remaining <= 0) {
            co_return;
        }
    }
}

} // namespace compute
} // namespace eugraph
