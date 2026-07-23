#include "query/function/batch_ops.hpp"
#include "query/function/compare_ops.hpp"

#include "common/types/temporal_value.hpp"

#include <cmath>

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

// Fallback: all-null result for incomparable type combinations (ordered comparison only).
void nullCmpBatch(const Column& /*left*/, const Column& /*right*/, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i)
        result.setNull(i);
}

void genericEqBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto cmp = valueEquals(left.getValue(i), right.getValue(i));
        if (!cmp)
            result.setNull(i);
        else
            result.setValue(i, Value(*cmp));
    }
}

void genericNeqBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto cmp = valueEquals(left.getValue(i), right.getValue(i));
        if (!cmp)
            result.setNull(i);
        else
            result.setValue(i, Value(!*cmp));
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
        bool saw_null = false;
        for (const auto& elem : list.elements) {
            // Use three-valued equality so nested nulls propagate correctly:
            // [1,2] IN [[null,2]] must return null, not false.
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
}

// ==================== List batch functions ====================

void listConcatBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<ListValue>(lv) && std::holds_alternative<ListValue>(rv)) {
            ListValue result_list = std::get<ListValue>(lv);
            const auto& rlist = std::get<ListValue>(rv);
            result_list.elements.insert(result_list.elements.end(), rlist.elements.begin(), rlist.elements.end());
            result.setValue(i, Value(std::move(result_list)));
        } else if (std::holds_alternative<ListValue>(lv)) {
            // list + scalar: append scalar as single element
            ListValue result_list = std::get<ListValue>(lv);
            result_list.elements.push_back(ValueStorage{rv});
            result.setValue(i, Value(std::move(result_list)));
        } else if (std::holds_alternative<ListValue>(rv)) {
            // scalar + list: prepend scalar
            ListValue result_list;
            result_list.elements.push_back(ValueStorage{lv});
            const auto& rlist = std::get<ListValue>(rv);
            result_list.elements.insert(result_list.elements.end(), rlist.elements.begin(), rlist.elements.end());
            result.setValue(i, Value(std::move(result_list)));
        } else {
            result.setNull(i);
        }
    }
}

// List ordered comparison — element-by-element with null propagation.
// On first element that differs, the result is determined by that element's ordering.
// If all elements equal, the longer list is "greater".
// Null elements propagate: if any comparison returns unknown, the overall result is null.
namespace {

// Returns true/false/null for left CMP right on list values.
// Cmp should be a functor on int: e.g., std::greater<int> for >, std::less<int> for <, etc.
template <typename Cmp>
void listCmpBatchImpl(const Column& left, const Column& right, Column& result, size_t count, bool equalLenResult) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (!std::holds_alternative<ListValue>(lv) || !std::holds_alternative<ListValue>(rv)) {
            result.setNull(i);
            continue;
        }
        const auto& la = std::get<ListValue>(lv);
        const auto& lb = std::get<ListValue>(rv);
        size_t min_len = std::min(la.elements.size(), lb.elements.size());
        bool saw_null = false;
        for (size_t j = 0; j < min_len; ++j) {
            auto cmp = compareValues(la.elements[j].value, lb.elements[j].value);
            if (!cmp) {
                saw_null = true;
                continue;
            }
            if (*cmp != 0) {
                result.setValue(i, Value(Cmp{}(*cmp, 0)));
                goto next_row;
            }
        }
        // All elements equal up to min_len: compare lengths
        if (saw_null)
            result.setNull(i);
        else if (la.elements.size() == lb.elements.size())
            result.setValue(i, Value(equalLenResult));
        else
            result.setValue(i,
                            Value(Cmp{}(static_cast<int>(la.elements.size()), static_cast<int>(lb.elements.size()))));
    next_row:;
    }
}

} // namespace

void listGteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    listCmpBatchImpl<std::greater_equal<int>>(left, right, result, count, true);
}
void listGtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    listCmpBatchImpl<std::greater<int>>(left, right, result, count, false);
}
void listLteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    listCmpBatchImpl<std::less_equal<int>>(left, right, result, count, true);
}
void listLtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    listCmpBatchImpl<std::less<int>>(left, right, result, count, false);
}

