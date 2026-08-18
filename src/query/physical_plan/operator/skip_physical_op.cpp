#include "query/physical_plan/operator/skip_physical_op.hpp"

#include "query/dataset/data_chunk.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/physical_plan/expression_compiler.hpp"

#include <stdexcept>

namespace eugraph {
namespace compute {

namespace {

/// Evaluate a variable-free SKIP expression once and validate it.
/// Parameters and constant expressions share this path so their type /
/// sign errors surface at runtime (matching Cypher semantics), while
/// literal validation still happens at bind time.
int64_t evaluateSkip(const binder::BoundExpression& expr, const function::EvalContext& eval_ctx) {
    DataChunk chunk;
    chunk.count = 1;
    Column result_col = Column::flat(binder::BoundTypeKind::ANY, 1);
    VectorizedEvaluator evaluator(eval_ctx);
    evaluator.evaluate(expr, chunk, result_col);
    const Value& value = result_col.getValue(0);
    if (!std::holds_alternative<int64_t>(value))
        throw std::runtime_error("SemanticError: SKIP must be an integer");
    int64_t skip = std::get<int64_t>(value);
    if (skip < 0)
        throw std::runtime_error("SemanticError: SKIP must be a non-negative integer");
    return skip;
}

} // namespace

void SkipPhysicalOp::compileExpressions(const TupleSlotLayout& input_layout) {
    if (expr_)
        ExpressionCompiler(input_layout).compile(*expr_);
}

folly::coro::AsyncGenerator<DataChunk> SkipPhysicalOp::executeChunk() {
    int64_t remaining = 0;
    if (skip_.has_value()) {
        remaining = *skip_;
    } else if (expr_.has_value()) {
        remaining = evaluateSkip(*expr_, eval_ctx_);
    }

    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();
        if (remaining >= static_cast<int64_t>(n)) {
            remaining -= static_cast<int64_t>(n);
            continue;
        }

        if (remaining > 0) {
            size_t offset = static_cast<size_t>(remaining);
            size_t new_count = n - offset;
            SelectionVector new_sel;
            new_sel.indices.resize(new_count);
            new_sel.count = new_count;
            new_sel.is_identity = false;
            for (size_t i = 0; i < new_count; ++i) {
                new_sel[i] = static_cast<uint32_t>(offset + i);
            }

            DataChunk output;
            for (auto& col : chunk->columns) {
                if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                    SelectionVector mapped = new_sel;
                    if (col.form == VectorForm::DICTIONARY) {
                        for (size_t i = 0; i < new_count; ++i) {
                            mapped[i] = col.dict_sel[new_sel[i]];
                        }
                    }
                    output.columns.push_back(Column::dict(col.buffer, mapped));
                } else if (col.form == VectorForm::CONSTANT) {
                    output.columns.push_back(Column::constant(col.constant_value));
                } else {
                    output.columns.push_back(Column(col.type));
                }
            }
            output.count = new_count;
            co_yield std::move(output);
            remaining = 0;

            // Pass through remaining chunks unchanged
            while (true) {
                auto rest = co_await child_gen.next();
                if (!rest.has_value())
                    break;
                co_yield std::move(*rest);
            }
            co_return;
        } else {
            co_yield std::move(*chunk);
        }
    }
}

} // namespace compute
} // namespace eugraph
