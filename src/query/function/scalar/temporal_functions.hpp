#pragma once

#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <charconv>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <string_view>

namespace eugraph {
namespace function {
namespace scalar {

// ==================== Internal helpers ====================

namespace {

bool isLeapYear(int64_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int64_t daysInMonth(int64_t year, int64_t month) {
    static const int64_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && isLeapYear(year))
        return 29;
    return kDays[month - 1];
}

// ISO week date helpers
void isoWeekToDate(int64_t iso_year, int64_t iso_week, int64_t iso_dow, int64_t& out_year, int64_t& out_month,
                   int64_t& out_day) {
    // ISO week 1 contains January 4
    // Find the Monday of ISO week 1
    // Jan 4 of iso_year
    struct tm tm_base = {};
    tm_base.tm_year = static_cast<int>(iso_year - 1900);
    tm_base.tm_mon = 0;
    tm_base.tm_mday = 4;
    tm_base.tm_isdst = -1;
    time_t jan4 = timegm(&tm_base);
    int jan4_wday = gmtime(&jan4)->tm_wday; // 0=Sun
    int jan4_mon = (jan4_wday + 6) % 7;     // 0=Mon

    // Days from Jan 4 Monday of week 1 to target: (iso_week-1)*7 + (iso_dow-1) - jan4_mon
    int64_t days = (iso_week - 1) * 7 + (iso_dow - 1) - jan4_mon;
    time_t target = jan4 + static_cast<time_t>(days) * 86400;
    struct tm* out = gmtime(&target);
    out_year = out->tm_year + 1900;
    out_month = out->tm_mon + 1;
    out_day = out->tm_mday;
}

void ordinalToDate(int64_t year, int64_t ordinal, int64_t& out_month, int64_t& out_day) {
    int64_t m = 1;
    while (m <= 12 && ordinal > daysInMonth(year, m)) {
        ordinal -= daysInMonth(year, m);
        m++;
    }
    out_month = m;
    out_day = ordinal;
}

void quarterToDate(int64_t year, int64_t quarter, int64_t day_of_quarter, int64_t& out_month, int64_t& out_day) {
    int64_t start_month = (quarter - 1) * 3 + 1;
    int64_t remaining = day_of_quarter;
    int64_t m = start_month;
    while (m <= 12 && remaining > daysInMonth(year, m)) {
        remaining -= daysInMonth(year, m);
        m++;
    }
    out_month = m;
    out_day = remaining;
}

void normalizeDate(int64_t& year, int64_t& month, int64_t& day) {
    while (month > 12) {
        month -= 12;
        year++;
    }
    while (month < 1) {
        month += 12;
        year--;
    }
    while (day > daysInMonth(year, month)) {
        day -= daysInMonth(year, month);
        month++;
        if (month > 12) {
            month -= 12;
            year++;
        }
    }
    while (day < 1) {
        month--;
        if (month < 1) {
            month = 12;
            year--;
        }
        day += daysInMonth(year, month);
    }
}

// Map value extraction
const MapValue* asMap(const Value& v) {
    if (std::holds_alternative<MapValue>(v))
        return &std::get<MapValue>(v);
    return nullptr;
}

bool hasMapKey(const MapValue* mv, const std::string& key) {
    if (!mv)
        return false;
    for (const auto& [k, vs] : mv->entries) {
        if (k == key && !std::holds_alternative<std::monostate>(vs.value))
            return true;
    }
    return false;
}

int64_t intFromMap(const MapValue* mv, const std::string& key, int64_t def = 0) {
    if (!mv)
        return def;
    for (const auto& [k, vs] : mv->entries) {
        if (k != key || std::holds_alternative<std::monostate>(vs.value))
            continue;
        if (std::holds_alternative<int64_t>(vs.value))
            return std::get<int64_t>(vs.value);
        if (std::holds_alternative<double>(vs.value))
            return static_cast<int64_t>(std::get<double>(vs.value));
    }
    return def;
}

std::string strFromMap(const MapValue* mv, const std::string& key) {
    if (!mv)
        return {};
    for (const auto& [k, vs] : mv->entries) {
        if (k == key && std::holds_alternative<std::string>(vs.value))
            return std::get<std::string>(vs.value);
    }
    return {};
}

int64_t extractNanosFromMap(const MapValue* mv) {
    bool has_ms = hasMapKey(mv, "millisecond");
    bool has_us = hasMapKey(mv, "microsecond");
    bool has_ns = hasMapKey(mv, "nanosecond");
    if (has_ns && !has_ms && !has_us)
        return intFromMap(mv, "nanosecond");
    int64_t total = 0;
    total += intFromMap(mv, "millisecond") * 1'000'000LL;
    total += intFromMap(mv, "microsecond") * 1'000LL;
    total += intFromMap(mv, "nanosecond");
    return total;
}

// Formatting helpers
std::string pad4(int64_t v) {
    char buf[5];
    buf[0] = static_cast<char>('0' + ((v / 1000) % 10));
    buf[1] = static_cast<char>('0' + ((v / 100) % 10));
    buf[2] = static_cast<char>('0' + ((v / 10) % 10));
    buf[3] = static_cast<char>('0' + (v % 10));
    buf[4] = '\0';
    return buf;
}

std::string pad2(int64_t v) {
    char buf[3];
    buf[0] = static_cast<char>('0' + ((v / 10) % 10));
    buf[1] = static_cast<char>('0' + (v % 10));
    buf[2] = '\0';
    return buf;
}

std::string formatSubsecond(int64_t nanos) {
    if (nanos == 0)
        return "";
    // Trim trailing zeros
    while (nanos > 0 && nanos % 10 == 0)
        nanos /= 10;
    // Count remaining digits, pad to correct length
    int64_t digits = 0, tmp = nanos;
    while (tmp > 0) {
        digits++;
        tmp /= 10;
    }
    return "." + std::to_string(nanos);
}

std::string formatTimezone(const std::string& tz) {
    if (tz.empty() || tz == "Z" || tz == "+00:00" || tz == "-00:00")
        return "Z";
    // Normalize: if +HHMM format, convert to +HH:MM
    if (tz.size() == 5 && (tz[0] == '+' || tz[0] == '-') && tz[3] != ':') {
        return tz.substr(0, 3) + ":" + tz.substr(3);
    }
    return tz;
}

std::string fmtDate(int64_t year, int64_t month, int64_t day) {
    return pad4(year) + "-" + pad2(month) + "-" + pad2(day);
}

std::string fmtTime(int64_t hour, int64_t minute, int64_t second, int64_t nanos, const std::string& tz) {
    std::string result = pad2(hour) + ":" + pad2(minute) + ":" + pad2(second);
    result += formatSubsecond(nanos);
    if (!tz.empty())
        result += formatTimezone(tz);
    return result;
}

std::string fmtDateTime(int64_t year, int64_t month, int64_t day, int64_t hour, int64_t minute, int64_t second,
                        int64_t nanos, const std::string& tz) {
    return fmtDate(year, month, day) + "T" + fmtTime(hour, minute, second, nanos, tz);
}

} // anonymous namespace

// ==================== date ====================

inline Value dateImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};

