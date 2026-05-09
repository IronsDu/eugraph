#include "compute_service/executor/vectorized_evaluator.hpp"

#include "compute_service/planner/bound_expression/bound_expression.hpp"

#include <cmath>

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
                if (val.column_index < input.columns.size()) {
                    return {&input.columns[val.column_index], false};
                }
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
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
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
            } else {
                auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, count);
                return {&col, true};
            }
        },
        expr);
}

// ==================== Per-type evaluation ====================

void VectorizedEvaluator::evalLiteral(const binder::BoundLiteral& lit, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, lit.value);
    }
}

void VectorizedEvaluator::evalBinaryOp(const binder::BoundBinaryOp& op, const DataChunk& input, Column& result,
                                       size_t count) {
    auto left = evaluateInternal(op.left, input);
    auto right = evaluateInternal(op.right, input);

    if (!left.column || !right.column)
        return;

    if (op.batch_fn) {
        op.batch_fn(*left.column, *right.column, result, count);
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

void VectorizedEvaluator::evalPropertyRef(const binder::BoundPropertyRef& ref, const DataChunk& input, Column& result,
                                          size_t count) {
    auto obj = evaluateInternal(ref.object, input);
    if (!obj.column)
        return;

    for (size_t i = 0; i < count; ++i) {
        Value ov = obj.column->getValue(i);

        Value r;
        if (std::holds_alternative<VertexValue>(ov)) {
            const auto& vertex = std::get<VertexValue>(ov);
            std::vector<Value> found;
            for (const auto& candidate : ref.candidates) {
                auto lit = vertex.properties.find(candidate.label_id);
                if (lit != vertex.properties.end()) {
                    const auto& props = lit->second;
                    if (candidate.prop_id < props.size() && props[candidate.prop_id].has_value()) {
                        const auto& pv = *props[candidate.prop_id];
                        if (std::holds_alternative<bool>(pv))
                            found.push_back(Value(std::get<bool>(pv)));
                        else if (std::holds_alternative<int64_t>(pv))
                            found.push_back(Value(std::get<int64_t>(pv)));
                        else if (std::holds_alternative<double>(pv))
                            found.push_back(Value(std::get<double>(pv)));
                        else if (std::holds_alternative<std::string>(pv))
                            found.push_back(Value(std::get<std::string>(pv)));
                    }
                }
            }
            if (found.size() == 1) {
                r = std::move(found[0]);
            } else if (found.size() > 1) {
                ListValue lv;
                for (auto& v : found) {
                    lv.elements.push_back(ValueStorage{std::move(v)});
                }
                r = Value(std::move(lv));
            }
        } else if (std::holds_alternative<EdgeValue>(ov)) {
            const auto& edge = std::get<EdgeValue>(ov);
            if (edge.properties.has_value()) {
                for (const auto& candidate : ref.candidates) {
                    if (candidate.prop_id < edge.properties->size()) {
                        const auto& pv = (*edge.properties)[candidate.prop_id];
                        if (pv.has_value()) {
                            if (std::holds_alternative<bool>(*pv))
                                r = Value(std::get<bool>(*pv));
                            else if (std::holds_alternative<int64_t>(*pv))
                                r = Value(std::get<int64_t>(*pv));
                            else if (std::holds_alternative<double>(*pv))
                                r = Value(std::get<double>(*pv));
                            else if (std::holds_alternative<std::string>(*pv))
                                r = Value(std::get<std::string>(*pv));
                            break;
                        }
                    }
                }
            }
        }
        result.setValue(i, r);
    }
}

void VectorizedEvaluator::evalFunctionCall(const binder::BoundFunctionCall& fc, const DataChunk& input, Column& result,
                                           size_t count) {
    if (!fc.func_def)
        return;

    // Evaluate all arguments first
    std::vector<EvalResult> arg_results;
    arg_results.reserve(fc.args.size());
    for (const auto& arg : fc.args) {
        arg_results.push_back(evaluateInternal(arg, input));
    }

    // Use batch function if available
    if (fc.func_def->batch_scalar_fn) {
        std::vector<const Column*> arg_cols;
        arg_cols.reserve(arg_results.size());
        for (const auto& ar : arg_results) {
            arg_cols.push_back(ar.column);
        }
        fc.func_def->batch_scalar_fn(arg_cols, result, count);
        return;
    }

    // Fallback: per-row scalar function call
    if (!fc.func_def->scalar_fn)
        return;

    for (size_t i = 0; i < count; ++i) {
        std::vector<Value> args;
        args.reserve(arg_results.size());
        for (const auto& ar : arg_results) {
            if (ar.column) {
                args.push_back(ar.column->getValue(i));
            } else {
                args.emplace_back();
            }
        }
        result.setValue(i, fc.func_def->scalar_fn(args));
    }
}

} // namespace compute
} // namespace eugraph
