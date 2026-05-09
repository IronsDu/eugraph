#include "compute_service/function/batch_ops.hpp"

#include <cmath>

namespace eugraph {
namespace function {
namespace {

// ==================== Generic (type-agnostic) binary batch functions ====================

void genericEqBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, Value(left.getValue(i) == right.getValue(i)));
    }
}

void genericNeqBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, Value(!(left.getValue(i) == right.getValue(i))));
    }
}

void boolAndBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        bool lb = std::holds_alternative<bool>(lv) ? std::get<bool>(lv) : false;
        bool rb = std::holds_alternative<bool>(rv) ? std::get<bool>(rv) : false;
        result.setValue(i, Value(lb && rb));
    }
}

void boolOrBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        bool lb = std::holds_alternative<bool>(lv) ? std::get<bool>(lv) : false;
        bool rb = std::holds_alternative<bool>(rv) ? std::get<bool>(rv) : false;
        result.setValue(i, Value(lb || rb));
    }
}

// ==================== Int64 arithmetic batch functions ====================

void int64AddBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) + std::get<int64_t>(rv)));
        else
            result.setNull(i);
    }
}

void int64SubBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) - std::get<int64_t>(rv)));
        else
            result.setNull(i);
    }
}

void int64MulBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) * std::get<int64_t>(rv)));
        else
            result.setNull(i);
    }
}

void int64DivBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv)) {
            int64_t b = std::get<int64_t>(rv);
            result.setValue(i, b != 0 ? Value(std::get<int64_t>(lv) / b) : Value{});
        } else {
            result.setNull(i);
        }
    }
}

// ==================== Double arithmetic batch functions ====================

void doubleAddBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::get<double>(lv) + std::get<double>(rv)));
        else
            result.setNull(i);
    }
}

void doubleSubBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::get<double>(lv) - std::get<double>(rv)));
        else
            result.setNull(i);
    }
}

void doubleMulBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::get<double>(lv) * std::get<double>(rv)));
        else
            result.setNull(i);
    }
}

void doubleDivBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv)) {
            double b = std::get<double>(rv);
            result.setValue(i, b != 0.0 ? Value(std::get<double>(lv) / b) : Value{});
        } else {
            result.setNull(i);
        }
    }
}

// ==================== Int64 comparison batch functions ====================

void int64LtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) < std::get<int64_t>(rv)));
        else
            result.setNull(i);
    }
}

void int64GtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) > std::get<int64_t>(rv)));
        else
            result.setNull(i);
    }
}

void int64LteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) <= std::get<int64_t>(rv)));
        else
            result.setNull(i);
    }
}

void int64GteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) >= std::get<int64_t>(rv)));
        else
            result.setNull(i);
    }
}

// ==================== Double comparison batch functions ====================

void doubleLtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::get<double>(lv) < std::get<double>(rv)));
        else
            result.setNull(i);
    }
}

void doubleGtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::get<double>(lv) > std::get<double>(rv)));
        else
            result.setNull(i);
    }
}

void doubleLteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::get<double>(lv) <= std::get<double>(rv)));
        else
            result.setNull(i);
    }
}

void doubleGteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::get<double>(lv) >= std::get<double>(rv)));
        else
            result.setNull(i);
    }
}

// ==================== String batch functions ====================

void stringConcatBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv))
            result.setValue(i, Value(std::get<std::string>(lv) + std::get<std::string>(rv)));
        else
            result.setNull(i);
    }
}

void stringLtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv))
            result.setValue(i, Value(std::get<std::string>(lv) < std::get<std::string>(rv)));
        else
            result.setNull(i);
    }
}

void stringGtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv))
            result.setValue(i, Value(std::get<std::string>(lv) > std::get<std::string>(rv)));
        else
            result.setNull(i);
    }
}

void stringLteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv))
            result.setValue(i, Value(std::get<std::string>(lv) <= std::get<std::string>(rv)));
        else
            result.setNull(i);
    }
}

void stringGteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv))
            result.setValue(i, Value(std::get<std::string>(lv) >= std::get<std::string>(rv)));
        else
            result.setNull(i);
    }
}

