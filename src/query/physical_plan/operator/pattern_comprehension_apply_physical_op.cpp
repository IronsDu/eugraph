#include "query/physical_plan/operator/pattern_comprehension_apply_physical_op.hpp"

namespace eugraph {
namespace compute {

void PatternComprehensionApplyPhysicalOp::deriveOutputLayout(const TupleSlotLayout& /*parent_layout*/) {
    if (slot_layout_.size() == 0) {
        slot_layout_ = left_->slotLayout();
        for (auto slot : output_slots_)
            slot_layout_.append(slot);
    }
}

folly::coro::AsyncGenerator<DataChunk> PatternComprehensionApplyPhysicalOp::executeChunk() {
    auto left_gen = cancellable(left_->executeChunkTraced());

    while (auto left_chunk_opt = co_await left_gen.next()) {
        if (!left_chunk_opt || left_chunk_opt->count == 0)
            continue;
        auto& in_chunk = *left_chunk_opt;
        size_t n_left = in_chunk.numRows();
        // Pre-allocate one ColumnBuffer per PatternComprehension output.
        std::vector<std::shared_ptr<ColumnBuffer>> list_buffers;
        list_buffers.reserve(list_element_types_.size());
        for (size_t oi = 0; oi < list_element_types_.size(); ++oi) {
            auto buf = std::make_shared<ColumnBuffer>();
            buf->type = binder::BoundTypeKind::LIST;
            buf->reserve(n_left); // allocates list_data AND validity bitmap
            list_buffers.push_back(std::move(buf));
        }

        // Empty list for rows without a matching right row (batch Aggregate
        // emits no group row for an empty batch).
        for (size_t oi = 0; oi < list_element_types_.size(); ++oi)
            for (size_t i = 0; i < n_left; ++i)
                list_buffers[oi]->list_data[i] = mk<ListValue>(ListValue{});

        if (batch_enabled_ && n_left > 0) {
            for (size_t base = 0; base < n_left; base += kBatchSize) {
                const size_t end = std::min(n_left, base + kBatchSize);
                std::vector<std::vector<Value>> corr_rows;
                corr_rows.reserve(end - base);
                for (size_t i = base; i < end; ++i) {
                    std::vector<Value> values;
                    values.reserve(left_correlation_cols_.size() + 1);
                    for (uint32_t col : left_correlation_cols_)
                        values.push_back(in_chunk.columns[col].getValue(i));
                    values.push_back(Value{static_cast<int64_t>(i - base)});
                    corr_rows.push_back(std::move(values));
                }
                correlated_source_->setRows(std::move(corr_rows));

                auto right_gen = cancellable(right_->executeChunkTraced());
                while (auto right_chunk = co_await right_gen.next()) {
                    if (!right_chunk || right_chunk->count == 0)
                        continue;
                    for (size_t r = 0; r < right_chunk->count; ++r) {
                        if (batch_column_ >= right_chunk->columns.size())
                            continue;
                        Value bid_value = right_chunk->columns[batch_column_].getValue(r);
                        const auto* bid = std::get_if<int64_t>(&bid_value);
                        if (!bid || *bid < 0 || static_cast<size_t>(*bid) >= end - base)
                            continue;
                        const size_t row = base + static_cast<size_t>(*bid);
                        for (size_t oi = 0; oi < list_element_types_.size(); ++oi) {
                            const size_t col = list_column_start_ + oi;
                            if (col >= right_chunk->columns.size())
                                break;
                            Value v = right_chunk->columns[col].getValue(r);
                            if (auto* lv = std::get_if<ListValuePtr>(&v)) {
                                if (*lv)
                                    list_buffers[oi]->list_data[row] = mk<ListValue>(**lv);
                            } else if (!::eugraph::isNull(v)) {
                                ListValue one;
                                one.elements.push_back(ValueStorage{std::move(v)});
                                list_buffers[oi]->list_data[row] = mk<ListValue>(std::move(one));
                            }
                        }
                    }
                }
            }
        } else {
            for (size_t i = 0; i < n_left; ++i) {
                std::vector<Value> values;
                values.reserve(left_correlation_cols_.size() + 1);
                for (uint32_t col : left_correlation_cols_)
                    values.push_back(in_chunk.columns[col].getValue(i));
                values.push_back(Value{int64_t{0}});
                correlated_source_->setValues(values);

                std::vector<ListValue> collected;
                collected.resize(list_element_types_.size());
                bool existence_hit = false;
                auto right_gen = cancellable(right_->executeChunkTraced());
                while (auto right_chunk = co_await right_gen.next()) {
                    if (!right_chunk || right_chunk->count == 0)
                        continue;
                    for (size_t oi = 0; oi < list_element_types_.size(); ++oi) {
                        const size_t col = list_column_start_ + oi;
                        if (col >= right_chunk->columns.size())
                            break;
                        Value v = right_chunk->columns[col].getValue(0);
                        if (std::holds_alternative<ListValuePtr>(v)) {
                            auto& lv = (*std::get<ListValuePtr>(v));
                            if (existence_only_) {
                                if (!lv.elements.empty() && collected[oi].elements.empty())
                                    collected[oi].elements.push_back(ValueStorage{Value{int64_t{1}}});
                            } else {
                                collected[oi] = std::move(lv);
                            }
                        } else if (!::eugraph::isNull(v)) {
                            collected[oi].elements.push_back(ValueStorage{std::move(v)});
                        }
                    }
                    if (existence_only_) {
                        for (const auto& lv : collected) {
                            if (!lv.elements.empty()) {
                                existence_hit = true;
                                break;
                            }
                        }
                        if (existence_hit)
                            break;
                    }
                }
                for (size_t oi = 0; oi < list_element_types_.size(); ++oi)
                    list_buffers[oi]->list_data[i] = mk<ListValue>(std::move(collected[oi]));
            }
        }

        // Build output chunk: forward left columns + append list column(s).
        DataChunk output;
        output.columns.reserve(in_chunk.columns.size() + list_buffers.size());
        for (auto& col : in_chunk.columns) {
            if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                SelectionVector mapped;
                mapped.is_identity = false;
                mapped.indices.reserve(n_left);
                for (size_t i = 0; i < n_left; ++i) {
                    if (col.form == VectorForm::DICTIONARY)
                        mapped.indices.push_back(col.dict_sel[i]);
                    else
                        mapped.indices.push_back(static_cast<uint32_t>(i));
                }
                mapped.count = n_left;
                output.columns.push_back(Column::dict(col.buffer, mapped));
            } else if (col.form == VectorForm::CONSTANT) {
                output.columns.push_back(Column::constant(col.constant_value));
            } else {
                output.columns.push_back(Column(col.type));
            }
        }
        for (auto& buf : list_buffers) {
            Column col(binder::BoundTypeKind::LIST);
            col.buffer = buf;
            output.columns.push_back(std::move(col));
        }
        output.count = n_left;
        output.sel = SelectionVector::identity(n_left);
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
