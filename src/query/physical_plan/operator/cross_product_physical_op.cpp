#include "query/physical_plan/operator/cross_product_physical_op.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk>
CrossProductPhysicalOp::executeChunk() {
    size_t left_cols = left_schema_.size();
    size_t right_cols = right_schema_.size();

    auto left_gen = left_->executeChunk();
    while (auto left_chunk = co_await left_gen.next()) {
        if (!left_chunk || left_chunk->count == 0) continue;
        size_t n_left = left_chunk->count;

        // For each left batch, restart right scan from scratch
        auto right_gen = right_->executeChunk();
        while (auto right_chunk = co_await right_gen.next()) {
            if (!right_chunk || right_chunk->count == 0) continue;
            size_t n_right = right_chunk->count;

            DataChunk output;
            output.setSchema(output_types_);
            output.reserve(DataChunk::DEFAULT_CAPACITY);

            for (size_t li = 0; li < n_left; ++li) {
                for (size_t ri = 0; ri < n_right; ++ri) {
                    for (size_t lc = 0; lc < left_cols; ++lc) {
                        output.setValue(lc, output.count, left_chunk->getValue(lc, li));
                    }
                    for (size_t rc = 0; rc < right_cols; ++rc) {
                        output.setValue(left_cols + rc, output.count, right_chunk->getValue(rc, ri));
                    }
                    output.count++;

                    if (output.count >= DataChunk::DEFAULT_CAPACITY) {
                        output.sel = SelectionVector::identity(output.count);
                        co_yield std::move(output);
                        output = DataChunk{};
                        output.setSchema(output_types_);
                        output.reserve(DataChunk::DEFAULT_CAPACITY);
                    }
                }
            }

            if (output.count > 0) {
                output.sel = SelectionVector::identity(output.count);
                co_yield std::move(output);
            }
        }
    }
}

} // namespace compute
} // namespace eugraph