// ==================== Generic numeric dispatch (ANY + ANY fallback) ====================

namespace {

// Extract numeric values for arithmetic: promotes int64→double when mixed.
// Returns nullopt when either operand is non-numeric (not int64/double).
inline std::optional<std::pair<double, double>> numericPair(const Value& lv, const Value& rv) {
    if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
        return std::pair{std::get<double>(lv), std::get<double>(rv)};
    if (std::holds_alternative<double>(lv) && std::holds_alternative<int64_t>(rv))
        return std::pair{std::get<double>(lv), static_cast<double>(std::get<int64_t>(rv))};
    if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<double>(rv))
        return std::pair{static_cast<double>(std::get<int64_t>(lv)), std::get<double>(rv)};
    if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
        return std::pair{static_cast<double>(std::get<int64_t>(lv)), static_cast<double>(std::get<int64_t>(rv))};
    return std::nullopt;
}

} // namespace

void genericAddBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        // lists: concatenation, append, or prepend
        if (std::holds_alternative<ListValue>(lv) || std::holds_alternative<ListValue>(rv)) {
            if (std::holds_alternative<ListValue>(lv) && std::holds_alternative<ListValue>(rv)) {
                ListValue res = std::get<ListValue>(lv);
                const auto& rlist = std::get<ListValue>(rv);
                res.elements.insert(res.elements.end(), rlist.elements.begin(), rlist.elements.end());
                result.setValue(i, Value(std::move(res)));
            } else if (std::holds_alternative<ListValue>(lv)) {
                ListValue res = std::get<ListValue>(lv);
                res.elements.push_back(ValueStorage{rv});
                result.setValue(i, Value(std::move(res)));
            } else {
                ListValue res;
                res.elements.push_back(ValueStorage{lv});
                const auto& rlist = std::get<ListValue>(rv);
                res.elements.insert(res.elements.end(), rlist.elements.begin(), rlist.elements.end());
                result.setValue(i, Value(std::move(res)));
            }
        } else if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) + std::get<int64_t>(rv)));
        else if (std::holds_alternative<std::string>(lv) && std::holds_alternative<std::string>(rv))
            result.setValue(i, Value(std::get<std::string>(lv) + std::get<std::string>(rv)));
        else if (std::holds_alternative<DurationValue>(lv) || std::holds_alternative<DurationValue>(rv) ||
                 std::holds_alternative<DateTimeValue>(lv) || std::holds_alternative<DateTimeValue>(rv) ||
                 std::holds_alternative<TimeValue>(lv) || std::holds_alternative<TimeValue>(rv)) {
            bool left_is_duration = std::holds_alternative<DurationValue>(lv);
            bool right_is_duration = std::holds_alternative<DurationValue>(rv);
            bool left_is_datetime = std::holds_alternative<DateTimeValue>(lv);
            bool right_is_datetime = std::holds_alternative<DateTimeValue>(rv);
            bool left_is_time = std::holds_alternative<TimeValue>(lv);
            bool right_is_time = std::holds_alternative<TimeValue>(rv);

            if (left_is_duration && right_is_duration) {
                result.setValue(i, Value(addDurations(std::get<DurationValue>(lv), std::get<DurationValue>(rv))));
            } else if (left_is_datetime && right_is_duration) {
                result.setValue(i, Value(addDuration(std::get<DateTimeValue>(lv), std::get<DurationValue>(rv))));
            } else if (left_is_duration && right_is_datetime) {
                result.setValue(i, Value(addDuration(std::get<DateTimeValue>(rv), std::get<DurationValue>(lv))));
            } else if (left_is_time && right_is_duration) {
                result.setValue(i, Value(addDuration(std::get<TimeValue>(lv), std::get<DurationValue>(rv))));
            } else if (left_is_duration && right_is_time) {
                result.setValue(i, Value(addDuration(std::get<TimeValue>(rv), std::get<DurationValue>(lv))));
            } else {
                result.setNull(i);
            }
        } else {
            auto pair = numericPair(lv, rv);
            if (pair)
                result.setValue(i, Value(pair->first + pair->second));
            else
                result.setNull(i);
        }
    }
}

void genericSubBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<DurationValue>(lv) || std::holds_alternative<DurationValue>(rv) ||
            std::holds_alternative<DateTimeValue>(lv) || std::holds_alternative<DateTimeValue>(rv) ||
            std::holds_alternative<TimeValue>(lv) || std::holds_alternative<TimeValue>(rv)) {
            bool left_is_duration = std::holds_alternative<DurationValue>(lv);
            bool right_is_duration = std::holds_alternative<DurationValue>(rv);
            bool left_is_datetime = std::holds_alternative<DateTimeValue>(lv);
            bool right_is_datetime = std::holds_alternative<DateTimeValue>(rv);
            bool left_is_time = std::holds_alternative<TimeValue>(lv);
            bool right_is_time = std::holds_alternative<TimeValue>(rv);

            if (left_is_duration && right_is_duration) {
                auto neg = std::get<DurationValue>(rv);
                neg.months = -neg.months;
                neg.days = -neg.days;
                neg.seconds = -neg.seconds;
                neg.nanos = -neg.nanos;
                result.setValue(i, Value(addDurations(std::get<DurationValue>(lv), neg)));
            } else if (left_is_datetime && right_is_duration) {
                auto neg = std::get<DurationValue>(rv);
                neg.months = -neg.months;
                neg.days = -neg.days;
                neg.seconds = -neg.seconds;
                neg.nanos = -neg.nanos;
                result.setValue(i, Value(addDuration(std::get<DateTimeValue>(lv), neg)));
            } else if (left_is_duration && right_is_datetime) {
                result.setNull(i);
            } else if (left_is_time && right_is_duration) {
                auto neg = std::get<DurationValue>(rv);
                neg.months = -neg.months;
                neg.days = -neg.days;
                neg.seconds = -neg.seconds;
                neg.nanos = -neg.nanos;
                result.setValue(i, Value(addDuration(std::get<TimeValue>(lv), neg)));
            } else if (left_is_duration && right_is_time) {
                result.setNull(i);
            } else {
                result.setNull(i);
            }
        } else {
            auto pair = numericPair(lv, rv);
            if (pair)
                result.setValue(i, Value(pair->first - pair->second));
            else
                result.setNull(i);
        }
    }
}

void genericMulBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<DurationValue>(lv) && std::holds_alternative<double>(rv)) {
            result.setValue(i, Value(mulDuration(std::get<DurationValue>(lv), std::get<double>(rv))));
        } else if (std::holds_alternative<double>(lv) && std::holds_alternative<DurationValue>(rv)) {
            result.setValue(i, Value(mulDuration(std::get<DurationValue>(rv), std::get<double>(lv))));
        } else if (std::holds_alternative<DurationValue>(lv) && std::holds_alternative<int64_t>(rv)) {
            result.setValue(
                i, Value(mulDuration(std::get<DurationValue>(lv), static_cast<double>(std::get<int64_t>(rv)))));
        } else if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<DurationValue>(rv)) {
            result.setValue(
                i, Value(mulDuration(std::get<DurationValue>(rv), static_cast<double>(std::get<int64_t>(lv)))));
        } else {
            auto pair = numericPair(lv, rv);
            if (pair)
                result.setValue(i, Value(pair->first * pair->second));
            else
                result.setNull(i);
        }
    }
}

void genericDivBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<DurationValue>(lv) && std::holds_alternative<double>(rv)) {
            double d = std::get<double>(rv);
            if (d == 0.0)
                result.setNull(i);
            else
                result.setValue(i, Value(mulDuration(std::get<DurationValue>(lv), 1.0 / d)));
        } else if (std::holds_alternative<DurationValue>(lv) && std::holds_alternative<int64_t>(rv)) {
            int64_t div = std::get<int64_t>(rv);
            if (div == 0)
                result.setNull(i);
            else
                result.setValue(i, Value(mulDuration(std::get<DurationValue>(lv), 1.0 / static_cast<double>(div))));
        } else {
            auto pair = numericPair(lv, rv);
            if (pair)
                result.setValue(i, Value(pair->first / pair->second));
            else
                result.setNull(i);
        }
    }
}

void genericPowBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto pair = numericPair(left.getValue(i), right.getValue(i));
        if (pair)
            result.setValue(i, Value(std::pow(pair->first, pair->second)));
        else
            result.setNull(i);
    }
}

void genericModBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv))
            result.setValue(i, Value(std::get<int64_t>(lv) % std::get<int64_t>(rv)));
        else {
            auto pair = numericPair(lv, rv);
            if (pair)
                result.setValue(i, Value(std::fmod(pair->first, pair->second)));
            else
                result.setNull(i);
        }
    }
}

#define DEF_GENERIC_CMP_BATCH(name, op)                                                                                \
    void name(const Column& left, const Column& right, Column& result, size_t count) {                                 \
        for (size_t i = 0; i < count; ++i) {                                                                           \
            Value lv = left.getValue(i);                                                                               \
            Value rv = right.getValue(i);                                                                              \
            if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv)) {                          \
                bool cmp = std::get<int64_t>(lv) op std::get<int64_t>(rv);                                             \
                result.setValue(i, Value(cmp));                                                                        \
            } else {                                                                                                   \
                auto pair = numericPair(lv, rv);                                                                       \
                if (pair) {                                                                                            \
                    bool cmp = pair->first op pair->second;                                                            \
                    result.setValue(i, Value(cmp));                                                                    \
                } else {                                                                                               \
                    if (compute::cypherTypeCategory(lv) != compute::cypherTypeCategory(rv)) {                          \
                        result.setNull(i);                                                                             \
                    } else {                                                                                           \
                        int order = compute::cypherCompareValues(lv, rv);                                              \
                        result.setValue(i, Value(order op 0));                                                         \
                    }                                                                                                  \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    }

DEF_GENERIC_CMP_BATCH(genericLtBatch, <)
DEF_GENERIC_CMP_BATCH(genericGtBatch, >)
DEF_GENERIC_CMP_BATCH(genericLteBatch, <=)
DEF_GENERIC_CMP_BATCH(genericGteBatch, >=)

#undef DEF_GENERIC_NUMERIC_BATCH
#undef DEF_GENERIC_CMP_BATCH

// ==================== Int64 arithmetic batch functions ====================

void int64AddBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv)) {
            result.setValue(i, Value(std::get<int64_t>(lv) + std::get<int64_t>(rv)));
        } else {
            result.setNull(i);
        }
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
        else {
            result.setNull(i);
        }
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

void int64ModBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv)) {
            int64_t b = std::get<int64_t>(rv);
            result.setValue(i, b != 0 ? Value(std::get<int64_t>(lv) % b) : Value{});
        } else {
            result.setNull(i);
        }
    }
}

void int64PowBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv)) {
            double base = static_cast<double>(std::get<int64_t>(lv));
            double exp = static_cast<double>(std::get<int64_t>(rv));
            result.setValue(i, Value(std::pow(base, exp)));
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
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::get<double>(lv) / std::get<double>(rv)));
        else
            result.setNull(i);
    }
}

void doubleModBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::fmod(std::get<double>(lv), std::get<double>(rv))));
        else
            result.setNull(i);
    }
}

void doublePowBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        if (std::holds_alternative<double>(lv) && std::holds_alternative<double>(rv))
            result.setValue(i, Value(std::pow(std::get<double>(lv), std::get<double>(rv))));
        else
            result.setNull(i);
    }
}

// ==================== Bool comparison batch functions ====================
// Bool ordering: true > false (true is "greater").
// Null operands propagate: if either operand is null the result is null.

void boolLtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto lb = std::get_if<bool>(&lv);
        auto rb = std::get_if<bool>(&rv);
        if (lb && rb)
            result.setValue(i, Value(!*lb && *rb));
        else
            result.setNull(i);
    }
}

void boolGtBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto lb = std::get_if<bool>(&lv);
        auto rb = std::get_if<bool>(&rv);
        if (lb && rb)
            result.setValue(i, Value(*lb && !*rb));
        else
            result.setNull(i);
    }
}

void boolLteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto lb = std::get_if<bool>(&lv);
        auto rb = std::get_if<bool>(&rv);
        if (lb && rb)
            result.setValue(i, Value(*lb <= *rb));
        else
            result.setNull(i);
    }
}

void boolGteBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);
        auto lb = std::get_if<bool>(&lv);
        auto rb = std::get_if<bool>(&rv);
        if (lb && rb)
            result.setValue(i, Value(*lb >= *rb));
        else
            result.setNull(i);
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
        if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv)) {
            bool cmp = std::get<int64_t>(lv) > std::get<int64_t>(rv);
            result.setValue(i, Value(cmp));
        } else {
            result.setNull(i);
        }
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

        // Not a temporal operation: fall back to generic numeric addition
        auto pair = numericPair(lv, rv);
        if (pair)
            result.setValue(i, Value(pair->first + pair->second));
        else
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

        // Not a temporal operation: fall back to generic numeric subtraction
        auto pair = numericPair(lv, rv);
        if (pair)
            result.setValue(i, Value(pair->first - pair->second));
        else
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

        // Not a Duration operation: fall back to generic numeric multiplication
        auto pair = numericPair(lv, rv);
        if (pair)
            result.setValue(i, Value(pair->first * pair->second));
        else
            result.setNull(i);
    }
}

