#include "query/physical_plan/operator/unwind_physical_op.hpp"

#include "common/types/graph_types.hpp"

#include <algorithm>

namespace eugraph {
namespace compute {

namespace {

/// Whether a pass-through column's payload is expensive enough that copying it
/// once per produced row dominates the operator.
///
/// UNWIND emits one row per list element, so a pass-through column is replicated
/// N times for a list of N elements. For a LIST/MAP/VERTEX/EDGE/PATH payload that
/// replication is a deep copy -- an unordered_map<LabelId, Properties> clone for an
/// entity, an element-wise clone for a list -- and on LDBC complex-10 it made the
/// operator quadratic (about 107 elements per row over 18321 posts). Scalars,
/// bare references (VERTEX_REF/EDGE_KEY/PATH_TOPOLOGY) and strings are deliberately
/// excluded: their copy is a handful of bytes or a single small allocation, so they
/// do not justify the different chunking strategy below.
bool isHeavyPassthrough(binder::BoundTypeKind kind) {
    switch (kind) {
    case binder::BoundTypeKind::LIST:
    case binder::BoundTypeKind::MAP:
    case binder::BoundTypeKind::VERTEX:
    case binder::BoundTypeKind::EDGE:
    case binder::BoundTypeKind::PATH:
        return true;
    default:
        return false;
    }
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> UnwindPhysicalOp::executeChunk() {
    auto gen = cancellable(child_->executeChunk());
    const size_t num_input_cols = input_schema_.size();

    // Rows produced from one input row all carry a byte-identical pass-through
    // value. When such a column is heavy, write it once per input row as a
    // CONSTANT column instead of once per produced row: Column::CONSTANT holds a
    // single value broadcast to every row, so the cost drops from one copy per
    // element to one copy per input row (107 -> 1 on complex-10).
    //
    // That requires the output chunk to hold rows from a single input row, since a
    // CONSTANT column cannot represent different values across rows. Chunking is
    // therefore per input row (sliced at DEFAULT_CAPACITY for long lists). Gate on
    // an actual heavy column so queries whose pass-through values are cheap keep the
    // original accumulate-across-rows chunking unchanged.
    const bool heavy_passthrough =
        std::any_of(output_types_.begin(), output_types_.begin() + static_cast<std::ptrdiff_t>(num_input_cols),
                    [](const binder::BoundType& t) { return isHeavyPassthrough(t.kind); });

    if (heavy_passthrough) {
        while (auto chunk = co_await gen.next()) {
            ExpressionEvaluator eval(eval_ctx_);

            Column list_col(binder::BoundTypeKind::LIST);
            list_col.reserve(chunk->count);
            eval.evaluate(list_expr_, *chunk, list_col);

            for (size_t r = 0; r < chunk->count; ++r) {
                // Borrow instead of getValue(r): getValue returns a Value by value,
                // which deep-copies the whole element vector. list_col is this
                // operator's own local, written only by the evaluate() call above and
                // never touched afterwards, so a borrow is safe here.
                const Column& read_only = list_col;
                const ListValue* list = read_only.borrowList(r);
                if (!list || list->elements.empty())
                    continue;

                // Non-null only when the buffer is exclusively ours, in which case the
                // elements can be moved out (each is consumed by exactly one output
                // row and list_col is discarded with this chunk).
                ListValue* movable = list_col.borrowList(r);

                // Emit this row's elements in slices of at most DEFAULT_CAPACITY.
                // `one` is a fresh chunk per slice, so the pass-through CONSTANT
                // values and the element buffer it owns are never shared with a
                // chunk that was already yielded.
                size_t emitted = 0;
                while (emitted < list->elements.size()) {
                    const size_t n = std::min(DataChunk::DEFAULT_CAPACITY, list->elements.size() - emitted);

                    DataChunk one;
                    one.setSchema(output_types_);
                    // Replace pass-through columns with CONSTANT BEFORE reserving, so
                    // reserve() only allocates for the element column; Column::reserve
                    // is a no-op for CONSTANT and the buffer a FLAT column would have
                    // allocated here would be discarded immediately.
                    for (size_t c = 0; c < num_input_cols; ++c)
                        one.columns[c] = Column::constant(chunk->columns[c].getValue(r));
                    one.reserve(n);

                    if (movable) {
                        for (size_t k = 0; k < n; ++k)
                            one.columns[output_col_index_].setValue(k, std::move(movable->elements[emitted + k].value));
                    } else {
                        for (size_t k = 0; k < n; ++k)
                            one.columns[output_col_index_].setValue(k, list->elements[emitted + k].value);
                    }

                    one.count = n;
                    emitted += n;
                    // Yield a copy; `one` is reassigned on the next iteration and its
                    // buffers are released with it.
                    co_yield one;
                }
            }
        }
        co_return;
    }

    DataChunk output;
    output.setSchema(output_types_);
    output.reserve(DataChunk::DEFAULT_CAPACITY);

    size_t output_row = 0;

    while (auto chunk = co_await gen.next()) {
        ExpressionEvaluator eval(eval_ctx_);

        // Evaluate the list expression for all rows in this chunk
        Column list_col(binder::BoundTypeKind::LIST);
        list_col.reserve(chunk->count);
        eval.evaluate(list_expr_, *chunk, list_col);

        // Scratch for the invariant per-row column values. Declared once and reused
        // across rows: allocating it per row was pure churn on a path that runs
        // hundreds of times per chunk.
        std::vector<Value> row_vals(num_input_cols);

        for (size_t r = 0; r < chunk->count; ++r) {
            // Borrowed, not copied -- see the heavy path above for why this is safe.
            const Column& read_only = list_col;
            const ListValue* list = read_only.borrowList(r);
            if (!list)
                continue;

            // Read the invariant input columns ONCE per input row instead of once
            // per produced element: the columns of row r do not change as the list
            // is walked.
            for (size_t c = 0; c < num_input_cols; ++c)
                row_vals[c] = chunk->columns[c].getValue(r);

            for (const auto& elem : list->elements) {
                for (size_t c = 0; c < num_input_cols; ++c)
                    output.columns[c].setValue(output_row, row_vals[c]);
                // Pass the stored element directly rather than copying it into a
                // temporary first: elem.value is a ValueStorage whose payload may be
                // a full VertexValue.
                output.columns[output_col_index_].setValue(output_row, elem.value);

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
