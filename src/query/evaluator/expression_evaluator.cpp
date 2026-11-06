#include "query/evaluator/expression_evaluator.hpp"

#include <algorithm>
#include <stdexcept>

namespace eugraph {
namespace compute {

namespace {
/// Track recursion depth so nested evaluations (function arguments, map
/// entries, comprehension bodies) get their own scratch frame.
struct EvalDepthGuard {
    size_t& depth;
    explicit EvalDepthGuard(size_t& d) : depth(d) {
        ++depth;
    }
    ~EvalDepthGuard() {
        --depth;
    }
    EvalDepthGuard(const EvalDepthGuard&) = delete;
    EvalDepthGuard& operator=(const EvalDepthGuard&) = delete;
};
} // namespace

// ==================== Public API ====================

void ExpressionEvaluator::beginExpression() {
    // Recycle temporary columns from the previous top-level expression. They
    // are no longer referenced: evaluate()/evaluatePredicate() have already
    // copied their result into the caller's column/bitmap.
    temp_used_ = 0;
}

void ExpressionEvaluator::resetForChunk() {
    temp_used_ = 0;
    prefetched_vertex_data_.clear();
    prefetch_pending_ids_.clear();
    prefetching_ = false;
    temp_column_overrides_.clear();
    for (auto& scratch : scratch_stack_) {
        scratch.arg_results.clear();
        scratch.arg_cols.clear();
        scratch.case_branches.clear();
        scratch.entry_cols.clear();
        scratch.predicate.clear();
    }
}

void ExpressionEvaluator::evaluate(const binder::BoundExpression& expr, const DataChunk& input, Column& result) {
    if (eval_depth_ == 0)
        beginExpression();

    size_t count = input.numRows();
    result.reserve(count);

    auto eval_result = evaluateInternal(expr, input);
    if (eval_result.column && eval_result.column != &result) {
        // Whole-column typed copy first: the common case is a typed expression
        // result copied into a same-typed output column.
        if (result.copyFrom(*eval_result.column, count))
            return;
        for (size_t i = 0; i < count; ++i) {
            if (result.copyValueFrom(*eval_result.column, i, i))
                continue;
            if (eval_result.column->isNull(i)) {
                result.setNull(i);
            } else {
                result.setValue(i, eval_result.column->getValue(i));
            }
        }
    }
}

void ExpressionEvaluator::evaluatePredicate(const binder::BoundExpression& expr, const DataChunk& input,
                                            std::vector<bool>& result) {
    if (eval_depth_ == 0)
        beginExpression();

    size_t count = input.numRows();
    result.resize(count, false);

    auto eval_result = evaluateInternal(expr, input);
    if (!eval_result.column)
        return;

    const Column& col = *eval_result.column;
    if (col.type == binder::BoundTypeKind::BOOL) {
        if (col.isConstant()) {
            const bool v = !col.isNull(0) && col.getBool(0);
            std::fill(result.begin(), result.end(), v);
            return;
        }
        if (col.form == VectorForm::FLAT) {
            for (size_t i = 0; i < count; ++i)
                result[i] = !col.isNull(i) && col.getBool(i);
            return;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        if (!col.isNull(i)) {
            Value v = col.getValue(i);
            if (std::holds_alternative<bool>(v)) {
                result[i] = std::get<bool>(v);
            }
        }
    }
}

std::optional<bool> ExpressionEvaluator::evaluatePredicateOne(const binder::BoundExpression& expr,
                                                              const DataChunk& input) {
    auto eval_result = evaluateInternal(expr, input);
    if (!eval_result.column || eval_result.column->isNull(0))
        return std::nullopt;
    if (eval_result.column->type == binder::BoundTypeKind::BOOL)
        return eval_result.column->getBool(0);
    Value v = eval_result.column->getValue(0);
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v);
    return std::nullopt;
}

// ==================== Internal ====================

Column& ExpressionEvaluator::acquireTempSlot() {
    if (temp_used_ < temp_columns_.size())
        return temp_columns_[temp_used_++];
    temp_columns_.emplace_back();
    ++temp_used_;
    return temp_columns_.back();
}

Column& ExpressionEvaluator::acquireTempColumn(binder::BoundTypeKind type, size_t capacity) {
    Column& col = acquireTempSlot();
    col.resetFlat(type, capacity);
    return col;
}

Column& ExpressionEvaluator::acquireTempConstant(binder::BoundTypeKind type, Value value) {
    Column& col = acquireTempSlot();
    col.resetConstant(type, std::move(value));
    return col;
}

ExpressionEvaluator::EvalResult ExpressionEvaluator::evaluateInternal(const binder::BoundExpression& expr,
                                                                      const DataChunk& input) {
    EvalDepthGuard guard(eval_depth_);
    return std::visit(
        [this, &input](const auto& val) -> EvalResult {
            using T = std::decay_t<decltype(val)>;
            size_t count = input.numRows();

            if constexpr (std::is_same_v<T, binder::BoundLiteral>) {
                auto& col = acquireTempConstant(val.type.kind, val.value);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                return evalColumnRef(val, input);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                throw std::runtime_error(
                    "BoundVariableRef reached ExpressionEvaluator — must be resolved to BoundColumnRef at bind time");
            } else if constexpr (std::is_same_v<T, binder::BoundParameter>) {
                throw std::runtime_error(
                    "BoundParameter reached ExpressionEvaluator — must be substituted at bind time");
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                auto& col = acquireTempColumn(val->result_type.kind, count);
                evalBinaryOp(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                auto& col = acquireTempColumn(val->result_type.kind, count);
                evalUnaryOp(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                auto col_type = val->candidates.size() > 1 ? binder::BoundTypeKind::ANY : val->result_type.kind;
                auto& col = acquireTempColumn(col_type, count);
                evalPropertyRef(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, count);
                evalDynamicPropertyRef(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                auto& col = acquireTempColumn(val->return_type.kind, count);
                evalFunctionCall(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                return evalLabelCast(*val, input);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                return evalList(*val, input);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::BOOL, count);
                evalQuantifierExpr(QuantifierKind::ALL, val->loop_column_index, val->list_expr, val->where_pred, input,
                                   col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::BOOL, count);
                evalQuantifierExpr(QuantifierKind::ANY, val->loop_column_index, val->list_expr, val->where_pred, input,
                                   col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::BOOL, count);
                evalQuantifierExpr(QuantifierKind::NONE, val->loop_column_index, val->list_expr, val->where_pred, input,
                                   col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::BOOL, count);
                evalQuantifierExpr(QuantifierKind::SINGLE, val->loop_column_index, val->list_expr, val->where_pred,
                                   input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::LIST, count);
                evalListComprehension(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundReduceExpr>>) {
                auto& col = acquireTempColumn(val->result_type.kind, count);
                evalReduce(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, binder::BoundPatternComprehension>) {
                // projection_extraction must have replaced this placeholder with a
                // BoundColumnRef to the precomputed list column. Reaching here
                // means hoisting/rewrite was skipped (programmer error).
                throw std::runtime_error(
                    "BoundPatternComprehension reached evaluator — must be rewritten to BoundColumnRef");
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, count);
                evalCase(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::MAP, count);
                evalMap(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                auto& col = acquireTempColumn(val->result_type.kind, count);
                evalSubscript(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::LIST, count);
                evalSlice(*val, input, col, count);
                return {&col, true};
            } else {
                throw std::runtime_error("Unknown BoundExpression variant in ExpressionEvaluator");
            }
        },
        expr);
}

} // namespace compute
} // namespace eugraph
