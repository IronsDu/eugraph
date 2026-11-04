#include "query/dataset/cell_access.hpp"
#include "query/evaluator/columnar_kernels.hpp"
#include "query/evaluator/expression_evaluator.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {
using namespace eugraph::compute::detail;

namespace {

enum class BoolLogicOp : uint8_t {
    And,
    Or,
    Xor
};

/// Read a BOOL cell without constructing Value. Returns false for NULL.
/// FLAT rows are mapped through the chunk's selection vector.
inline bool readBoolCell(const Column& col, const detail::RowIndex& rows, size_t logical, bool& value) {
    if (col.form == VectorForm::CONSTANT) {
        if (col.isNull(0))
            return false;
        value = col.getBool(0);
        return true;
    }
    const size_t physical = rows.physical(logical);
    if (!col.buffer || physical >= col.buffer->bool_data.size() || col.buffer->isNull(physical))
        return false;
    value = col.buffer->bool_data[physical] != 0;
    return true;
}

/// BOOL AND/OR/XOR with Cypher three-valued logic, without per-row Value.
bool typedBoolLogicEval(BoolLogicOp op, const Column& lhs, const Column& rhs, const detail::RowIndex& rows,
                        Column& result) {
    if (lhs.type != binder::BoundTypeKind::BOOL || rhs.type != binder::BoundTypeKind::BOOL)
        return false;
    if (result.type != binder::BoundTypeKind::BOOL || result.form != VectorForm::FLAT || !result.buffer)
        return false;
    if ((lhs.form != VectorForm::FLAT && lhs.form != VectorForm::CONSTANT) ||
        (rhs.form != VectorForm::FLAT && rhs.form != VectorForm::CONSTANT))
        return false;
    if ((lhs.form == VectorForm::FLAT && !lhs.buffer) || (rhs.form == VectorForm::FLAT && !rhs.buffer))
        return false;

    const size_t n = rows.count;
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        bool lv = false, rv = false;
        const bool lvalid = readBoolCell(lhs, rows, i, lv);
        const bool rvalid = readBoolCell(rhs, rows, i, rv);
        if (op == BoolLogicOp::And) {
            // false AND anything => false, including NULL.
            if ((lvalid && !lv) || (rvalid && !rv)) {
                result.buffer->bool_data[i] = 0;
            } else if (!lvalid || !rvalid) {
                result.setNull(i);
            } else {
                result.buffer->bool_data[i] = 1;
            }
        } else if (op == BoolLogicOp::Or) {
            if ((lvalid && lv) || (rvalid && rv)) {
                result.buffer->bool_data[i] = 1;
            } else if (!lvalid || !rvalid) {
                result.setNull(i);
            } else {
                result.buffer->bool_data[i] = 0;
            }
        } else {
            if (!lvalid || !rvalid) {
                result.setNull(i);
            } else {
                result.buffer->bool_data[i] = (lv != rv) ? 1 : 0;
            }
        }
    }
    return true;
}

/// BOOL =/<> without per-row Value. Supports FLAT and CONSTANT operands.
bool typedBoolEquality(const Column& lhs, const Column& rhs, const detail::RowIndex& rows, Column& result,
                       bool negate) {
    if (lhs.type != binder::BoundTypeKind::BOOL || rhs.type != binder::BoundTypeKind::BOOL)
        return false;
    if (result.type != binder::BoundTypeKind::BOOL || result.form != VectorForm::FLAT || !result.buffer)
        return false;
    if ((lhs.form != VectorForm::FLAT && lhs.form != VectorForm::CONSTANT) ||
        (rhs.form != VectorForm::FLAT && rhs.form != VectorForm::CONSTANT))
        return false;
    if ((lhs.form == VectorForm::FLAT && !lhs.buffer) || (rhs.form == VectorForm::FLAT && !rhs.buffer))
        return false;

    const size_t n = rows.count;
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        bool lv = false;
        bool rv = false;
        const bool lvalid = readBoolCell(lhs, rows, i, lv);
        const bool rvalid = readBoolCell(rhs, rows, i, rv);
        if (!lvalid || !rvalid) {
            result.setNull(i);
        } else {
            result.buffer->bool_data[i] = ((lv == rv) != negate) ? 1 : 0;
        }
    }
    return true;
}

