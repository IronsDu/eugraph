#include "query/physical_plan/operator/left_join_physical_op.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> LeftJoinPhysicalOp::executeChunk() {
    auto left_gen = left_->executeChunk();

    while (auto left_chunk = co_await left_gen.next()) {
        if (!left_chunk || left_chunk->count == 0)
            continue;

        size_t n_left = left_chunk->count;
        size_t left_cols = left_chunk->columns.size();
        size_t right_cols = right_output_types_.size();

        DataChunk output;

        for (size_t i = 0; i < n_left; ++i) {
            // Extract correlated values from the left row
            if (correlated_source_ && !left_correlation_cols_.empty()) {
                std::vector<Value> corr_values;
                corr_values.reserve(left_correlation_cols_.size());
                for (uint32_t col_idx : left_correlation_cols_) {
                    corr_values.push_back(left_chunk->getValue(col_idx, i));
                }
                correlated_source_->setValues(std::move(corr_values));
            }

            // Execute the right sub-plan
            auto right_gen = right_->executeChunk();
            bool matched = false;

            while (auto right_chunk = co_await right_gen.next()) {
                if (!right_chunk || right_chunk->count == 0)
                    continue;

                matched = true;
                size_t n_right = right_chunk->count;

                // Ensure output capacity
                if (output.columns.empty()) {
                    output.columns.reserve(left_cols + right_cols);
                    for (size_t lc = 0; lc < left_cols; ++lc) {
                        output.columns.push_back(Column::flat(left_chunk->columns[lc].type));
                    }
                    for (size_t rc = 0; rc < right_cols; ++rc) {
                        output.columns.push_back(Column::flat(right_output_types_[rc].kind));
                    }
                    output.reserve(DataChunk::DEFAULT_CAPACITY);
                }

                // Emit left_row × right_rows
                for (size_t ri = 0; ri < n_right; ++ri) {
                    for (size_t lc = 0; lc < left_cols; ++lc) {
                        output.columns[lc].setValue(output.count, left_chunk->getValue(lc, i));
                    }
                    for (size_t rc = 0; rc < right_cols; ++rc) {
                        output.columns[left_cols + rc].setValue(output.count, right_chunk->getValue(rc, ri));
                    }
                    output.count++;

                    if (output.count >= DataChunk::DEFAULT_CAPACITY) {
                        output.sel = SelectionVector::identity(output.count);
                        co_yield std::move(output);
                        output = DataChunk();
                        output.columns.reserve(left_cols + right_cols);
                        for (size_t lc = 0; lc < left_cols; ++lc) {
                            output.columns.push_back(Column::flat(left_chunk->columns[lc].type));
                        }
                        for (size_t rc = 0; rc < right_cols; ++rc) {
                            output.columns.push_back(Column::flat(right_output_types_[rc].kind));
                        }
                        output.reserve(DataChunk::DEFAULT_CAPACITY);
                    }
                }
            }

            // No match: emit left row + NULLs for right columns
            if (!matched) {
                if (output.columns.empty()) {
                    output.columns.reserve(left_cols + right_cols);
                    for (size_t lc = 0; lc < left_cols; ++lc) {
                        output.columns.push_back(Column::flat(left_chunk->columns[lc].type));
                    }
                    for (size_t rc = 0; rc < right_cols; ++rc) {
                        output.columns.push_back(Column::flat(right_output_types_[rc].kind));
                    }
                    output.reserve(DataChunk::DEFAULT_CAPACITY);
                }

                for (size_t lc = 0; lc < left_cols; ++lc) {
                    output.columns[lc].setValue(output.count, left_chunk->getValue(lc, i));
                }
                for (size_t rc = 0; rc < right_cols; ++rc) {
                    output.columns[left_cols + rc].setNull(output.count);
                }
                output.count++;

                if (output.count >= DataChunk::DEFAULT_CAPACITY) {
                    output.sel = SelectionVector::identity(output.count);
                    co_yield std::move(output);
                    output = DataChunk();
                    output.columns.reserve(left_cols + right_cols);
                    for (size_t lc = 0; lc < left_cols; ++lc) {
                        output.columns.push_back(Column::flat(left_chunk->columns[lc].type));
                    }
                    for (size_t rc = 0; rc < right_cols; ++rc) {
                        output.columns.push_back(Column::flat(right_output_types_[rc].kind));
                    }
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

} // namespace compute
} // namespace eugraph
