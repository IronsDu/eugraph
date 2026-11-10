#include "query/physical_plan/operator/pattern_comprehension_fork_physical_op.hpp"

#include <algorithm>

namespace eugraph {
namespace compute {

namespace {

/// The branches replay the anchor's chunks as their own input, so the spooled
/// chunks must be self-contained: no dangling selection vector, no dependence on
/// the producing operator's buffer lifetime beyond the shared ColumnBuffers.
DataChunk normalizeChunk(const DataChunk& in) {
    if (in.sel.is_identity)
        return in;
    DataChunk out;
    const size_t n = in.numRows();
    out.count = n;
    out.sel = SelectionVector::identity(n);
    out.columns.reserve(in.columns.size());
    for (const auto& src : in.columns) {
        Column dst = Column::flat(src.type, n);
        for (size_t i = 0; i < n; ++i) {
            if (src.isNull(in.sel[i]))
                dst.setNull(i);
            else
                dst.setValue(i, src.getValue(in.sel[i]));
        }
        out.columns.push_back(std::move(dst));
    }
    return out;
}

} // namespace

std::vector<const PhysicalOperator*> PatternComprehensionForkPhysicalOp::children() const {
    std::vector<const PhysicalOperator*> out;
    out.reserve(2 + branches_.size());
    out.push_back(left_.get());
    out.push_back(anchor_.get());
    for (const auto& branch : branches_)
        out.push_back(branch.get());
    return out;
}

void PatternComprehensionForkPhysicalOp::deriveOutputLayout(const TupleSlotLayout& /*parent_layout*/) {
    if (slot_layout_.size() == 0) {
        slot_layout_ = left_->slotLayout();
        for (auto slot : output_slots_)
            slot_layout_.append(slot);
    }
}

folly::coro::AsyncGenerator<DataChunk> PatternComprehensionForkPhysicalOp::executeChunk() {
    auto left_gen = cancellable(left_->executeChunkTraced());

    while (auto left_chunk_opt = co_await left_gen.next()) {
        if (!left_chunk_opt || left_chunk_opt->count == 0)
            continue;
        auto& in_chunk = *left_chunk_opt;
        const size_t n_left = in_chunk.numRows();

        // One output LIST column per branch.
        std::vector<std::shared_ptr<ColumnBuffer>> list_buffers;
        list_buffers.reserve(list_element_types_.size());
        for (size_t oi = 0; oi < list_element_types_.size(); ++oi) {
            auto buf = std::make_shared<ColumnBuffer>();
            buf->type = binder::BoundTypeKind::LIST;
            buf->reserve(n_left);
            list_buffers.push_back(std::move(buf));
        }
        // A left row whose branch produced no group row keeps an empty list.
        for (auto& buf : list_buffers)
            for (size_t i = 0; i < n_left; ++i)
                buf->list_data[i] = mk<ListValue>(ListValue{});

        for (size_t base = 0; base < n_left; base += kBatchSize) {
            const size_t end = std::min(n_left, base + kBatchSize);

            // ---- 1. Feed the outer batch into the anchor's correlated source.
            std::vector<std::vector<Value>> corr_rows;
            corr_rows.reserve(end - base);
            for (size_t i = base; i < end; ++i) {
                std::vector<Value> values;
                values.reserve(anchor_value_columns_.size());
                for (int32_t col : anchor_value_columns_) {
                    if (col < 0)
                        values.push_back(Value{static_cast<int64_t>(i - base)});
                    else if (static_cast<size_t>(col) < in_chunk.columns.size())
                        values.push_back(in_chunk.columns[col].getValue(i));
                    else
                        values.push_back(Value{});
                }
                corr_rows.push_back(std::move(values));
            }
            anchor_source_->setRows(std::move(corr_rows));

            // ---- 2. Execute the shared anchor exactly once for this batch and
            // spool its output rows.
            auto spool = std::make_shared<std::vector<DataChunk>>();
            {
                auto anchor_gen = cancellable(anchor_->executeChunkTraced());
                while (auto chunk = co_await anchor_gen.next()) {
                    if (!chunk || chunk->count == 0)
                        continue;
                    spool->push_back(normalizeChunk(*chunk));
                }
            }
            ++anchor_executions_;

            // ---- 3. Replay the anchor rows into every branch.
            for (size_t bi = 0; bi < branches_.size(); ++bi) {
                if (!branch_sources_[bi])
                    continue;
                branch_sources_[bi]->setChunks(spool);
                const int batch_col = bi < branch_batch_columns_.size() ? branch_batch_columns_[bi] : -1;
                const size_t list_col = bi < branch_list_columns_.size() ? branch_list_columns_[bi] : 0;
                auto branch_gen = cancellable(branches_[bi]->executeChunkTraced());
                while (auto chunk = co_await branch_gen.next()) {
                    if (!chunk || chunk->count == 0)
                        continue;
                    for (size_t r = 0; r < chunk->count; ++r) {
                        size_t row = base;
                        if (batch_col >= 0 && static_cast<size_t>(batch_col) < chunk->columns.size()) {
                            Value bid = chunk->columns[batch_col].getValue(r);
                            const auto* as_int = std::get_if<int64_t>(&bid);
                            if (!as_int || *as_int < 0 || static_cast<size_t>(*as_int) >= end - base)
                                continue;
                            row = base + static_cast<size_t>(*as_int);
                        } else if (chunk->count == 1) {
                            row = base;
                        } else if (r < end - base) {
                            row = base + r;
                        }
                        if (list_col >= chunk->columns.size())
                            break;
                        Value v = chunk->columns[list_col].getValue(r);
                        if (auto* lv = std::get_if<ListValuePtr>(&v)) {
                            if (*lv)
                                list_buffers[bi]->list_data[row] = mk<ListValue>(**lv);
                        } else if (!::eugraph::isNull(v)) {
                            ListValue one;
                            one.elements.push_back(ValueStorage{std::move(v)});
                            list_buffers[bi]->list_data[row] = mk<ListValue>(std::move(one));
                        }
                    }
                }
            }
        }

        // ---- 4. Forward the left columns and append the collected lists.
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
