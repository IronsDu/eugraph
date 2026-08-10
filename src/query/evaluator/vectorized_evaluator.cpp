#include "query/evaluator/vectorized_evaluator.hpp"

#include <stdexcept>

namespace eugraph {
namespace compute {

// ==================== Public API ====================

void VectorizedEvaluator::evaluate(const binder::BoundExpression& expr, const DataChunk& input, Column& result) {
    size_t count = input.numRows();
    result.reserve(count);

    auto eval_result = evaluateInternal(expr, input);
    if (eval_result.column && eval_result.column != &result) {
        for (size_t i = 0; i < count; ++i) {
            if (eval_result.column->isNull(i)) {
                result.setNull(i);
            } else {
                result.setValue(i, eval_result.column->getValue(i));
            }
        }
    }
}

void VectorizedEvaluator::evaluatePredicate(const binder::BoundExpression& expr, const DataChunk& input,
                                            std::vector<bool>& result) {
    size_t count = input.numRows();
    result.resize(count, false);

    auto eval_result = evaluateInternal(expr, input);
    if (!eval_result.column)
        return;

    for (size_t i = 0; i < count; ++i) {
        if (!eval_result.column->isNull(i)) {
            Value v = eval_result.column->getValue(i);
            if (std::holds_alternative<bool>(v)) {
                result[i] = std::get<bool>(v);
            }
        }
    }
}

// ==================== Internal ====================

Column& VectorizedEvaluator::acquireTempColumn(binder::BoundTypeKind type, size_t capacity) {
    temp_columns_.push_back(Column::flat(type, capacity));
    return temp_columns_.back();
}

VectorizedEvaluator::EvalResult VectorizedEvaluator::evaluateInternal(const binder::BoundExpression& expr,
                                                                      const DataChunk& input) {
    return std::visit(
        [this, &input](const auto& val) -> EvalResult {
            using T = std::decay_t<decltype(val)>;
            size_t count = input.numRows();

            if constexpr (std::is_same_v<T, binder::BoundLiteral>) {
                auto& col = acquireTempColumn(val.type.kind, count);
                evalLiteral(val, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                return evalColumnRef(val, input);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                throw std::runtime_error(
                    "BoundVariableRef reached VectorizedEvaluator — must be resolved to BoundColumnRef at bind time");
            } else if constexpr (std::is_same_v<T, binder::BoundParameter>) {
                throw std::runtime_error(
                    "BoundParameter reached VectorizedEvaluator — must be substituted at bind time");
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
            } else if constexpr (std::is_same_v<T, binder::BoundPatternComprehension>) {
                // column_rewrite must have replaced this placeholder with a
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
                throw std::runtime_error("Unknown BoundExpression variant in VectorizedEvaluator");
            }
        },
        expr);
}

} // namespace compute
} // namespace eugraph
