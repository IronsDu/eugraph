#include "query/physical_plan/operator/apply_physical_op.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> ApplyPhysicalOp::executeChunk() {
    auto left_gen = left_->executeChunk();

    while (auto left_chunk = co_await left_gen.next()) {
        if (!left_chunk || left_chunk->count == 0)
            continue;

        const size_t left_cols = left_chunk->numColumns();
        const size_t right_cols = right_output_types_.size();

        DataChunk output;
        output.columns.reserve(left_cols + right_cols);
        for (size_t c = 0; c < left_cols; ++c)
            output.columns.push_back(Column::flat(left_chunk->columns[c].type));
        for (size_t c = 0; c < right_cols; ++c)
            output.columns.push_back(Column::flat(right_output_types_[c].kind));
        output.reserve(DataChunk::DEFAULT_CAPACITY);

        for (size_t row = 0; row < left_chunk->count; ++row) {
            std::vector<Value> corr_values;
            corr_values.reserve(left_correlation_cols_.size());
            for (uint32_t col_idx : left_correlation_cols_) {
                if (col_idx < left_cols)
                    corr_values.push_back(left_chunk->getValue(col_idx, row));
            }
            if (correlated_source_)
                correlated_source_->setValues(std::move(corr_values));

            if (value_sink_ && !corr_values.empty() && std::holds_alternative<ListValue>(corr_values[0])) {
                auto allowed = std::make_shared<std::vector<PropertyValue>>();
                for (const auto& elem : std::get<ListValue>(corr_values[0]).elements) {
                    if (std::holds_alternative<int64_t>(elem.value))
                        allowed->push_back(int64_t{std::get<int64_t>(elem.value)});
                    else if (std::holds_alternative<std::string>(elem.value))
                        allowed->push_back(std::get<std::string>(elem.value));
                }
                value_sink_->setValues(std::move(allowed));
            }

            auto right_gen = right_->executeChunk();
            while (auto right_chunk = co_await right_gen.next()) {
                if (!right_chunk || right_chunk->count == 0)
                    continue;

                for (size_t rr = 0; rr < right_chunk->count; ++rr) {
                    for (size_t lc = 0; lc < left_cols; ++lc)
                        output.columns[lc].setValue(output.count, left_chunk->getValue(lc, row));
                    for (size_t rc = 0; rc < right_cols && rc < right_chunk->numColumns(); ++rc)
                        output.columns[left_cols + rc].setValue(output.count, right_chunk->getValue(rc, rr));
                    output.count++;

                    if (output.count >= DataChunk::DEFAULT_CAPACITY) {
                        output.sel = SelectionVector::identity(output.count);
                        co_yield std::move(output);
                        output = DataChunk();
                        output.columns.reserve(left_cols + right_cols);
                        for (size_t c = 0; c < left_cols; ++c)
                            output.columns.push_back(Column::flat(left_chunk->columns[c].type));
                        for (size_t c = 0; c < right_cols; ++c)
                            output.columns.push_back(Column::flat(right_output_types_[c].kind));
                        output.reserve(DataChunk::DEFAULT_CAPACITY);
                    }
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
