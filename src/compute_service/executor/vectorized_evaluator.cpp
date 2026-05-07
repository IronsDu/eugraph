#include "compute_service/executor/vectorized_evaluator.hpp"
#include "compute_service/function/scalar/id_function.hpp"
#include "compute_service/function/scalar/path_functions.hpp"

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
                // Direct reference to input column — zero copy.
                // Column handles FLAT/CONSTANT/DICTIONARY internally.
                if (val.column_index < input.columns.size()) {
                    return {&input.columns[val.column_index], false};
                }
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
                auto& col = acquireTempColumn(val->result_type.kind, count);
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

    for (size_t i = 0; i < count; ++i) {
        Value lv = left.column->getValue(i);
        Value rv = right.column->getValue(i);

        Value r;
        switch (op.op) {
        case cypher::BinaryOperator::EQ:
            r = Value(lv == rv);
            break;
        case cypher::BinaryOperator::NEQ:
            r = Value(!(lv == rv));
            break;
        case cypher::BinaryOperator::AND: {
            bool lb = std::holds_alternative<bool>(lv) ? std::get<bool>(lv) : false;
            bool rb = std::holds_alternative<bool>(rv) ? std::get<bool>(rv) : false;
            r = Value(lb && rb);
            break;
        }
        case cypher::BinaryOperator::OR: {
            bool lb = std::holds_alternative<bool>(lv) ? std::get<bool>(lv) : false;
            bool rb = std::holds_alternative<bool>(rv) ? std::get<bool>(rv) : false;
            r = Value(lb || rb);
            break;
        }
        default:
            if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv)) {
                int64_t a = std::get<int64_t>(lv);
                int64_t b = std::get<int64_t>(rv);
                switch (op.op) {
                case cypher::BinaryOperator::LT:
                    r = Value(a < b);
                    break;
                case cypher::BinaryOperator::GT:
                    r = Value(a > b);
                    break;
                case cypher::BinaryOperator::LTE:
                    r = Value(a <= b);
                    break;
                case cypher::BinaryOperator::GTE:
                    r = Value(a >= b);
                    break;
                case cypher::BinaryOperator::ADD:
                    r = Value(a + b);
                    break;
                case cypher::BinaryOperator::SUB:
                    r = Value(a - b);
                    break;
                case cypher::BinaryOperator::MUL:
                    r = Value(a * b);
                    break;
                case cypher::BinaryOperator::DIV:
                    r = b != 0 ? Value(a / b) : Value{};
                    break;
                default:
                    r = Value{};
                    break;
                }
            } else if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv)) {
                double a = std::get<double>(lv);
                double b = std::get<double>(rv);
                switch (op.op) {
                case cypher::BinaryOperator::LT:
                    r = Value(a < b);
                    break;
                case cypher::BinaryOperator::GT:
                    r = Value(a > b);
                    break;
                case cypher::BinaryOperator::LTE:
                    r = Value(a <= b);
                    break;
                case cypher::BinaryOperator::GTE:
                    r = Value(a >= b);
                    break;
                case cypher::BinaryOperator::ADD:
                    r = Value(a + b);
                    break;
                case cypher::BinaryOperator::SUB:
                    r = Value(a - b);
                    break;
                case cypher::BinaryOperator::MUL:
                    r = Value(a * b);
                    break;
                case cypher::BinaryOperator::DIV:
                    r = b != 0.0 ? Value(a / b) : Value{};
                    break;
                default:
                    r = Value{};
                    break;
                }
            } else if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv)) {
                const auto& a = std::get<std::string>(lv);
                const auto& b = std::get<std::string>(rv);
                switch (op.op) {
                case cypher::BinaryOperator::EQ:
                    r = Value(a == b);
                    break;
                case cypher::BinaryOperator::NEQ:
                    r = Value(a != b);
                    break;
                case cypher::BinaryOperator::LT:
                    r = Value(a < b);
                    break;
                case cypher::BinaryOperator::GT:
                    r = Value(a > b);
                    break;
                case cypher::BinaryOperator::LTE:
                    r = Value(a <= b);
                    break;
                case cypher::BinaryOperator::GTE:
                    r = Value(a >= b);
                    break;
                case cypher::BinaryOperator::ADD:
                    r = Value(a + b);
                    break;
                default:
                    r = Value{};
                    break;
                }
            } else {
                r = Value{};
            }
            break;
        }
        result.setValue(i, r);
    }
}

void VectorizedEvaluator::evalUnaryOp(const binder::BoundUnaryOp& op, const DataChunk& input, Column& result,
                                      size_t count) {
    auto operand = evaluateInternal(op.operand, input);
    if (!operand.column)
        return;

    for (size_t i = 0; i < count; ++i) {
        Value ov = operand.column->getValue(i);

        Value r;
        switch (op.op) {
        case cypher::UnaryOperator::NOT:
            if (std::holds_alternative<bool>(ov))
                r = Value(!std::get<bool>(ov));
            break;
        case cypher::UnaryOperator::NEGATE:
            if (std::holds_alternative<int64_t>(ov))
                r = Value(-std::get<int64_t>(ov));
            else if (std::holds_alternative<double>(ov))
                r = Value(-std::get<double>(ov));
            break;
        case cypher::UnaryOperator::IS_NULL:
            r = Value(isNull(ov));
            break;
        case cypher::UnaryOperator::IS_NOT_NULL:
            r = Value(!isNull(ov));
            break;
        default:
            break;
        }
        result.setValue(i, r);
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

    std::vector<EvalResult> arg_results;
    arg_results.reserve(fc.args.size());
    for (const auto& arg : fc.args) {
        arg_results.push_back(evaluateInternal(arg, input));
    }

    const std::string& name = fc.func_def->name;

    for (size_t i = 0; i < count; ++i) {
        Value r;

        if (name == "id" && fc.args.size() == 1) {
            if (arg_results[0].column) {
                r = function::scalar::idImpl(arg_results[0].column->getValue(i));
            }
        } else if (name == "nodes" && fc.args.size() == 1) {
            if (arg_results[0].column) {
                r = function::scalar::nodesImpl(arg_results[0].column->getValue(i));
            }
        } else if (name == "relationships" && fc.args.size() == 1) {
            if (arg_results[0].column) {
                r = function::scalar::relationshipsImpl(arg_results[0].column->getValue(i));
            }
        } else if (name == "length" && fc.args.size() == 1) {
            if (arg_results[0].column) {
                r = function::scalar::lengthImpl(arg_results[0].column->getValue(i));
            }
        }
        result.setValue(i, r);
    }
}

} // namespace compute
} // namespace eugraph
