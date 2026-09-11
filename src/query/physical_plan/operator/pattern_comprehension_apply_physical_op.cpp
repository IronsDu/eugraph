#include "query/physical_plan/operator/pattern_comprehension_apply_physical_op.hpp"

namespace eugraph {
namespace compute {

void PatternComprehensionApplyPhysicalOp::deriveOutputLayout(const TupleSlotLayout& /*parent_layout*/) {
    if (slot_layout_.size() == 0)
        slot_layout_ = left_->slotLayout();
}

folly::coro::AsyncGenerator<DataChunk> PatternComprehensionApplyPhysicalOp::executeChunk() {
    auto left_gen = left_->executeChunk();

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

        for (size_t i = 0; i < n_left; ++i) {
            // Inject correlation values from left row i.
            std::vector<Value> corr_values;
            corr_values.reserve(left_correlation_cols_.size());
            for (uint32_t col_idx : left_correlation_cols_) {
                corr_values.push_back(in_chunk.getValue(col_idx, i));
            }
            correlated_source_->setValues(std::move(corr_values));

            // Drain the right sub-plan. The right sub-plan ends with
            // Aggregate(collect(...)) emitting one row per group; with no
            // group keys we get a single row whose column[oi] IS the fully
            // collected ListValue.
            std::vector<ListValue> collected;
            collected.resize(list_element_types_.size());
            // Existence-only: every consumer of this column reads it solely
            // through the synthesised `size(list) > 0` predicate, so the list
            // contents are dead — only its emptiness is observable. Stop at the
            // first matching row and keep a single dummy element, which yields
            // exactly the same truth value (`0 > 0` false / `1 > 0` true) while
            // skipping the rest of the correlated sub-plan and the whole
            // collect() materialisation.
            bool existence_hit = false;
            auto right_gen = right_->executeChunk();
            while (auto right_chunk = co_await right_gen.next()) {
                if (!right_chunk || right_chunk->count == 0)
                    continue;
                for (size_t oi = 0; oi < list_element_types_.size(); ++oi) {
                    if (oi >= right_chunk->columns.size())
                        break;
                    Value v = right_chunk->columns[oi].getValue(0);
                    if (std::holds_alternative<ListValue>(v)) {
                        auto& lv = std::get<ListValue>(v);
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
            for (size_t oi = 0; oi < list_element_types_.size(); ++oi) {
                list_buffers[oi]->list_data[i] = std::move(collected[oi]);
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
