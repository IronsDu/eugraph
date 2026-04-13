#include "compute_service/executor/expression_evaluator.hpp"

#include <cmath>
#include <stdexcept>

namespace eugraph {
namespace compute {

Value ExpressionEvaluator::evaluate(const cypher::Expression& expr, const Row& row, const Schema& schema) {
    return std::visit(
        [this, &row, &schema](const auto& ptr) -> Value {
            using T = std::decay_t<decltype(ptr)>;
            using ElementType = typename T::element_type;

            if constexpr (std::is_same_v<ElementType, cypher::Literal>) {
                return evalLiteral(*ptr);
            } else if constexpr (std::is_same_v<ElementType, cypher::Variable>) {
                return evalVariable(*ptr, row, schema);
            } else if constexpr (std::is_same_v<ElementType, cypher::BinaryOp>) {
                return evalBinaryOp(*ptr, row, schema);
            } else if constexpr (std::is_same_v<ElementType, cypher::UnaryOp>) {
                return evalUnaryOp(*ptr, row, schema);
            } else if constexpr (std::is_same_v<ElementType, cypher::PropertyAccess>) {
                return evalPropertyAccess(*ptr, row, schema);
            } else if constexpr (std::is_same_v<ElementType, cypher::FunctionCall>) {
                return evalFunctionCall(*ptr, row, schema);
            } else {
                // Unsupported expression type in Phase 1
                return Value{};
            }
        },
        expr);
}

Value ExpressionEvaluator::evalLiteral(const cypher::Literal& lit) {
    return std::visit(
        [](const auto& v) -> Value {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, cypher::NullValue>) {
                return Value{}; // monostate = null
            } else if constexpr (std::is_same_v<T, std::string>) {
                return Value(std::string(v));
            } else {
                return Value(v);
            }
        },
        lit.value);
}

Value ExpressionEvaluator::evalVariable(const cypher::Variable& var, const Row& row, const Schema& schema) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i] == var.name) {
            if (i < row.size()) {
                return row[i];
            }
            break;
        }
    }
    return Value{}; // not found → null
}

Value ExpressionEvaluator::evalBinaryOp(const cypher::BinaryOp& op, const Row& row, const Schema& schema) {
    Value left = evaluate(op.left, row, schema);
    Value right = evaluate(op.right, row, schema);

    switch (op.op) {
    case cypher::BinaryOperator::EQ:
        return Value(left == right);
    case cypher::BinaryOperator::NEQ:
        return Value(!(left == right));
    case cypher::BinaryOperator::LT:
        return std::visit(
            [](const auto& a, const auto& b) -> Value {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr ((std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) ||
                              (std::is_same_v<A, double> && std::is_same_v<B, double>)) {
                    return Value(a < b);
                }
                return Value{};
            },
            left, right);
    case cypher::BinaryOperator::GT:
        return std::visit(
            [](const auto& a, const auto& b) -> Value {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr ((std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) ||
                              (std::is_same_v<A, double> && std::is_same_v<B, double>)) {
                    return Value(a > b);
                }
                return Value{};
            },
            left, right);
    case cypher::BinaryOperator::LTE:
        return std::visit(
            [](const auto& a, const auto& b) -> Value {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr ((std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) ||
                              (std::is_same_v<A, double> && std::is_same_v<B, double>)) {
                    return Value(a <= b);
                }
                return Value{};
            },
            left, right);
    case cypher::BinaryOperator::GTE:
        return std::visit(
            [](const auto& a, const auto& b) -> Value {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr ((std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) ||
                              (std::is_same_v<A, double> && std::is_same_v<B, double>)) {
                    return Value(a >= b);
                }
                return Value{};
            },
            left, right);
    case cypher::BinaryOperator::AND: {
        bool lb = std::holds_alternative<bool>(left) ? std::get<bool>(left) : false;
        bool rb = std::holds_alternative<bool>(right) ? std::get<bool>(right) : false;
        return Value(lb && rb);
    }
    case cypher::BinaryOperator::OR: {
        bool lb = std::holds_alternative<bool>(left) ? std::get<bool>(left) : false;
        bool rb = std::holds_alternative<bool>(right) ? std::get<bool>(right) : false;
        return Value(lb || rb);
    }
    case cypher::BinaryOperator::ADD:
        return std::visit(
            [](const auto& a, const auto& b) -> Value {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) {
                    return Value(a + b);
                } else if constexpr (std::is_same_v<A, double> && std::is_same_v<B, double>) {
                    return Value(a + b);
                }
                return Value{};
            },
            left, right);
    case cypher::BinaryOperator::SUB:
        return std::visit(
            [](const auto& a, const auto& b) -> Value {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) {
                    return Value(a - b);
                } else if constexpr (std::is_same_v<A, double> && std::is_same_v<B, double>) {
                    return Value(a - b);
                }
                return Value{};
            },
            left, right);
    case cypher::BinaryOperator::MUL:
        return std::visit(
            [](const auto& a, const auto& b) -> Value {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) {
                    return Value(a * b);
                } else if constexpr (std::is_same_v<A, double> && std::is_same_v<B, double>) {
                    return Value(a * b);
                }
                return Value{};
            },
            left, right);
    case cypher::BinaryOperator::DIV:
        return std::visit(
            [](const auto& a, const auto& b) -> Value {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) {
                    return b != 0 ? Value(a / b) : Value{};
                } else if constexpr (std::is_same_v<A, double> && std::is_same_v<B, double>) {
                    return b != 0.0 ? Value(a / b) : Value{};
                }
                return Value{};
            },
            left, right);
    default:
        return Value{};
    }
}