    if (auto* mv = asMap(arg)) {
        int64_t year = intFromMap(mv, "year", 1970);
        int64_t month = 1, day = 1;

        // Handle base date from keys
        std::string base_date = strFromMap(mv, "date");
        if (base_date.empty())
            base_date = strFromMap(mv, "datetime");
        if (base_date.empty())
            base_date = strFromMap(mv, "localdatetime");
        // Don't use localtime/time as date base

        bool use_week = hasMapKey(mv, "week");
        bool use_ordinal = hasMapKey(mv, "ordinalDay");
        bool use_quarter = hasMapKey(mv, "quarter");

        if (use_week) {
            int64_t wk = intFromMap(mv, "week", 1);
            int64_t dow = intFromMap(mv, "dayOfWeek", 1);
            isoWeekToDate(year, wk, dow, year, month, day);
        } else if (use_ordinal) {
            int64_t ord = intFromMap(mv, "ordinalDay", 1);
            ordinalToDate(year, ord, month, day);
        } else if (use_quarter) {
            int64_t q = intFromMap(mv, "quarter", 1);
            int64_t doq = intFromMap(mv, "dayOfQuarter", 1);
            quarterToDate(year, q, doq, month, day);
        } else {
            if (hasMapKey(mv, "month"))
                month = intFromMap(mv, "month", 1);
            if (hasMapKey(mv, "day"))
                day = intFromMap(mv, "day", 1);
            normalizeDate(year, month, day);
        }
        return Value{fmtDate(year, month, day)};
    }

