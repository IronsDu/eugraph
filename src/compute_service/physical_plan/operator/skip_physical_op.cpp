#include "compute_service/physical_plan/operator/skip_physical_op.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> SkipPhysicalOp::execute() {
    auto child_gen = child_->execute();
    int64_t remaining = skip_;

    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (auto& row : batch->rows) {
            if (remaining > 0) {
                --remaining;
                continue;
            }
            output.push_back(std::move(row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
