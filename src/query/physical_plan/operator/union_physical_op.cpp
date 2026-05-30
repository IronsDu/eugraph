#include "query/physical_plan/operator/union_physical_op.hpp"
#include "query/dataset/data_chunk.hpp"

namespace eugraph {
namespace compute {

UnionPhysicalOp::UnionPhysicalOp(bool all, std::unique_ptr<PhysicalOperator> left,
                                 std::unique_ptr<PhysicalOperator> right)
    : all_(all), left_(std::move(left)), right_(std::move(right)) {}

std::string UnionPhysicalOp::toString() const {
    return all_ ? "UnionAll" : "Union";
}

std::vector<const PhysicalOperator*> UnionPhysicalOp::children() const {
    return {left_.get(), right_.get()};
}

folly::coro::AsyncGenerator<RowBatch> UnionPhysicalOp::execute() {
    return executeViaChunk();
}

folly::coro::AsyncGenerator<DataChunk> UnionPhysicalOp::executeChunk() {
    // Execute left child
    auto left_gen = left_->executeChunk();
    while (true) {
        auto chunk = co_await left_gen.next();
        if (!chunk.has_value())
            break;
        co_yield std::move(*chunk);
    }

    // Execute right child
    auto right_gen = right_->executeChunk();
    while (true) {
        auto chunk = co_await right_gen.next();
        if (!chunk.has_value())
            break;
        co_yield std::move(*chunk);
    }

    // Note: Deduplication (UNION without ALL) is handled by a DistinctPhysicalOp
    // placed above this operator by the physical planner.
}

} // namespace compute
} // namespace eugraph