Value ExpressionEvaluator::evalUnaryOp(const cypher::UnaryOp& op, const Row& row, const Schema& schema) {
    Value operand = evaluate(op.operand, row, schema);

    switch (op.op) {
    case cypher::UnaryOperator::NOT:
        if (std::holds_alternative<bool>(operand)) {
            return Value(!std::get<bool>(operand));
        }
        return Value{};
    case cypher::UnaryOperator::NEGATE:
        if (std::holds_alternative<int64_t>(operand)) {
            return Value(-std::get<int64_t>(operand));
        } else if (std::holds_alternative<double>(operand)) {
            return Value(-std::get<double>(operand));
        }
        return Value{};
    case cypher::UnaryOperator::IS_NULL:
        return Value(std::holds_alternative<std::monostate>(operand));
    case cypher::UnaryOperator::IS_NOT_NULL:
        return Value(!std::holds_alternative<std::monostate>(operand));
    default:
        return Value{};
    }
}

Value ExpressionEvaluator::evalPropertyAccess(const cypher::PropertyAccess& pa, const Row& row, const Schema& schema) {
    Value obj = evaluate(pa.object, row, schema);

    // Property access on VertexValue
    if (std::holds_alternative<VertexValue>(obj)) {
        const auto& vertex = std::get<VertexValue>(obj);
        if (pa.property == "id") {
            return Value(static_cast<int64_t>(vertex.id));
        }
        if (vertex.properties.has_value()) {
            // Properties are indexed by prop_id. For now, property access by name
            // requires metadata resolution (Phase 2). Return null for unknown props.
        }
        return Value{};
    }

    // Property access on EdgeValue
    if (std::holds_alternative<EdgeValue>(obj)) {
        const auto& edge = std::get<EdgeValue>(obj);
        if (pa.property == "id") {
            return Value(static_cast<int64_t>(edge.id));
        }
        return Value{};
    }

    return Value{};
}

Value ExpressionEvaluator::evalFunctionCall(const cypher::FunctionCall& fc, const Row& row, const Schema& schema) {
    // Phase 1: support basic functions
    // count, sum, etc. are handled by Aggregate operator, not here.
    // Just handle simple functions like type(), id(), labels()
    if (fc.name == "id" && fc.args.size() == 1) {
        Value arg = evaluate(fc.args[0], row, schema);
        if (std::holds_alternative<VertexValue>(arg)) {
            return Value(static_cast<int64_t>(std::get<VertexValue>(arg).id));
        } else if (std::holds_alternative<EdgeValue>(arg)) {
            return Value(static_cast<int64_t>(std::get<EdgeValue>(arg).id));
        }
    }
    return Value{};
}

} // namespace compute
} // namespace eugraph
