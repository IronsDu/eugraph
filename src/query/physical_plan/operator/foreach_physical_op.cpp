#include "query/physical_plan/operator/foreach_physical_op.hpp"

#include "common/types/graph_types.hpp"
#include "query/physical_plan/operator/mutation_mirror.hpp"

#include <utility>

namespace eugraph {
namespace compute {

namespace {

/// Entity id of a published vertex/edge value, or nullopt for anything else.
std::optional<uint64_t> entityId(const Value& value) {
    if (const auto* vertex = std::get_if<VertexValuePtr>(&value))
        return (*vertex)->id;
    if (const auto* edge = std::get_if<EdgeValuePtr>(&value))
        return (*edge)->id;
    return std::nullopt;
}

/// Record `updated` in `collected`, replacing an earlier value for the same entity.
///
/// Elements run one after another and each run publishes the entities it touched, so
/// keeping only the newest value per id is what leaves `collected` holding the final
/// state after the row's last element.
void rememberEntities(std::vector<Value>& collected, const DataChunk& chunk, size_t row) {
    for (size_t c = 0; c < chunk.columns.size(); ++c) {
        Value value = chunk.getValue(c, row);
        auto id = entityId(value);
        if (!id)
            continue;
        bool replaced = false;
        for (Value& existing : collected) {
            if (entityId(existing) == id) {
                existing = std::move(value);
                replaced = true;
                break;
            }
        }
        if (!replaced)
            collected.push_back(std::move(value));
    }
}

} // namespace

folly::coro::Task<void> ForeachPhysicalOp::runBodyOnce(const DataChunk& chunk, size_t row, const Value& element,
                                                       std::vector<Value>& updated) {
    // Positional contract with the body's CorrelatedSource: outer variables first
    // (in `input_columns_` order), the element last (its `element_column_`).
    std::vector<Value> correlated;
    correlated.reserve(input_columns_.size() + 1);
    for (uint32_t column : input_columns_)
        correlated.push_back(chunk.getValue(column, row));
    correlated.push_back(element);
    correlated_source_->setValues(std::move(correlated));

    // The body is a chain of updating operators: pulling it is what applies the
    // writes, so it must be drained even though nothing of it is emitted.
    auto body_gen = cancellable(body_->executeChunk());
    while (auto body_chunk = co_await body_gen.next()) {
        if (cancelled())
            co_return;
        if (!body_chunk || body_chunk->numRows() == 0)
            continue;
        // The body sees one input row (the element), so its output rows all describe
        // that same element; the last one carries the final state.
        rememberEntities(updated, *body_chunk, body_chunk->numRows() - 1);
    }
}

void ForeachPhysicalOp::refreshOuterEntities(DataChunk& chunk, size_t row, const std::vector<Value>& updated) {
    if (updated.empty())
        return;
    for (size_t c = 0; c < chunk.columns.size(); ++c) {
        Column& column = chunk.columns[c];
        if (column.type != binder::BoundTypeKind::VERTEX && column.type != binder::BoundTypeKind::EDGE)
            continue;
        const Value current = column.getValue(row);
        auto id = entityId(current);
        if (!id)
            continue;
        for (const Value& replacement : updated) {
            if (entityId(replacement) != id)
                continue;
            ensureExclusiveBuffer(column, chunk.count);
            column.setValue(row, replacement);
            break;
        }
    }
}

folly::coro::AsyncGenerator<DataChunk> ForeachPhysicalOp::executeChunk() {
    auto child_gen = cancellable(child_->executeChunk());

    while (auto chunk = co_await child_gen.next()) {
        if (cancelled())
            co_return;
        if (!chunk || chunk->numRows() == 0)
            continue;

        // Evaluate the list once per logical row, then interpret each value the way
        // neo4j does: list -> its elements, null -> nothing, anything else -> a
        // single element.
        Column list_column(binder::BoundTypeKind::ANY);
        list_column.reserve(chunk->count);
        ExpressionEvaluator eval(eval_ctx_);
        eval.evaluate(list_expr_, *chunk, list_column);

        const size_t rows = chunk->numRows();
        for (size_t row = 0; row < rows; ++row) {
            if (cancelled())
                co_return;

            const Value list = list_column.getValue(row);
            if (::eugraph::isNull(list))
                continue;

            std::vector<Value> updated;
            if (const auto* list_ptr = std::get_if<ListValuePtr>(&list)) {
                for (const auto& element : (*list_ptr)->elements) {
                    co_await runBodyOnce(*chunk, row, element.value, updated);
                    if (cancelled())
                        co_return;
                }
            } else {
                co_await runBodyOnce(*chunk, row, list, updated);
            }

            // The body may have written to entities this row carries (see the
            // declaration): publish the final state into the row before it is
            // handed on, or later clauses would read the pre-FOREACH snapshot.
            refreshOuterEntities(*chunk, row, updated);
        }

        // Side effects done, rows untouched: FOREACH never changes cardinality.
        chunk->sel = SelectionVector::identity(chunk->count);
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
