#include "query/physical_plan/operator/semi_join_physical_op.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> SemiJoinPhysicalOp::executeChunk() {
    auto left_gen = left_->executeChunk();

    while (auto left_chunk = co_await left_gen.next()) {
        if (!left_chunk || left_chunk->count == 0)
            continue;

        size_t n_left = left_chunk->count;
        // Build bool mask: true = row has a match in the right sub-plan
        std::vector<bool> matched(n_left, false);

        for (size_t i = 0; i < n_left; ++i) {
            // Extract correlated values from the left row
            std::vector<Value> corr_values;
            corr_values.reserve(left_correlation_cols_.size());
            for (uint32_t col_idx : left_correlation_cols_) {
                corr_values.push_back(left_chunk->getValue(col_idx, i));
            }
            correlated_source_->setValues(std::move(corr_values));

            // Re-execute the right sub-plan with the new correlated values
            auto right_gen = right_->executeChunk();
            auto right_chunk = co_await right_gen.next();
            if (right_chunk && right_chunk->numRows() > 0) {
                matched[i] = true;
            }
        }

        // Build filtered output with only matched rows
        SelectionVector filtered;
        filtered.is_identity = false;
        filtered.indices.reserve(n_left);
        for (size_t i = 0; i < n_left; ++i) {
            bool keep = anti_ ? !matched[i] : matched[i];
            if (keep)
                filtered.indices.push_back(static_cast<uint32_t>(i));
        }
        filtered.count = filtered.indices.size();

        if (filtered.count == 0)
            continue;

        DataChunk output;
        output.columns.reserve(left_chunk->columns.size());
        for (auto& col : left_chunk->columns) {
            if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                SelectionVector mapped;
                mapped.is_identity = false;
                mapped.indices.reserve(filtered.count);
                for (size_t i = 0; i < filtered.count; ++i) {
                    uint32_t physical = filtered[i];
                    if (col.form == VectorForm::DICTIONARY) {
                        mapped.indices.push_back(col.dict_sel[physical]);
                    } else {
                        mapped.indices.push_back(physical);
                    }
                }
                mapped.count = filtered.count;
                output.columns.push_back(Column::dict(col.buffer, mapped));
            } else if (col.form == VectorForm::CONSTANT) {
                output.columns.push_back(Column::constant(col.constant_value));
            } else {
                output.columns.push_back(Column(col.type));
            }
        }
        output.count = filtered.count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
