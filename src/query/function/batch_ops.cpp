#include "query/function/batch_ops.hpp"

#include "common/types/temporal_value.hpp"
#include "query/function/scalar/temporal_functions.hpp"

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

void boolXorBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        bool lb = std::holds_alternative<bool>(lv) ? std::get<bool>(lv) : false;
        bool rb = std::holds_alternative<bool>(rv) ? std::get<bool>(rv) : false;
        result.setValue(i, Value(lb ^ rb));
    }
}

void inBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (left.isNull(i) || right.isNull(i)) {
            result.setNull(i);
            continue;
        }
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (!std::holds_alternative<ListValue>(rv)) {
            result.setNull(i);
            continue;
        }
        const auto& list = std::get<ListValue>(rv);
        bool found = false;
        for (const auto& elem : list.elements) {
            if (!eugraph::isNull(elem.value) && elem.value == lv) {
                found = true;
                break;
            }
        }
        result.setValue(i, Value(found));
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

void stringStartsWithBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv)) {
            const auto& str = std::get<std::string>(lv);
            const auto& prefix = std::get<std::string>(rv);
            result.setValue(i, Value(prefix.empty() ||
                                     (str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0)));
        } else {
            result.setNull(i);
        }
    }
}

void stringEndsWithBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv)) {
            const auto& str = std::get<std::string>(lv);
            const auto& suffix = std::get<std::string>(rv);
            result.setValue(
                i, Value(suffix.empty() || (str.size() >= suffix.size() &&
                                            str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0)));
        } else {
            result.setNull(i);
        }
    }
}

void stringContainsBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv)) {
            const auto& str = std::get<std::string>(lv);
            const auto& sub = std::get<std::string>(rv);
            result.setValue(i, Value(str.find(sub) != std::string::npos));
        } else {
            result.setNull(i);
        }
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

// ==================== Temporal helper ====================

namespace {

using eugraph::function::scalar::parseDateFromString;
using eugraph::function::scalar::parseDatetimeStr;
using eugraph::function::scalar::parseDurationFromString;
using eugraph::function::scalar::parseTimeStr;

TemporalValue valueToTemporal(const Value& v) {
    if (std::holds_alternative<TemporalValue>(v))
        return std::get<TemporalValue>(v);
    if (std::holds_alternative<std::string>(v)) {
        const auto& s = std::get<std::string>(v);
        TemporalValue tv = parseDateFromString(s);
        if (tv.year != 1970 || tv.month != 1 || tv.day != 1)
            return tv;
        tv = parseDatetimeStr(s, TemporalKind::DATETIME);
        if (tv.year != 1970 || tv.month != 1 || tv.day != 1)
            return tv;
        tv = parseDatetimeStr(s, TemporalKind::LOCAL_DATETIME);
        if (tv.year != 1970 || tv.month != 1 || tv.day != 1)
            return tv;
        tv = parseTimeStr(s, TemporalKind::TIME);
        if (tv.hour != 0 || tv.minute != 0)
            return tv;
        tv = parseTimeStr(s, TemporalKind::LOCAL_TIME);
        if (tv.hour != 0 || tv.minute != 0)
            return tv;
        tv = parseDurationFromString(s);
        if (tv.dur_months != 0 || tv.dur_days != 0 || tv.dur_seconds != 0 || tv.dur_nanos != 0)
            return tv;
    }
    return TemporalValue{TemporalKind::DATE};
}

bool isValidTemporal(const TemporalValue& tv) {
    if (tv.kind == TemporalKind::DURATION)
        return tv.dur_months != 0 || tv.dur_days != 0 || tv.dur_seconds != 0 || tv.dur_nanos != 0;
    if (tv.kind == TemporalKind::LOCAL_TIME || tv.kind == TemporalKind::TIME)
        return tv.hour != 0 || tv.minute != 0 || tv.second != 0 || tv.nanos != 0;
    return tv.year != 1970 || tv.month != 1 || tv.day != 1 || tv.hour != 0 || tv.minute != 0 || tv.second != 0 ||
           tv.nanos != 0;
}

} // namespace

// ==================== Temporal comparison batch functions ====================

void temporalLtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto a = valueToTemporal(lv);
        auto b = valueToTemporal(rv);
        if (isValidTemporal(a) && isValidTemporal(b)) {
            if (a.kind == b.kind)
                result.setValue(i, Value(temporalLess(a, b)));
            else
                result.setNull(i);
        } else {
            result.setNull(i);
        }
    }
}

void temporalGtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto a = valueToTemporal(lv);
        auto b = valueToTemporal(rv);
        if (isValidTemporal(a) && isValidTemporal(b)) {
            if (a.kind == b.kind)
                result.setValue(i, Value(temporalLess(b, a)));
            else
                result.setNull(i);
        } else {
            result.setNull(i);
        }
    }
}

void temporalLteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto a = valueToTemporal(lv);
        auto b = valueToTemporal(rv);
        if (isValidTemporal(a) && isValidTemporal(b)) {
            if (a.kind == b.kind)
                result.setValue(i, Value(!temporalLess(b, a)));
            else
                result.setNull(i);
        } else {
            result.setNull(i);
        }
    }
}

void temporalGteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto a = valueToTemporal(lv);
        auto b = valueToTemporal(rv);
        if (isValidTemporal(a) && isValidTemporal(b)) {
            if (a.kind == b.kind)
                result.setValue(i, Value(!temporalLess(a, b)));
            else
                result.setNull(i);
        } else {
            result.setNull(i);
        }
    }
}