/// STRING =/<> without per-row Value. Supports FLAT and CONSTANT operands;
/// DICTIONARY falls back to the generic Value kernel.
bool typedStringEquality(const Column& lhs, const Column& rhs, const detail::RowIndex& rows, Column& result,
                         bool negate) {
    if (lhs.type != binder::BoundTypeKind::STRING || rhs.type != binder::BoundTypeKind::STRING)
        return false;
    if (result.type != binder::BoundTypeKind::BOOL || result.form != VectorForm::FLAT || !result.buffer)
        return false;
    if ((lhs.form != VectorForm::FLAT && lhs.form != VectorForm::CONSTANT) ||
        (rhs.form != VectorForm::FLAT && rhs.form != VectorForm::CONSTANT))
        return false;
    if ((lhs.form == VectorForm::FLAT && !lhs.buffer) || (rhs.form == VectorForm::FLAT && !rhs.buffer))
        return false;

    const size_t n = rows.count;
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        size_t lp = 0;
        size_t rp = 0;
        bool lnull = false;
        bool rnull = false;
        if (lhs.form == VectorForm::CONSTANT) {
            lnull = lhs.isNull(0);
        } else {
            lp = rows.physical(i);
            lnull = lhs.buffer->isNull(lp);
        }
        if (rhs.form == VectorForm::CONSTANT) {
            rnull = rhs.isNull(0);
        } else {
            rp = rows.physical(i);
            rnull = rhs.buffer->isNull(rp);
        }
        if (lnull || rnull) {
            result.setNull(i);
            continue;
        }
        const std::string_view lv = columnStringView(lhs, lp);
        const std::string_view rv = columnStringView(rhs, rp);
        result.buffer->bool_data[i] = ((lv == rv) != negate) ? 1 : 0;
    }
    return true;
}

} // namespace

void ExpressionEvaluator::evalBinaryOp(const binder::BoundBinaryOp& op, const DataChunk& input, Column& result,
                                       size_t count) {
    auto left = evaluateInternal(op.left, input);
    auto right = evaluateInternal(op.right, input);

    if (!left.column || !right.column)
        return;

    using BO = cypher::BinaryOperator;

    // Operand columns are already evaluated. The selection vector applies only
    // while both operands are input columns; a temp result already has one row
    // per logical row.
    const detail::RowIndex rows =
        (!left.is_temp && !right.is_temp) ? chunkRows(input) : detail::RowIndex{nullptr, count};

    if (op.op == BO::AND || op.op == BO::OR || op.op == BO::XOR) {
        const BoolLogicOp logic = op.op == BO::AND  ? BoolLogicOp::And
                                  : op.op == BO::OR ? BoolLogicOp::Or
                                                    : BoolLogicOp::Xor;
        if (typedBoolLogicEval(logic, *left.column, *right.column, rows, result))
            return;
    }

    if (op.op == BO::IN) {
        for (size_t i = 0; i < count; ++i) {
            if (right.column->isNull(i)) {
                result.setNull(i);
                continue;
            }
            Value rv = right.column->getValue(i);
            if (!std::holds_alternative<ListValuePtr>(rv)) {
                result.setNull(i);
                continue;
            }
            const auto& list = (*std::get<ListValuePtr>(rv));
            // null IN [] → false (null is definitely not in an empty list)
            if (left.column->isNull(i)) {
                if (list.elements.empty()) {
                    result.setValue(i, Value(false));
                } else {
                    result.setNull(i);
                }
                continue;
            }
            // For scalar/string left operands the membership test can use the
            // typed cell helper; only materialise the left Value when the column
            // is a generic/heavy form.
            const bool scalar_left = left.column->type == binder::BoundTypeKind::BOOL ||
                                     left.column->type == binder::BoundTypeKind::INT64 ||
                                     left.column->type == binder::BoundTypeKind::DOUBLE ||
                                     left.column->type == binder::BoundTypeKind::STRING;
            Value lv;
            if (!scalar_left)
                lv = left.column->getValue(i);
            bool found = false;
            bool saw_null = false;
            for (const auto& elem : list.elements) {
                if (isNull(elem.value)) {
                    saw_null = true;
                    continue;
                }
                // Three-valued equality: nested nulls (e.g. `[null] IN [[null]]`)
                // must propagate to unknown, not collapse to a definitive answer.
                if (scalar_left) {
                    if (columnCellEqualsValue(*left.column, i, elem.value)) {
                        found = true;
                        break;
                    }
                    continue;
                }
                auto cmp = valueEquals(elem.value, lv);
                if (!cmp) {
                    saw_null = true;
                    continue;
                }
                if (*cmp) {
                    found = true;
                    break;
                }
            }
            if (found) {
                result.setValue(i, Value(true));
            } else if (saw_null) {
                result.setNull(i);
            } else {
                result.setValue(i, Value(false));
            }
        }
        return;
    }

    // The operand columns are already evaluated; run the typed fast path on
    // their runtime representation when possible.
    if (tryEvaluateTypedBinaryColumns(op, *left.column, *right.column, rows, result))
        return;

    if (op.fallback_fn) {
        op.fallback_fn(*left.column, *right.column, result, count);
    }
}