// ==================== Unary batch functions ====================

void boolNotBatch(const Column& operand, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value ov = operand.getValue(i);
        if (std::holds_alternative<bool>(ov))
            result.setValue(i, Value(!std::get<bool>(ov)));
        else
            result.setNull(i);
    }
}

void int64NegateBatch(const Column& operand, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value ov = operand.getValue(i);
        if (std::holds_alternative<int64_t>(ov))
            result.setValue(i, Value(-std::get<int64_t>(ov)));
        else
            result.setNull(i);
    }
}

void doubleNegateBatch(const Column& operand, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value ov = operand.getValue(i);
        if (std::holds_alternative<double>(ov))
            result.setValue(i, Value(-std::get<double>(ov)));
        else
            result.setNull(i);
    }
}

void isNullBatch(const Column& operand, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, Value(operand.isNull(i)));
    }
}

void isNotNullBatch(const Column& operand, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, Value(!operand.isNull(i)));
    }
}

} // anonymous namespace

// ==================== Resolver functions ====================

binder::BinaryBatchFn resolveBinaryBatchFn(cypher::BinaryOperator op, binder::BoundTypeKind left_type,
                                           binder::BoundTypeKind right_type) {
    using BO = cypher::BinaryOperator;
    using BTK = binder::BoundTypeKind;

    // EQ / NEQ — generic, works for all types via Value comparison
    if (op == BO::EQ)
        return genericEqBatch;
    if (op == BO::NEQ)
        return genericNeqBatch;

    // AND / OR — bool extraction
    if (op == BO::AND)
        return boolAndBatch;
    if (op == BO::OR)
        return boolOrBatch;

    // String-specific operations
    if (left_type == BTK::STRING && right_type == BTK::STRING) {
        switch (op) {
        case BO::ADD:
            return stringConcatBatch;
        case BO::LT:
            return stringLtBatch;
        case BO::GT:
            return stringGtBatch;
        case BO::LTE:
            return stringLteBatch;
        case BO::GTE:
            return stringGteBatch;
        default:
            return nullptr;
        }
    }

    // Int64-specific operations
    if (left_type == BTK::INT64 && right_type == BTK::INT64) {
        switch (op) {
        case BO::ADD:
            return int64AddBatch;
        case BO::SUB:
            return int64SubBatch;
        case BO::MUL:
            return int64MulBatch;
        case BO::DIV:
            return int64DivBatch;
        case BO::LT:
            return int64LtBatch;
        case BO::GT:
            return int64GtBatch;
        case BO::LTE:
            return int64LteBatch;
        case BO::GTE:
            return int64GteBatch;
        default:
            return nullptr;
        }
    }

    // Double-specific operations
    if (left_type == BTK::DOUBLE && right_type == BTK::DOUBLE) {
        switch (op) {
        case BO::ADD:
            return doubleAddBatch;
        case BO::SUB:
            return doubleSubBatch;
        case BO::MUL:
            return doubleMulBatch;
        case BO::DIV:
            return doubleDivBatch;
        case BO::LT:
            return doubleLtBatch;
        case BO::GT:
            return doubleGtBatch;
        case BO::LTE:
            return doubleLteBatch;
        case BO::GTE:
            return doubleGteBatch;
        default:
            return nullptr;
        }
    }

    return nullptr;
}

binder::UnaryBatchFn resolveUnaryBatchFn(cypher::UnaryOperator op, binder::BoundTypeKind operand_type) {
    using UO = cypher::UnaryOperator;
    using BTK = binder::BoundTypeKind;

    switch (op) {
    case UO::NOT:
        return boolNotBatch;
    case UO::NEGATE:
        if (operand_type == BTK::INT64)
            return int64NegateBatch;
        if (operand_type == BTK::DOUBLE)
            return doubleNegateBatch;
        return nullptr;
    case UO::IS_NULL:
        return isNullBatch;
    case UO::IS_NOT_NULL:
        return isNotNullBatch;
    default:
        return nullptr;
    }
}

} // namespace function
} // namespace eugraph
