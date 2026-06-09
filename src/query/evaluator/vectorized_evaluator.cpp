#include "query/evaluator/vectorized_evaluator.hpp"

#include "query/catalog/catalog.hpp"
#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

#include <cmath>
#include <spdlog/spdlog.h>

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

            if constexpr (!std::is_same_v<T, binder::BoundLiteral> && !std::is_same_v<T, binder::BoundColumnRef>) {
                spdlog::info("[evalDispatch] type={}", typeid(T).name());
            }

            if constexpr (std::is_same_v<T, binder::BoundLiteral>) {
                auto& col = acquireTempColumn(val.type.kind, count);
                evalLiteral(val, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                if (val.column_index < input.columns.size()) {
                    return {&input.columns[val.column_index], false};
                }
                spdlog::info("[evalDispatch] BoundColumnRef name='{}' idx={} but input has {} columns", val.name,
                             val.column_index, input.columns.size());
                auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, binder::BoundParameter>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, count);
                return {&col, true};
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
                // Check aggregate substitution map (used by AggregatePhysicalOp output phase)
                if (aggregate_substitutions) {
                    auto it = aggregate_substitutions->find(val->func_def);
                    if (it != aggregate_substitutions->end()) {
                        auto& col = acquireTempColumn(val->return_type.kind, count);
                        for (size_t i = 0; i < count; ++i)
                            col.setValue(i, it->second);
                        return {&col, true};
                    }
                }
                auto& col = acquireTempColumn(val->return_type.kind, count);
                evalFunctionCall(*val, input, col, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                auto inner = evaluateInternal(val->object, input);
                if (inner.column) {
                    auto& col = acquireTempColumn(binder::BoundTypeKind::VERTEX, count);
                    for (size_t i = 0; i < count; ++i) {
                        col.setValue(i, inner.column->getValue(i));
                    }
                    return {&col, true};
                }
                auto& col = acquireTempColumn(binder::BoundTypeKind::VERTEX, count);
                return {&col, true};
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                auto& col = acquireTempColumn(binder::BoundTypeKind::LIST, count);
                for (size_t i = 0; i < count; ++i) {
                    ListValue lv;
                    for (const auto& elem : val->elements) {
                        auto elem_eval = evaluateInternal(elem, input);
                        if (elem_eval.column && !elem_eval.column->isNull(i)) {
                            lv.elements.push_back(ValueStorage{elem_eval.column->getValue(i)});
                        } else {
                            lv.elements.push_back(ValueStorage{Value{}});
                        }
                    }
                    col.setValue(i, Value(std::move(lv)));
                }
                return {&col, true};
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
                auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, count);
                return {&col, true};
            }
        },
        expr);
}

void VectorizedEvaluator::evalLiteral(const binder::BoundLiteral& lit, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, lit.value);
    }
}

void VectorizedEvaluator::evalUnaryOp(const binder::BoundUnaryOp& op, const DataChunk& input, Column& result,
                                      size_t count) {
    auto operand = evaluateInternal(op.operand, input);
    if (!operand.column)
        return;

    if (op.batch_fn) {
        op.batch_fn(*operand.column, result, count);
    }
}

} // namespace compute
} // namespace eugraph