bool ExpressionEvaluator::tryEvaluateTypedBinaryColumns(const binder::BoundBinaryOp& op, const Column& lhs,
                                                        const Column& rhs, const detail::RowIndex& rows,
                                                        Column& result) {
    using BTK = binder::BoundTypeKind;

    if (lhs.type == BTK::INT64 && rhs.type == BTK::INT64) {
        if (op.op == cypher::BinaryOperator::ADD)
            return typedBinaryEval<int64_t, int64_t, AddOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::SUB)
            return typedBinaryEval<int64_t, int64_t, SubOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::MUL)
            return typedBinaryEval<int64_t, int64_t, MulOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::LT)
            return typedBinaryEval<int64_t, uint8_t, LtOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::GT)
            return typedBinaryEval<int64_t, uint8_t, GtOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::LTE)
            return typedBinaryEval<int64_t, uint8_t, LeOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::GTE)
            return typedBinaryEval<int64_t, uint8_t, GteOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::EQ && result.type == BTK::BOOL)
            return typedBinaryEval<int64_t, uint8_t, EqOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::NEQ && result.type == BTK::BOOL)
            return typedBinaryEval<int64_t, uint8_t, NeqOp>(lhs, rhs, rows, result);
        return false;
    }
    if (lhs.type == BTK::DOUBLE && rhs.type == BTK::DOUBLE) {
        if (op.op == cypher::BinaryOperator::ADD)
            return typedBinaryEval<double, double, AddOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::SUB)
            return typedBinaryEval<double, double, SubOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::MUL)
            return typedBinaryEval<double, double, MulOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::LT)
            return typedBinaryEval<double, uint8_t, LtOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::GT)
            return typedBinaryEval<double, uint8_t, GtOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::LTE)
            return typedBinaryEval<double, uint8_t, LeOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::GTE)
            return typedBinaryEval<double, uint8_t, GteOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::EQ && result.type == BTK::BOOL)
            return typedBinaryEval<double, uint8_t, EqOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::NEQ && result.type == BTK::BOOL)
            return typedBinaryEval<double, uint8_t, NeqOp>(lhs, rhs, rows, result);
        return false;
    }
    if (lhs.type == BTK::BOOL && rhs.type == BTK::BOOL) {
        if (op.op == cypher::BinaryOperator::EQ)
            return typedBoolEquality(lhs, rhs, rows, result, false);
        if (op.op == cypher::BinaryOperator::NEQ)
            return typedBoolEquality(lhs, rhs, rows, result, true);
    }
    if (lhs.type == BTK::STRING && rhs.type == BTK::STRING) {
        if (op.op == cypher::BinaryOperator::EQ)
            return typedStringEquality(lhs, rhs, rows, result, false);
        if (op.op == cypher::BinaryOperator::NEQ)
            return typedStringEquality(lhs, rhs, rows, result, true);
    }
    return false;
}

} // namespace compute
} // namespace eugraph
