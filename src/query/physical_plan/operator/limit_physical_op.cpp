#include "query/physical_plan/operator/limit_physical_op.hpp"

#include "query/dataset/data_chunk.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/physical_plan/expression_compiler.hpp"

#include <stdexcept>

namespace eugraph {
namespace compute {

namespace {

/// Evaluate a variable-free LIMIT expression once and validate it.
/// Parameters and constant expressions share this path so their type /
/// sign errors surface at runtime (matching Cypher semantics), while
/// literal validation still happens at bind time.
int64_t evaluateLimit(const binder::BoundExpression& expr, const function::EvalContext& eval_ctx) {
    DataChunk chunk;
    chunk.count = 1;
    Column result_col = Column::flat(binder::BoundTypeKind::ANY, 1);
    VectorizedEvaluator evaluator(eval_ctx);
    evaluator.evaluate(expr, chunk, result_col);
    const Value& value = result_col.getValue(0);
    if (!std::holds_alternative<int64_t>(value))
        throw std::runtime_error("SemanticError: LIMIT must be an integer");
    int64_t limit = std::get<int64_t>(value);
    if (limit < 0)
        throw std::runtime_error("SemanticError: LIMIT must be a non-negative integer");
    return limit;
}

} // namespace

void LimitPhysicalOp::compileExpressions(const TupleSlotLayout& input_layout) {
    if (expr_)
        ExpressionCompiler(input_layout).compile(*expr_);
}

folly::coro::AsyncGenerator<DataChunk> LimitPhysicalOp::executeChunk() {
    int64_t remaining = 0;
    if (limit_.has_value()) {
        remaining = *limit_;
    } else if (expr_.has_value()) {
        remaining = evaluateLimit(*expr_, eval_ctx_);
    }

    auto child_gen = child_->executeChunk();

    // LIMIT 0: consume all child data (triggering side-effect operators like
    // DELETE / REMOVE) but yield nothing.  Cypher semantics require mutations
    // to execute regardless of LIMIT 0 — LIMIT only affects the result set.
    if (remaining == 0) {
        while (auto chunk = co_await child_gen.next()) {
            // discard — side effects already executed inside the child pipeline
        }
        co_return;
    }

    while (remaining > 0) {
        auto chunk = co_await child_gen.next();
        if (!chunk.has_value())
            break;

        size_t n = chunk->numRows();
        if (static_cast<int64_t>(n) <= remaining) {
            remaining -= static_cast<int64_t>(n);
            co_yield std::move(*chunk);
        } else {
            size_t limit = static_cast<size_t>(remaining);
            SelectionVector new_sel;
            new_sel.indices.resize(limit);
            new_sel.count = limit;
            new_sel.is_identity = false;
            for (size_t i = 0; i < limit; ++i) {
                new_sel[i] = static_cast<uint32_t>(i);
            }

            DataChunk output;
            for (auto& col : chunk->columns) {
                if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                    SelectionVector mapped = new_sel;
                    if (col.form == VectorForm::DICTIONARY) {
                        for (size_t i = 0; i < limit; ++i) {
                            mapped[i] = col.dict_sel[i];
                        }
                    }
                    output.columns.push_back(Column::dict(col.buffer, mapped));
                } else if (col.form == VectorForm::CONSTANT) {
                    output.columns.push_back(Column::constant(col.constant_value));
                } else {
                    output.columns.push_back(Column(col.type));
                }
            }
            output.count = limit;
            co_yield std::move(output);
            remaining = 0;
        }

        // Cypher side-effect semantics: LIMIT limits the visible result set,
        // but upstream write operators (CREATE / SET / DELETE / MERGE) must
        // still execute for every input row. Drain the rest of the child.
        if (remaining <= 0) {
            while (auto rest = co_await child_gen.next()) {
                // discard — side effects already executed inside child pipeline
            }
            co_return;
        }
    }
}

} // namespace compute
} // namespace eugraph