void temporalDivBatch(const Column& left, const Column& right, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Value lv = left.getValue(i);
        Value rv = right.getValue(i);

        if (!std::holds_alternative<DurationValue>(lv)) {
            auto pair = numericPair(lv, rv);
            if (pair)
                result.setValue(i, Value(pair->first / pair->second));
            else
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
            auto pair = numericPair(lv, rv);
            if (pair)
                result.setValue(i, Value(pair->first / pair->second));
            else
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

    // STARTS_WITH / ENDS_WITH / CONTAINS: accept any types.
    // Non-string operands return null at runtime via the string batch functions.
    if (op == BO::STARTS_WITH)
        return stringStartsWithBatch;
    if (op == BO::ENDS_WITH)
        return stringEndsWithBatch;
    if (op == BO::CONTAINS)
        return stringContainsBatch;

    // Bool ordered comparison (true > false).
    if (left_type == BTK::BOOL && right_type == BTK::BOOL) {
        switch (op) {
        case BO::LT:
            return boolLtBatch;
        case BO::GT:
            return boolGtBatch;
        case BO::LTE:
            return boolLteBatch;
        case BO::GTE:
            return boolGteBatch;
        default:
            return nullptr;
        }
    }

    // String-specific operations (accept NULL for null operands)
    if ((left_type == BTK::STRING || left_type == BTK::NULL_TYPE) &&
        (right_type == BTK::STRING || right_type == BTK::NULL_TYPE)) {
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
        case BO::MOD:
            return int64ModBatch;
        case BO::POW:
            return int64PowBatch;
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
        case BO::MOD:
            return doubleModBatch;
        case BO::POW:
            return doublePowBatch;
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

    // INT64+DOUBLE cross-type: promote int64→double at runtime.
    if ((left_type == BTK::INT64 && right_type == BTK::DOUBLE) ||
        (left_type == BTK::DOUBLE && right_type == BTK::INT64)) {
        switch (op) {
        case BO::ADD:
            return genericAddBatch;
        case BO::SUB:
            return genericSubBatch;
        case BO::MUL:
            return genericMulBatch;
        case BO::DIV:
            return genericDivBatch;
        case BO::MOD:
            return genericModBatch;
        case BO::POW:
            return genericPowBatch;
        case BO::LT:
            return genericLtBatch;
        case BO::GT:
            return genericGtBatch;
        case BO::LTE:
            return genericLteBatch;
        case BO::GTE:
            return genericGteBatch;
        default:
            return nullptr;
        }
    }

    // Temporal * number / Temporal / number (must precede ANY fallback
    // so that ANY-typed temporal properties dispatch correctly).
    // Only intercept MUL/DIV; other ops fall through to generic dispatch.
    if ((isTemporalType(left_type) || left_type == BTK::ANY) &&
        (right_type == BTK::INT64 || right_type == BTK::DOUBLE)) {
        switch (op) {
        case BO::MUL:
            return temporalMulBatch;
        case BO::DIV:
            return temporalDivBatch;
        default:
            break;
        }
    }

    // number * Temporal (commutative MUL, accept ANY as temporal)
    if ((left_type == BTK::INT64 || left_type == BTK::DOUBLE) &&
        (isTemporalType(right_type) || right_type == BTK::ANY)) {
        switch (op) {
        case BO::MUL:
            return temporalMulBatch;
        default:
            break;
        }
    }

    // ANY-type fallback: properties stored as ANY need runtime dispatch.
    // ANY + concrete → use concrete type's batch function.
    // ANY + ANY → use generic dispatch that inspects runtime Value types.
    if (left_type == BTK::ANY || right_type == BTK::ANY) {
        auto concrete = (left_type != BTK::ANY) ? left_type : (right_type != BTK::ANY) ? right_type : BTK::ANY;
        switch (concrete) {
        case BTK::INT64:
            switch (op) {
            case BO::ADD:
                return genericAddBatch;
            case BO::SUB:
                return genericSubBatch;
            case BO::MUL:
                return genericMulBatch;
            case BO::DIV:
                return genericDivBatch;
            case BO::MOD:
                return genericModBatch;
            case BO::POW:
                return genericPowBatch;
            case BO::LT:
                return genericLtBatch;
            case BO::GT:
                return genericGtBatch;
            case BO::LTE:
                return genericLteBatch;
            case BO::GTE:
                return genericGteBatch;
            default:
                return nullptr;
            }
        case BTK::DOUBLE:
            switch (op) {
            case BO::ADD:
                return genericAddBatch;
            case BO::SUB:
                return doubleSubBatch;
            case BO::MUL:
                return genericMulBatch;
            case BO::DIV:
                return genericDivBatch;
            case BO::MOD:
                return doubleModBatch;
            case BO::POW:
                return doublePowBatch;
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
        case BTK::STRING:
            switch (op) {
            case BO::ADD:
                return genericAddBatch;
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
        case BTK::BOOL:
            switch (op) {
            case BO::LT:
                return boolLtBatch;
            case BO::GT:
                return boolGtBatch;
            case BO::LTE:
                return boolLteBatch;
            case BO::GTE:
                return boolGteBatch;
            default:
                return nullptr;
            }
        case BTK::ANY: // both sides are ANY — runtime type dispatch
            switch (op) {
            case BO::ADD:
                return genericAddBatch;
            case BO::SUB:
                return genericSubBatch;
            case BO::MUL:
                return genericMulBatch;
            case BO::DIV:
                return genericDivBatch;
            case BO::MOD:
                return genericModBatch;
            case BO::POW:
                return genericPowBatch;
            case BO::LT:
                return genericLtBatch;
            case BO::GT:
                return genericGtBatch;
            case BO::LTE:
                return genericLteBatch;
            case BO::GTE:
                return genericGteBatch;
            default:
                return nullptr;
            }
        default:
            break;
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

    // List-specific operations (accept ANY for property round-trip)
    // Must be after temporal checks so temporal+ANY ADD is correctly dispatched
    if ((left_type == BTK::LIST || left_type == BTK::ANY) || (right_type == BTK::LIST || right_type == BTK::ANY)) {
        if (op == BO::ADD)
            return listConcatBatch;
        // Ordered comparison for list types (LT/GT/LTE/GTE)
        if ((left_type == BTK::LIST || left_type == BTK::ANY) && (right_type == BTK::LIST || right_type == BTK::ANY)) {
            switch (op) {
            case BO::LT:
                return listLtBatch;
            case BO::GT:
                return listGtBatch;
            case BO::LTE:
                return listLteBatch;
            case BO::GTE:
                return listGteBatch;
            default:
                break;
            }
        }
    }

    // Unhandled type combination: for ordered comparison, return null (incomparable types).
    if (op == BO::LT || op == BO::GT || op == BO::LTE || op == BO::GTE)
        return nullCmpBatch;

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