    if (std::holds_alternative<std::string>(arg)) {
        // Pass-through string (validated ISO 8601); basic normalization
        const auto& s = std::get<std::string>(arg);
        // If already canonical, return as-is
        return Value{s};
    }

    return Value{};
}

inline Value dateScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{std::string("1970-01-01")};
    return dateImpl(args[0]);
}

inline void dateBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{std::string("1970-01-01")});
        return;
    }
    const auto& col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, dateImpl(col.getValue(i)));
}

// ==================== localtime ====================

inline Value localtimeImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};

    if (auto* mv = asMap(arg)) {
        int64_t hour = intFromMap(mv, "hour");
        int64_t minute = intFromMap(mv, "minute");
        int64_t second = intFromMap(mv, "second");
        int64_t nanos = extractNanosFromMap(mv);
        return Value{fmtTime(hour, minute, second, nanos, "")};
    }

    if (std::holds_alternative<std::string>(arg))
        return arg; // pass-through string

    return Value{};
}

inline Value localtimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{std::string("00:00:00")};
    return localtimeImpl(args[0]);
}

inline void localtimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{std::string("00:00:00")});
        return;
    }
    const auto& col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, localtimeImpl(col.getValue(i)));
}

// ==================== time ====================

inline Value timeImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};

    if (auto* mv = asMap(arg)) {
        int64_t hour = intFromMap(mv, "hour");
        int64_t minute = intFromMap(mv, "minute");
        int64_t second = intFromMap(mv, "second");
        int64_t nanos = extractNanosFromMap(mv);
        std::string tz = strFromMap(mv, "timezone");
        if (tz.empty())
            tz = "Z";
        return Value{fmtTime(hour, minute, second, nanos, tz)};
    }

    if (std::holds_alternative<std::string>(arg))
        return arg;

    return Value{};
}

inline Value timeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{std::string("00:00:00Z")};
    return timeImpl(args[0]);
}

inline void timeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{std::string("00:00:00Z")});
        return;
    }
    const auto& col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, timeImpl(col.getValue(i)));
}

// ==================== localdatetime ====================

inline Value localdatetimeImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};

    if (auto* mv = asMap(arg)) {
        int64_t year = intFromMap(mv, "year", 1970);
        int64_t month = 1, day = 1;
        int64_t hour = intFromMap(mv, "hour");
        int64_t minute = intFromMap(mv, "minute");
        int64_t second = intFromMap(mv, "second");
        int64_t nanos = extractNanosFromMap(mv);

        bool use_week = hasMapKey(mv, "week");
        bool use_ordinal = hasMapKey(mv, "ordinalDay");
        bool use_quarter = hasMapKey(mv, "quarter");

        if (use_week) {
            int64_t wk = intFromMap(mv, "week", 1);
            int64_t dow = intFromMap(mv, "dayOfWeek", 1);
            isoWeekToDate(year, wk, dow, year, month, day);
        } else if (use_ordinal) {
            int64_t ord = intFromMap(mv, "ordinalDay", 1);
            ordinalToDate(year, ord, month, day);
        } else if (use_quarter) {
            int64_t q = intFromMap(mv, "quarter", 1);
            int64_t doq = intFromMap(mv, "dayOfQuarter", 1);
            quarterToDate(year, q, doq, month, day);
        } else {
            if (hasMapKey(mv, "month"))
                month = intFromMap(mv, "month", 1);
            if (hasMapKey(mv, "day"))
                day = intFromMap(mv, "day", 1);
            normalizeDate(year, month, day);
        }
        return Value{fmtDateTime(year, month, day, hour, minute, second, nanos, "")};
    }

    if (std::holds_alternative<std::string>(arg))
        return arg;

    return Value{};
}

