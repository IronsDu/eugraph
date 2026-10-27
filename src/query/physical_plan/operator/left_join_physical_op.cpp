#include "query/physical_plan/operator/left_join_physical_op.hpp"

#include <cstdlib>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> LeftJoinPhysicalOp::executePerRow() {
    auto left_gen = cancellable(left_->executeChunk());

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
            auto right_gen = cancellable(right_->executeChunk());
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
                        // Same shape as the ProjectionExtract pass-through: the value is
                        // only relocated, so copy it typed and skip the Value round-trip.
                        if (!output.columns[left_cols + rc].copyValueFrom(right_chunk->columns[rc], ri, output.count))
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

namespace {

VertexId ljVertexId(const Value& v) {
    if (std::holds_alternative<VertexRef>(v))
        return std::get<VertexRef>(v).id;
    if (std::holds_alternative<VertexValuePtr>(v)) {
        const auto& p = std::get<VertexValuePtr>(v);
        return p ? p->id : INVALID_VERTEX_ID;
    }
    return INVALID_VERTEX_ID;
}

bool ljCorrEqual(const Value& a, const Value& b) {
    VertexId aid = ljVertexId(a);
    VertexId bid = ljVertexId(b);
    if (aid != INVALID_VERTEX_ID || bid != INVALID_VERTEX_ID)
        return aid == bid;
    if (std::holds_alternative<ListValuePtr>(a) && std::holds_alternative<ListValuePtr>(b)) {
        const auto& pa = std::get<ListValuePtr>(a);
        const auto& pb = std::get<ListValuePtr>(b);
        if (!pa || !pb)
            return !pa && !pb;
        if (pa->elements.size() != pb->elements.size())
            return false;
        for (size_t i = 0; i < pa->elements.size(); ++i)
            if (!ljCorrEqual(pa->elements[i].value, pb->elements[i].value))
                return false;
        return true;
    }
    return valueEquals(a, b).value_or(false);
}

DataChunk ljMakeChunk(size_t cols) {
    DataChunk out;
    for (size_t i = 0; i < cols; ++i)
        out.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, DataChunk::DEFAULT_CAPACITY));
    return out;
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> LeftJoinPhysicalOp::executeChunk() {
    const char* env = std::getenv("EUGRAPH_LJ_BATCH");
    // Keep batches well below DataChunk::DEFAULT_CAPACITY (1024): this path
    // materializes all batch rows in one CorrelatedSource chunk, and crossing
    // the chunk-capacity boundary changes downstream batch behavior.
    size_t batch_limit = env ? std::strtoul(env, nullptr, 10) : 512;
    bool right_has_path = false;
    for (const auto& t : right_output_types_)
        if (t.kind == binder::BoundTypeKind::PATH || t.kind == binder::BoundTypeKind::PATH_TOPOLOGY)
            right_has_path = true;
    if (batch_limit == 0 || right_has_path || !correlated_source_ || right_correlation_cols_.empty()) {
        auto gen = executePerRow();
        while (auto chunk = co_await gen.next())
            co_yield std::move(*chunk);
        co_return;
    }
    std::vector<Row> batch;
    batch.reserve(batch_limit);
    std::vector<binder::BoundTypeKind> left_kinds;
    auto left_gen = cancellable(left_->executeChunk());
    while (auto left_chunk = co_await left_gen.next()) {
        if (left_chunk->count == 0)
            continue;
        if (left_kinds.empty())
            for (const auto& col : left_chunk->columns)
                left_kinds.push_back(col.type);
        for (size_t r = 0; r < left_chunk->count; ++r) {
            Row row;
            for (size_t c = 0; c < left_chunk->columns.size(); ++c)
                row.push_back(left_chunk->getValue(c, r));
            batch.push_back(std::move(row));
            if (batch.size() >= batch_limit) {
                auto gen = executeBatch(std::move(batch), left_kinds);
                while (auto chunk = co_await gen.next())
                    co_yield std::move(*chunk);
                batch.clear();
            }
        }
    }
    if (!batch.empty()) {
        auto gen = executeBatch(std::move(batch), left_kinds);
        while (auto chunk = co_await gen.next())
            co_yield std::move(*chunk);
    }
}

folly::coro::AsyncGenerator<DataChunk> LeftJoinPhysicalOp::executeBatch(std::vector<Row> left_rows,
                                                                        std::vector<binder::BoundTypeKind> left_kinds) {
    std::vector<std::vector<Value>> corr_rows;
    for (const auto& row : left_rows) {
        std::vector<Value> values;
        for (uint32_t col : left_correlation_cols_)
            values.push_back(row[col]);
        corr_rows.push_back(std::move(values));
    }
    correlated_source_->setRows(std::move(corr_rows));

    std::vector<Row> right_rows;
    auto right_gen = cancellable(right_->executeChunk());
    while (auto right_chunk = co_await right_gen.next())
        for (size_t r = 0; r < right_chunk->count; ++r) {
            Row row;
            for (size_t c = 0; c < right_chunk->columns.size(); ++c)
                row.push_back(right_chunk->getValue(c, r));
            right_rows.push_back(std::move(row));
        }

    DataChunk out = ljMakeChunk(left_kinds.size() + right_output_types_.size());
    std::vector<const Row*> matches;
    for (const auto& left : left_rows) {
        matches.clear();
        for (const auto& right : right_rows) {
            bool same = true;
            for (size_t i = 0; i < left_correlation_cols_.size(); ++i) {
                if (i >= right_correlation_cols_.size() || right_correlation_cols_[i] >= right.size() ||
                    left_correlation_cols_[i] >= left.size()) {
                    same = false;
                    break;
                }
                if (!ljCorrEqual(left[left_correlation_cols_[i]], right[right_correlation_cols_[i]])) {
                    same = false;
                    break;
                }
            }
            if (same)
                matches.push_back(&right);
        }
        auto emit = [&](const Row* right) {
            for (size_t c = 0; c < left_kinds.size() && c < left.size(); ++c)
                out.columns[c].setValue(out.count, left[c]);
            if (right) {
                for (size_t c = 0; c < right_output_types_.size() && c < right->size(); ++c)
                    out.columns[left_kinds.size() + c].setValue(out.count, (*right)[c]);
            } else {
                for (size_t c = 0; c < right_output_types_.size(); ++c)
                    out.columns[left_kinds.size() + c].setNull(out.count);
            }
            ++out.count;
        };
        if (matches.empty())
            emit(nullptr);
        else
            for (const auto* right : matches)
                emit(right);
        if (out.count >= DataChunk::DEFAULT_CAPACITY) {
            out.sel = SelectionVector::identity(out.count);
            co_yield std::move(out);
            out = ljMakeChunk(left_kinds.size() + right_output_types_.size());
        }
    }
    if (out.count > 0) {
        out.sel = SelectionVector::identity(out.count);
        co_yield std::move(out);
    }
}

} // namespace compute
} // namespace eugraph