// ==================== Temporal arithmetic batch functions ====================

void temporalAddBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto a = valueToTemporal(lv);
        auto b = valueToTemporal(rv);
        if (!isValidTemporal(a) || !isValidTemporal(b)) {
            result.setNull(i);
            continue;
        }
        bool la = (a.kind == TemporalKind::DURATION);
        bool lb = (b.kind == TemporalKind::DURATION);
        if (la && lb)
            result.setValue(i, Value(addDurations(a, b)));
        else if (!la && lb)
            result.setValue(i, Value(addDurationToTemporal(a, b)));
        else if (la && !lb)
            result.setValue(i, Value(addDurationToTemporal(b, a)));
        else
            result.setNull(i);
    }
}

void temporalSubBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto a = valueToTemporal(lv);
        auto b = valueToTemporal(rv);
        if (!isValidTemporal(a) || !isValidTemporal(b)) {
            result.setNull(i);
            continue;
        }
        bool la = (a.kind == TemporalKind::DURATION);
        bool lb = (b.kind == TemporalKind::DURATION);
        if (la && lb)
            result.setValue(i, Value(subDurations(a, b)));
        else if (!la && lb)
            result.setValue(i, Value(subDurationFromTemporal(a, b)));
        else if (!la && !lb && a.kind == b.kind)
            result.setValue(i, Value(subtractTemporals(a, b)));
        else
            result.setNull(i);
    }
}

void temporalMulBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        TemporalValue dur;
        int64_t factor = 0;
        bool got = false;
        // Try temporal * number
        auto tv = valueToTemporal(lv);
        if (isValidTemporal(tv)) {
            if (std::holds_alternative<int64_t>(rv)) {
                dur = tv;
                factor = std::get<int64_t>(rv);
                got = true;
            } else if (std::holds_alternative<double>(rv)) {
                dur = tv;
                factor = static_cast<int64_t>(std::get<double>(rv));
                got = true;
            }
        }
        // Try number * temporal
        if (!got) {
            tv = valueToTemporal(rv);
            if (isValidTemporal(tv)) {
                if (std::holds_alternative<int64_t>(lv)) {
                    dur = tv;
                    factor = std::get<int64_t>(lv);
                    got = true;
                } else if (std::holds_alternative<double>(lv)) {
                    dur = tv;
                    factor = static_cast<int64_t>(std::get<double>(lv));
                    got = true;
                }
            }
        }
        if (got && dur.kind == TemporalKind::DURATION)
            result.setValue(i, Value(mulDuration(dur, factor)));
        else
            result.setNull(i);
    }
}

void temporalDivBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto dur = valueToTemporal(lv);
        if (!isValidTemporal(dur)) {
            result.setNull(i);
            continue;
        }
        if (dur.kind != TemporalKind::DURATION) {
            result.setNull(i);
            continue;
        }
        int64_t divisor = 0;
        if (std::holds_alternative<int64_t>(rv))
            divisor = std::get<int64_t>(rv);
        else if (std::holds_alternative<double>(rv))
            divisor = static_cast<int64_t>(std::get<double>(rv));
        else {
            result.setNull(i);
            continue;
        }
        if (divisor != 0)
            result.setValue(i, Value(divDuration(dur, divisor)));
        else
            result.setNull(i);
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

    // AND / OR / XOR — bool extraction
    if (op == BO::AND)
        return boolAndBatch;
    if (op == BO::OR)
        return boolOrBatch;
    if (op == BO::XOR)
        return boolXorBatch;

    // IN — scalar IN list → bool
    if (op == BO::IN)
        return inBatch;

    // String-specific operations (accept ANY for property round-trip)
    if ((left_type == BTK::STRING || left_type == BTK::ANY) && (right_type == BTK::STRING || right_type == BTK::ANY)) {
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
        case BO::STARTS_WITH:
            return stringStartsWithBatch;
        case BO::ENDS_WITH:
            return stringEndsWithBatch;
        case BO::CONTAINS:
            return stringContainsBatch;
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

    // Temporal: accept ANY as temporal (for property round-trip)
    bool left_temporal = (left_type == BTK::TEMPORAL || left_type == BTK::ANY);
    bool right_temporal = (right_type == BTK::TEMPORAL || right_type == BTK::ANY);

    // Temporal-specific: TEMPORAL + TEMPORAL (or ANY variants)
    if (left_temporal && right_temporal) {
        switch (op) {
        case BO::LT:
            return temporalLtBatch;
        case BO::GT:
            return temporalGtBatch;
        case BO::LTE:
            return temporalLteBatch;
        case BO::GTE:
            return temporalGteBatch;
        case BO::ADD:
            return temporalAddBatch;
        case BO::SUB:
            return temporalSubBatch;
        default:
            return nullptr;
        }
    }

    // Temporal * number  /  Temporal / number  (accept ANY as temporal)
    if ((left_type == BTK::TEMPORAL || left_type == BTK::ANY) &&
        (right_type == BTK::INT64 || right_type == BTK::DOUBLE)) {
        switch (op) {
        case BO::MUL:
            return temporalMulBatch;
        case BO::DIV:
            return temporalDivBatch;
        default:
            return nullptr;
        }
    }

    // number * Temporal  (commutative MUL, accept ANY as temporal)
    if ((left_type == BTK::INT64 || left_type == BTK::DOUBLE) &&
        (right_type == BTK::TEMPORAL || right_type == BTK::ANY)) {
        switch (op) {
        case BO::MUL:
            return temporalMulBatch;
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
