#include "query/physical_plan/operator/list_index_join_physical_op.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> ListIndexJoinPhysicalOp::executeChunk() {
    auto left_gen = left_->executeChunk();

    while (auto left_chunk = co_await left_gen.next()) {
        if (!left_chunk || left_chunk->count == 0)
            continue;

        const size_t left_cols = left_chunk->numColumns();
        for (size_t row = 0; row < left_chunk->count; ++row) {
            const auto& list_val = left_chunk->getValue(left_list_col_, row);
            std::shared_ptr<std::vector<PropertyValue>> allowed;
            if (std::holds_alternative<ListValue>(list_val)) {
                allowed = std::make_shared<std::vector<PropertyValue>>();
                for (const auto& elem : std::get<ListValue>(list_val).elements) {
                    if (std::holds_alternative<int64_t>(elem.value))
                        allowed->push_back(int64_t{std::get<int64_t>(elem.value)});
                    else if (std::holds_alternative<std::string>(elem.value))
                        allowed->push_back(std::get<std::string>(elem.value));
                }
            }
            if (filtered_expand_)
                filtered_expand_->setAllowedDstValues(allowed);
            if (filtered_source_)
                filtered_source_->setValues(allowed);

            auto right_gen = right_->executeChunk();
            while (auto right_chunk = co_await right_gen.next()) {
                if (!right_chunk || right_chunk->count == 0)
                    continue;

                DataChunk output;
                output.columns.reserve(left_cols + right_chunk->numColumns());
                for (size_t c = 0; c < left_cols; ++c) {
                    output.columns.push_back(Column::constant(left_chunk->getValue(c, row)));
                }
                for (auto& col : right_chunk->columns)
                    output.columns.push_back(std::move(col));
                output.count = right_chunk->count;
                output.sel = SelectionVector::identity(output.count);
                co_yield std::move(output);
            }
        }
    }
}

} // namespace compute
} // namespace eugraph