inline Value localdatetimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{std::string("1970-01-01T00:00:00")};
    return localdatetimeImpl(args[0]);
}

inline void localdatetimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                 const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{std::string("1970-01-01T00:00:00")});
        return;
    }
    const auto& col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, localdatetimeImpl(col.getValue(i)));
}

// ==================== datetime ====================

inline Value datetimeImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};

    if (auto* mv = asMap(arg)) {
        int64_t year = intFromMap(mv, "year", 1970);
        int64_t month = 1, day = 1;
        int64_t hour = intFromMap(mv, "hour");
        int64_t minute = intFromMap(mv, "minute");
        int64_t second = intFromMap(mv, "second");
        int64_t nanos = extractNanosFromMap(mv);
        std::string tz = strFromMap(mv, "timezone");
        if (tz.empty())
            tz = "Z";

        bool use_week = hasMapKey(mv, "week");
        bool use_ordinal = hasMapKey(mv, "ordinalDay");
        bool use_quarter = hasMapKey(mv, "quarter");

        if (use_week) {
            int64_t wk = intFromMap(mv, "week", 1);
            int64_t dow = intFromMap(mv, "dayOfWeek", 1);
            isoWeekToDate(year, wk, dow, year, month, day);
        } else if (use_ordinal) {
            int64_t ord = intFromMap(mv, "ordinalDay", 1);
            ordinalToDate(year, ord, month, day);
        } else if (use_quarter) {
            int64_t q = intFromMap(mv, "quarter", 1);
            int64_t doq = intFromMap(mv, "dayOfQuarter", 1);
            quarterToDate(year, q, doq, month, day);
        } else {
            if (hasMapKey(mv, "month"))
                month = intFromMap(mv, "month", 1);
            if (hasMapKey(mv, "day"))
                day = intFromMap(mv, "day", 1);
            normalizeDate(year, month, day);
        }
        return Value{fmtDateTime(year, month, day, hour, minute, second, nanos, tz)};
    }

    if (std::holds_alternative<std::string>(arg))
        return arg;

    return Value{};
}

inline Value datetimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{std::string("1970-01-01T00:00:00Z")};
    return datetimeImpl(args[0]);
}

inline void datetimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{std::string("1970-01-01T00:00:00Z")});
        return;
    }
    const auto& col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, datetimeImpl(col.getValue(i)));
}

// ==================== duration ====================

inline Value durationImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};

    if (auto* mv = asMap(arg)) {
        int64_t years = intFromMap(mv, "years");
        int64_t months = intFromMap(mv, "months");
        int64_t weeks = intFromMap(mv, "weeks");
        int64_t days = intFromMap(mv, "days");
        int64_t hours = intFromMap(mv, "hours");
        int64_t minutes = intFromMap(mv, "minutes");
        int64_t seconds = intFromMap(mv, "seconds");

        std::string result = "P";
        bool has_t = false;
        if (years != 0) {
            result += std::to_string(years) + "Y";
        }
        if (months != 0) {
            result += std::to_string(months) + "M";
        }
        if (weeks != 0) {
            result += std::to_string(weeks) + "W";
        }
        if (days != 0) {
            result += std::to_string(days) + "D";
        }
        if (hours != 0) {
            if (!has_t) {
                result += "T";
                has_t = true;
            }
            result += std::to_string(hours) + "H";
        }
        if (minutes != 0) {
            if (!has_t) {
                result += "T";
                has_t = true;
            }
            result += std::to_string(minutes) + "M";
        }
        if (seconds != 0) {
            if (!has_t) {
                result += "T";
                has_t = true;
            }
            result += std::to_string(seconds) + "S";
        }
        if (result == "P")
            result += "T0S";
        return Value{result};
    }

    if (std::holds_alternative<std::string>(arg))
        return arg;

    return Value{};
}

inline Value durationScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{std::string("PT0S")};
    return durationImpl(args[0]);
}

inline void durationBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{std::string("PT0S")});
        return;
    }
    const auto& col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, durationImpl(col.getValue(i)));
}

} // namespace scalar
} // namespace function
} // namespace eugraph
