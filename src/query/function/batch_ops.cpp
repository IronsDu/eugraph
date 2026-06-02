#include "query/function/batch_ops.hpp"

#include "common/types/temporal_value.hpp"

namespace eugraph {

namespace {
bool isTemporalType(binder::BoundTypeKind k) {
    return k == binder::BoundTypeKind::DATETIME || k == binder::BoundTypeKind::TIME ||
           k == binder::BoundTypeKind::DURATION;
}
} // namespace
namespace function {
namespace {

// ==================== Generic (type-agnostic) binary batch functions ====================

void genericEqBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (left.isNull(i) || right.isNull(i)) {
            result.setNull(i);
        } else {
            result.setValue(i, Value(left.getValue(i) == right.getValue(i)));
        }
    }
}

void genericNeqBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (left.isNull(i) || right.isNull(i)) {
            result.setNull(i);
        } else {
            result.setValue(i, Value(!(left.getValue(i) == right.getValue(i))));
        }
    }
}

static void boolAndNullSafe(size_t i, const Column& left, const Column& right, Column& result) {
    Value lv = left.getValue(i);
    Value rv = right.getValue(i);
    auto lb = std::get_if<bool>(&lv);
    auto rb = std::get_if<bool>(&rv);
    // false AND anything => false
    if ((lb && !*lb) || (rb && !*rb)) {
        result.setValue(i, Value(false));
        return;
    }
    // true AND null => null; null AND anything => null
    if (!lb || !rb) {
        result.setNull(i);
        return;
    }
    result.setValue(i, Value(true));
}

void boolAndBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i)
        boolAndNullSafe(i, left, right, result);
}

void boolOrBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto lb = std::get_if<bool>(&lv);
        auto rb = std::get_if<bool>(&rv);
        // true OR anything => true
        if ((lb && *lb) || (rb && *rb)) {
            result.setValue(i, Value(true));
            continue;
        }
        // false OR null => null; null OR anything => null
        if (!lb || !rb) {
            result.setNull(i);
            continue;
        }
        result.setValue(i, Value(false));
    }
}

void boolXorBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto lb = std::get_if<bool>(&lv);
        auto rb = std::get_if<bool>(&rv);
        // null XOR anything => null
        if (!lb || !rb) {
            result.setNull(i);
            continue;
        }
        result.setValue(i, Value(*lb != *rb));
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

// ==================== Temporal helpers ====================

/// Check if two DateTimeValues have the same kind (both DATE, both LOCAL_DATETIME, or both DATETIME).
/// Cross-kind comparisons (e.g. DATE vs DATETIME) return NULL per openCypher semantics.
bool sameKind(const DateTimeValue& a, const DateTimeValue& b) {
    return a.kind == b.kind;
}

bool sameKind(const TimeValue& a, const TimeValue& b) {
    return a.kind == b.kind;
}

/// Try to extract a DateTimeValue from a Value. Returns true if successful.
bool tryGetDateTime(const Value& v, DateTimeValue& out) {
    if (std::holds_alternative<DateTimeValue>(v)) {
        out = std::get<DateTimeValue>(v);
        return true;
    }
    return false;
}

/// Try to extract a TimeValue from a Value. Returns true if successful.
bool tryGetTime(const Value& v, TimeValue& out) {
    if (std::holds_alternative<TimeValue>(v)) {
        out = std::get<TimeValue>(v);
        return true;
    }
    return false;
}

/// Try to extract a DurationValue from a Value. Returns true if successful.
bool tryGetDuration(const Value& v, DurationValue& out) {
    if (std::holds_alternative<DurationValue>(v)) {
        out = std::get<DurationValue>(v);
        return true;
    }
    return false;
}

// ==================== Temporal comparison batch functions ====================

void temporalLtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);

        // DateTimeValue vs DateTimeValue
        DateTimeValue dt_a, dt_b;
        if (tryGetDateTime(lv, dt_a) && tryGetDateTime(rv, dt_b)) {
            if (sameKind(dt_a, dt_b)) {
                result.setValue(i, Value(temporalLess(dt_a, dt_b)));
            } else {
                result.setNull(i);
            }
            continue;
        }

        // TimeValue vs TimeValue
        TimeValue t_a, t_b;
        if (tryGetTime(lv, t_a) && tryGetTime(rv, t_b)) {
            if (sameKind(t_a, t_b)) {
                result.setValue(i, Value(temporalLess(t_a, t_b)));
            } else {
                result.setNull(i);
            }
            continue;
        }

        // DurationValue vs DurationValue: compare by normalized representation
        DurationValue d_a, d_b;
        if (tryGetDuration(lv, d_a) && tryGetDuration(rv, d_b)) {
            // Duration comparison: total nanoseconds approximation
            int64_t total_a = d_a.months * 30LL * 86400LL * 1000000000LL + d_a.days * 86400LL * 1000000000LL +
                              d_a.seconds * 1000000000LL + d_a.nanos;
            int64_t total_b = d_b.months * 30LL * 86400LL * 1000000000LL + d_b.days * 86400LL * 1000000000LL +
                              d_b.seconds * 1000000000LL + d_b.nanos;
            result.setValue(i, Value(total_a < total_b));
            continue;
        }

        result.setNull(i);
    }
}

void temporalGtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);

        DateTimeValue dt_a, dt_b;
        if (tryGetDateTime(lv, dt_a) && tryGetDateTime(rv, dt_b)) {
            if (sameKind(dt_a, dt_b)) {
                result.setValue(i, Value(temporalLess(dt_b, dt_a)));
            } else {
                result.setNull(i);
            }
            continue;
        }

        TimeValue t_a, t_b;
        if (tryGetTime(lv, t_a) && tryGetTime(rv, t_b)) {
            if (sameKind(t_a, t_b)) {
                result.setValue(i, Value(temporalLess(t_b, t_a)));
            } else {
                result.setNull(i);
            }
            continue;
        }

        DurationValue d_a, d_b;
        if (tryGetDuration(lv, d_a) && tryGetDuration(rv, d_b)) {
            int64_t total_a = d_a.months * 30LL * 86400LL * 1000000000LL + d_a.days * 86400LL * 1000000000LL +
                              d_a.seconds * 1000000000LL + d_a.nanos;
            int64_t total_b = d_b.months * 30LL * 86400LL * 1000000000LL + d_b.days * 86400LL * 1000000000LL +
                              d_b.seconds * 1000000000LL + d_b.nanos;
            result.setValue(i, Value(total_a > total_b));
            continue;
        }

        result.setNull(i);
    }
}

void temporalLteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);

        DateTimeValue dt_a, dt_b;
        if (tryGetDateTime(lv, dt_a) && tryGetDateTime(rv, dt_b)) {
            if (sameKind(dt_a, dt_b)) {
                result.setValue(i, Value(!temporalLess(dt_b, dt_a)));
            } else {
                result.setNull(i);
            }
            continue;
        }

        TimeValue t_a, t_b;
        if (tryGetTime(lv, t_a) && tryGetTime(rv, t_b)) {
            if (sameKind(t_a, t_b)) {
                result.setValue(i, Value(!temporalLess(t_b, t_a)));
            } else {
                result.setNull(i);
            }
            continue;
        }

        DurationValue d_a, d_b;
        if (tryGetDuration(lv, d_a) && tryGetDuration(rv, d_b)) {
            int64_t total_a = d_a.months * 30LL * 86400LL * 1000000000LL + d_a.days * 86400LL * 1000000000LL +
                              d_a.seconds * 1000000000LL + d_a.nanos;
            int64_t total_b = d_b.months * 30LL * 86400LL * 1000000000LL + d_b.days * 86400LL * 1000000000LL +
                              d_b.seconds * 1000000000LL + d_b.nanos;
            result.setValue(i, Value(total_a <= total_b));
            continue;
        }

        result.setNull(i);
    }
}

void temporalGteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);

        DateTimeValue dt_a, dt_b;
        if (tryGetDateTime(lv, dt_a) && tryGetDateTime(rv, dt_b)) {
            if (sameKind(dt_a, dt_b)) {
                result.setValue(i, Value(!temporalLess(dt_a, dt_b)));
            } else {
                result.setNull(i);
            }
            continue;
        }

        TimeValue t_a, t_b;
        if (tryGetTime(lv, t_a) && tryGetTime(rv, t_b)) {
            if (sameKind(t_a, t_b)) {
                result.setValue(i, Value(!temporalLess(t_a, t_b)));
            } else {
                result.setNull(i);
            }
            continue;
        }

        DurationValue d_a, d_b;
        if (tryGetDuration(lv, d_a) && tryGetDuration(rv, d_b)) {
            int64_t total_a = d_a.months * 30LL * 86400LL * 1000000000LL + d_a.days * 86400LL * 1000000000LL +
                              d_a.seconds * 1000000000LL + d_a.nanos;
            int64_t total_b = d_b.months * 30LL * 86400LL * 1000000000LL + d_b.days * 86400LL * 1000000000LL +
                              d_b.seconds * 1000000000LL + d_b.nanos;
            result.setValue(i, Value(total_a >= total_b));
            continue;
        }

        result.setNull(i);
    }
}

// ==================== Temporal arithmetic batch functions ====================

void temporalAddBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);

        bool left_is_duration = std::holds_alternative<DurationValue>(lv);
        bool right_is_duration = std::holds_alternative<DurationValue>(rv);
        bool left_is_datetime = std::holds_alternative<DateTimeValue>(lv);
        bool right_is_datetime = std::holds_alternative<DateTimeValue>(rv);
        bool left_is_time = std::holds_alternative<TimeValue>(lv);
        bool right_is_time = std::holds_alternative<TimeValue>(rv);

        // Duration + Duration
        if (left_is_duration && right_is_duration) {
            result.setValue(i, Value(addDurations(std::get<DurationValue>(lv), std::get<DurationValue>(rv))));
            continue;
        }

        // DateTime + Duration
        if (left_is_datetime && right_is_duration) {
            result.setValue(i, Value(addDuration(std::get<DateTimeValue>(lv), std::get<DurationValue>(rv))));
            continue;
        }

        // Duration + DateTime
        if (left_is_duration && right_is_datetime) {
            result.setValue(i, Value(addDuration(std::get<DateTimeValue>(rv), std::get<DurationValue>(lv))));
            continue;
        }

        // Time + Duration
        if (left_is_time && right_is_duration) {
            result.setValue(i, Value(addDuration(std::get<TimeValue>(lv), std::get<DurationValue>(rv))));
            continue;
        }

        // Duration + Time
        if (left_is_duration && right_is_time) {
            result.setValue(i, Value(addDuration(std::get<TimeValue>(rv), std::get<DurationValue>(lv))));
            continue;
        }

        // Non-duration + non-duration or mismatched types: null
        result.setNull(i);
    }
}

void temporalSubBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);

        bool left_is_duration = std::holds_alternative<DurationValue>(lv);
        bool right_is_duration = std::holds_alternative<DurationValue>(rv);
        bool left_is_datetime = std::holds_alternative<DateTimeValue>(lv);
        bool right_is_datetime = std::holds_alternative<DateTimeValue>(rv);
        bool left_is_time = std::holds_alternative<TimeValue>(lv);
        bool right_is_time = std::holds_alternative<TimeValue>(rv);

        // Duration - Duration
        if (left_is_duration && right_is_duration) {
            result.setValue(i, Value(subDurations(std::get<DurationValue>(lv), std::get<DurationValue>(rv))));
            continue;
        }

        // DateTime - Duration
        if (left_is_datetime && right_is_duration) {
            result.setValue(i, Value(subDuration(std::get<DateTimeValue>(lv), std::get<DurationValue>(rv))));
            continue;
        }

        // Time - Duration
        if (left_is_time && right_is_duration) {
            result.setValue(i, Value(subDuration(std::get<TimeValue>(lv), std::get<DurationValue>(rv))));
            continue;
        }

        // DateTime - DateTime
        if (left_is_datetime && right_is_datetime) {
            result.setValue(i, Value(subtractDateTimes(std::get<DateTimeValue>(lv), std::get<DateTimeValue>(rv))));
            continue;
        }

        // Time - Time
        if (left_is_time && right_is_time) {
            result.setValue(i, Value(subtractTimes(std::get<TimeValue>(lv), std::get<TimeValue>(rv))));
            continue;
        }

        result.setNull(i);
    }
}

void temporalMulBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        bool got = false;
        bool use_double = false;
        double dbl_factor = 0;
        int64_t int_factor = 0;

        // Try Duration * number
        if (std::holds_alternative<DurationValue>(lv)) {
            if (std::holds_alternative<int64_t>(rv)) {
                int_factor = std::get<int64_t>(rv);
                got = true;
            } else if (std::holds_alternative<double>(rv)) {
                dbl_factor = std::get<double>(rv);
                use_double = true;
                got = true;
            }
            if (got) {
                if (use_double)
                    result.setValue(i, Value(mulDuration(std::get<DurationValue>(lv), dbl_factor)));
                else
                    result.setValue(i, Value(mulDuration(std::get<DurationValue>(lv), int_factor)));
                continue;
            }
        }

        // Try number * Duration
        if (std::holds_alternative<DurationValue>(rv)) {
            if (std::holds_alternative<int64_t>(lv)) {
                int_factor = std::get<int64_t>(lv);
                got = true;
            } else if (std::holds_alternative<double>(lv)) {
                dbl_factor = std::get<double>(lv);
                use_double = true;
                got = true;
            }
            if (got) {
                if (use_double)
                    result.setValue(i, Value(mulDuration(std::get<DurationValue>(rv), dbl_factor)));
                else
                    result.setValue(i, Value(mulDuration(std::get<DurationValue>(rv), int_factor)));
                continue;
            }
        }

        result.setNull(i);
    }
}

void temporalDivBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);

        if (!std::holds_alternative<DurationValue>(lv)) {
            result.setNull(i);
            continue;
        }
        const auto& dur = std::get<DurationValue>(lv);

        if (std::holds_alternative<int64_t>(rv)) {
            int64_t divisor = std::get<int64_t>(rv);
            if (divisor != 0)
                result.setValue(i, Value(divDuration(dur, divisor)));
            else
                result.setNull(i);
        } else if (std::holds_alternative<double>(rv)) {
            double divisor = std::get<double>(rv);
            if (divisor != 0.0)
                result.setValue(i, Value(divDuration(dur, divisor)));
            else
                result.setNull(i);
        } else {
            result.setNull(i);
        }
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

    // IN — scalar IN list -> bool
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
    bool left_temporal = (isTemporalType(left_type) || left_type == BTK::ANY);
    bool right_temporal = (isTemporalType(right_type) || right_type == BTK::ANY);

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
    if ((isTemporalType(left_type) || left_type == BTK::ANY) &&
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
        (isTemporalType(right_type) || right_type == BTK::ANY)) {
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
