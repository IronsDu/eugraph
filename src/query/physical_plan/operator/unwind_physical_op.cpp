#include "query/physical_plan/operator/unwind_physical_op.hpp"

#include "common/types/graph_types.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> UnwindPhysicalOp::executeChunk() {
    auto gen = child_->executeChunk();
    size_t num_input_cols = input_schema_.size();

    DataChunk output;
    output.setSchema(output_types_);
    output.reserve(DataChunk::DEFAULT_CAPACITY);

    size_t output_row = 0;

    while (auto chunk = co_await gen.next()) {
        VectorizedEvaluator eval(eval_ctx_);

        // Evaluate the list expression for all rows in this chunk
        Column list_col(binder::BoundTypeKind::LIST);
        list_col.reserve(chunk->count);
        eval.evaluate(list_expr_, *chunk, list_col);

        for (size_t r = 0; r < chunk->count; ++r) {
            Value list_val = list_col.getValue(r);

            // NULL or non-list → skip this row
            if (isNull(list_val) || !std::holds_alternative<ListValue>(list_val))
                continue;

            const auto& list = std::get<ListValue>(list_val);

            for (const auto& elem : list.elements) {
                Value elem_val = elem.value;
                // Copy input columns
                for (size_t c = 0; c < num_input_cols; ++c) {
                    output.columns[c].setValue(output_row, chunk->columns[c].getValue(r));
                }
                // Set the unwind variable column
                output.columns[output_col_index_].setValue(output_row, elem_val);

                ++output_row;
                if (output_row >= DataChunk::DEFAULT_CAPACITY) {
                    output.count = output_row;
                    co_yield output;

                    output.setSchema(output_types_);
                    output.reserve(DataChunk::DEFAULT_CAPACITY);
                    output_row = 0;
                }
            }
        }
    }

    if (output_row > 0) {
        output.count = output_row;
        co_yield output;
    }
}

} // namespace compute
} // namespace eugraph
