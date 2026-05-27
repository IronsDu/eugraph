#pragma once

#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <cmath>
#include <cstring>
#include <ctime>
#include <string>

namespace eugraph {
namespace function {
namespace scalar {

// ==================== Internal helpers ====================

namespace {

void isoWeekToDate(int64_t iso_year, int64_t iso_week, int64_t iso_dow, int64_t& out_year, int64_t& out_month,
                   int64_t& out_day) {
    // ISO week date using pure calendar math (works for all years)
    int64_t jan4 = daysFromCivil(iso_year, 1, 4);
    // ISO weekday: 1970-01-01 was Thursday = ISO day 4
    int64_t jan4_dow = ((jan4 % 7) + 10) % 7 + 1; // 1=Mon..7=Sun
    int64_t week1_monday = jan4 - (jan4_dow - 1);
    int64_t target = week1_monday + (iso_week - 1) * 7 + (iso_dow - 1);
    civilFromDays(target, out_year, out_month, out_day);
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

int64_t dayOfWeekIso(int64_t year, int64_t month, int64_t day) {
    // 1970-01-01 was Thursday = ISO day 4
    int64_t days = daysFromCivil(year, month, day);
    return ((days % 7) + 10) % 7 + 1; // 1=Mon..7=Sun
}

int64_t isoWeekNumber(int64_t year, int64_t month, int64_t day) {
    // ISO week: week containing Jan 4
    int64_t jan4_dow = dayOfWeekIso(year, 1, 4);
    int64_t jan4_mon_offset = jan4_dow - 1; // days from Monday to Jan 4
    // Find Monday of week 1
    int64_t monday_week1_day = 4 - jan4_mon_offset;
    // Days from year start
    int64_t day_of_year = 0;
    for (int64_t m = 1; m < month; m++)
        day_of_year += daysInMonth(year, m);
    day_of_year += day;
    // Week number
    int64_t week = (day_of_year - monday_week1_day) / 7 + 1;
    if (week < 1)
        return isoWeekNumber(year - 1, 12, 31); // belonged to last year
    if (week > 52) {
        // Check if week 53
        int64_t dec31_dow = dayOfWeekIso(year, 12, 31);
        int64_t max_week = (dec31_dow >= 4) ? 53 : 52;
        if (week > max_week)
            return 1; // Jan 1 of next year
    }
    return week;
}

int64_t isoWeekYear(int64_t year, int64_t month, int64_t day) {
    int64_t w = isoWeekNumber(year, month, day);
    if (month == 1 && day <= 7 && w >= 52)
        return year - 1;
    if (month == 12 && day >= 25 && w == 1)
        return year + 1;
    return year;
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

int32_t parseTzOffset(const std::string& tz) {
    if (tz.empty() || tz == "Z" || tz == "+00:00" || tz == "-00:00")
        return 0;
    int sign = (tz[0] == '-') ? -1 : 1;
    size_t start = (tz[0] == '+' || tz[0] == '-') ? 1 : 0;
    // Parse HH:MM[:SS] or HHMM
    int64_t hours = 0, minutes = 0, seconds = 0;
    std::string rest = tz.substr(start);
    size_t col1 = rest.find(':');
    if (col1 != std::string::npos) {
        hours = std::stoll(rest.substr(0, col1));
        size_t col2 = rest.find(':', col1 + 1);
        if (col2 != std::string::npos) {
            minutes = std::stoll(rest.substr(col1 + 1, col2 - col1 - 1));
            seconds = std::stoll(rest.substr(col2 + 1));
        } else {
            minutes = std::stoll(rest.substr(col1 + 1));
        }
    } else {
        size_t len = rest.size();
        if (len >= 2) {
            hours = std::stoll(rest.substr(0, 2));
            if (len >= 4)
                minutes = std::stoll(rest.substr(2, 2));
        }
    }
    return static_cast<int32_t>(sign * (hours * 60 + minutes + seconds / 60));
}

void extractDateFields(const MapValue* mv, int64_t& year, int64_t& month, int64_t& day) {
    year = intFromMap(mv, "year", 1970);
    month = 1;
    day = 1;

    bool use_week = hasMapKey(mv, "week");
    bool use_ordinal = hasMapKey(mv, "ordinalDay");
    bool use_quarter = hasMapKey(mv, "quarter");

    // If 'date' key is present, use its date as the base
    if (hasMapKey(mv, "date")) {
        for (const auto& [k, vs] : mv->entries) {
            if (k == "date" && std::holds_alternative<TemporalValue>(vs.value)) {
                const auto& base = std::get<TemporalValue>(vs.value);
                year = base.year;
                month = base.month;
                day = base.day;
                break;
            }
        }
    }

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
        bool has_year = hasMapKey(mv, "year");
        if (hasMapKey(mv, "month"))
            month = intFromMap(mv, "month", 1);
        if (hasMapKey(mv, "day"))
            day = intFromMap(mv, "day", 1);
        if (has_year || hasMapKey(mv, "month") || hasMapKey(mv, "day"))
            normalizeDate(year, month, day);
    }
}

// Fractional value extraction for duration
double doubleFromMap(const MapValue* mv, const std::string& key, double def = 0.0) {
    if (!mv)
        return def;
    for (const auto& [k, vs] : mv->entries) {
        if (k != key || std::holds_alternative<std::monostate>(vs.value))
            continue;
        if (std::holds_alternative<double>(vs.value))
            return std::get<double>(vs.value);
        if (std::holds_alternative<int64_t>(vs.value))
            return static_cast<double>(std::get<int64_t>(vs.value));
    }
    return def;
}

// String parsing for temporal values
TemporalValue parseDateFromString(const std::string& s) {
    TemporalValue tv{TemporalKind::DATE};
    if (s.empty())
        return tv;
    if (s.size() >= 10 && s[4] == '-' && s[7] == '-') {
        tv.year = std::stoll(s.substr(0, 4));
        tv.month = std::stoll(s.substr(5, 2));
        tv.day = std::stoll(s.substr(8));
    } else if (s.size() == 8 && s.find('-') == std::string::npos && s.find('W') == std::string::npos) {
        tv.year = std::stoll(s.substr(0, 4));
        tv.month = std::stoll(s.substr(4, 2));
        tv.day = std::stoll(s.substr(6, 2));
    } else if (s.size() >= 7 && s[4] == '-' && s.rfind('-') == 4) {
        tv.year = std::stoll(s.substr(0, 4));
        tv.month = std::stoll(s.substr(5));
        tv.day = 1;
    } else if (s.size() == 6 && s.find('-') == std::string::npos) {
        tv.year = std::stoll(s.substr(0, 4));
        tv.month = std::stoll(s.substr(4, 2));
        tv.day = 1;
    } else if (s.find('W') != std::string::npos || s.find('w') != std::string::npos) {
        size_t wpos = s.find('W');
        if (wpos == std::string::npos)
            wpos = s.find('w');
        int64_t iso_year = std::stoll(s.substr(0, wpos));
        size_t num_start = wpos + 1;
        if (num_start < s.size() && s[num_start] == '-')
            num_start++;
        size_t num_end = num_start;
        while (num_end < s.size() && s[num_end] >= '0' && s[num_end] <= '9')
            num_end++;
        int64_t iso_week = std::stoll(s.substr(num_start, num_end - num_start));
        int64_t iso_dow = 1;
        if (num_end < s.size() && (s[num_end] == '-' || (s[num_end] >= '0' && s[num_end] <= '9'))) {
            if (s[num_end] == '-')
                num_end++;
            iso_dow = std::stoll(s.substr(num_end));
        }
        isoWeekToDate(iso_year, iso_week, iso_dow, tv.year, tv.month, tv.day);
    }
    return tv;
}

TemporalValue parseTimeStr(const std::string& s, TemporalKind kind) {
    TemporalValue tv{kind};
    if (s.empty())
        return tv;
    size_t pos = 0;
    tv.hour = std::stoll(s.substr(pos, 2));
    pos += 2;
    if (pos < s.size() && s[pos] == ':')
        pos++;
    tv.minute = std::stoll(s.substr(pos, 2));
    pos += 2;
    if (pos < s.size() && s[pos] == ':') {
        pos++;
        tv.second = std::stoll(s.substr(pos, 2));
        pos += 2;
    }
    if (pos < s.size() && s[pos] == '.') {
        pos++;
        size_t start = pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
            pos++;
        std::string frac = s.substr(start, pos - start);
        frac.resize(9, '0');
        tv.nanos = std::stoll(frac);
    }
    if (kind == TemporalKind::TIME && pos < s.size()) {
        std::string tz = s.substr(pos);
        if (tz != "Z" && !tz.empty())
            tv.tz_offset_min = parseTzOffset(tz);
    }
    return tv;
}

TemporalValue parseDatetimeStr(const std::string& s, TemporalKind kind) {
    TemporalValue tv{kind};
    if (s.empty())
        return tv;
    size_t pos = 0;
    tv.year = std::stoll(s.substr(0, 4));
    pos += 4;
    if (pos < s.size() && s[pos] == '-')
        pos++;
    tv.month = std::stoll(s.substr(pos, 2));
    pos += 2;
    if (pos < s.size() && s[pos] == '-')
        pos++;
    tv.day = std::stoll(s.substr(pos, 2));
    pos += 2;
    if (pos < s.size() && (s[pos] == 'T' || s[pos] == 't' || s[pos] == ' '))
        pos++;
    tv.hour = std::stoll(s.substr(pos, 2));
    pos += 2;
    if (pos < s.size() && s[pos] == ':')
        pos++;
    tv.minute = std::stoll(s.substr(pos, 2));
    pos += 2;
    if (pos < s.size() && s[pos] == ':') {
        pos++;
        tv.second = std::stoll(s.substr(pos, 2));
        pos += 2;
    }
    if (pos < s.size() && s[pos] == '.') {
        pos++;
        size_t start = pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
            pos++;
        std::string frac = s.substr(start, pos - start);
        frac.resize(9, '0');
        tv.nanos = std::stoll(frac);
    }
    if (kind == TemporalKind::DATETIME && pos < s.size()) {
        std::string tz = s.substr(pos);
        if (tz != "Z" && !tz.empty())
            tv.tz_offset_min = parseTzOffset(tz);
    }
    return tv;
}

TemporalValue parseDurationFromString(const std::string& s) {
    TemporalValue tv{TemporalKind::DURATION};
    if (s.empty() || s[0] != 'P')
        return tv;
    size_t pos = 1;
    bool in_time = false;
    while (pos < s.size()) {
        if (s[pos] == 'T') {
            in_time = true;
            pos++;
            continue;
        }
        size_t num_start = pos;
        bool neg = (s[pos] == '-');
        if (neg)
            pos++;
        while (pos < s.size() && ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.'))
            pos++;
        double val = std::stod(s.substr(num_start, pos - num_start));
        if (pos >= s.size())
            break;
        char unit = s[pos++];
        int64_t int_part = static_cast<int64_t>(val);
        double frac = val - int_part;
        switch (unit) {
        case 'Y':
            tv.dur_months += int_part * 12;
            break;
        case 'M':
            if (!in_time) {
                tv.dur_months += int_part;
            } else {
                tv.dur_seconds += int_part * 60;
                if (frac != 0)
                    tv.dur_seconds += static_cast<int64_t>(frac * 60);
            }
            break;
        case 'W':
            tv.dur_days += int_part * 7;
            break;
        case 'D':
            tv.dur_days += int_part;
            if (frac != 0)
                tv.dur_seconds += static_cast<int64_t>(frac * 86400);
            break;
        case 'H':
            tv.dur_seconds += int_part * 3600;
            if (frac != 0)
                tv.dur_seconds += static_cast<int64_t>(frac * 3600);
            break;
        case 'S': {
            tv.dur_seconds += int_part;
            if (frac != 0) {
                int64_t frac_ns = static_cast<int64_t>(frac * 1'000'000'000.0);
                tv.dur_nanos += frac_ns;
                if (tv.dur_nanos >= 1'000'000'000LL) {
                    tv.dur_seconds += tv.dur_nanos / 1'000'000'000LL;
                    tv.dur_nanos %= 1'000'000'000LL;
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return tv;
}

} // anonymous namespace

// ==================== date ====================

inline Value dateImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};

    if (auto* mv = asMap(arg)) {
        TemporalValue tv;
        tv.kind = TemporalKind::DATE;
        extractDateFields(mv, tv.year, tv.month, tv.day);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseDateFromString(std::get<std::string>(arg))};

    return Value{};
}

inline Value dateScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{TemporalValue{TemporalKind::DATE}};
    return dateImpl(args[0]);
}

inline void dateBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{TemporalValue{TemporalKind::DATE}});
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
        TemporalValue tv;
        tv.kind = TemporalKind::LOCAL_TIME;
        tv.hour = intFromMap(mv, "hour");
        tv.minute = intFromMap(mv, "minute");
        tv.second = intFromMap(mv, "second");
        tv.nanos = extractNanosFromMap(mv);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseTimeStr(std::get<std::string>(arg), TemporalKind::LOCAL_TIME)};

    return Value{};
}

inline Value localtimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{TemporalValue{TemporalKind::LOCAL_TIME}};
    return localtimeImpl(args[0]);
}

inline void localtimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{TemporalValue{TemporalKind::LOCAL_TIME}});
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
        TemporalValue tv;
        tv.kind = TemporalKind::TIME;
        tv.hour = intFromMap(mv, "hour");
        tv.minute = intFromMap(mv, "minute");
        tv.second = intFromMap(mv, "second");
        tv.nanos = extractNanosFromMap(mv);
        std::string tz = strFromMap(mv, "timezone");
        tv.tz_offset_min = parseTzOffset(tz);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseTimeStr(std::get<std::string>(arg), TemporalKind::TIME)};

    return Value{};
}

inline Value timeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{TemporalValue{TemporalKind::TIME}};
    return timeImpl(args[0]);
}

inline void timeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{TemporalValue{TemporalKind::TIME}});
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
        TemporalValue tv;
        tv.kind = TemporalKind::LOCAL_DATETIME;
        extractDateFields(mv, tv.year, tv.month, tv.day);
        tv.hour = intFromMap(mv, "hour");
        tv.minute = intFromMap(mv, "minute");
        tv.second = intFromMap(mv, "second");
        tv.nanos = extractNanosFromMap(mv);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseDatetimeStr(std::get<std::string>(arg), TemporalKind::LOCAL_DATETIME)};

    return Value{};
}

inline Value localdatetimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{TemporalValue{TemporalKind::LOCAL_DATETIME}};
    return localdatetimeImpl(args[0]);
}

inline void localdatetimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                 const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{TemporalValue{TemporalKind::LOCAL_DATETIME}});
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
        TemporalValue tv;
        tv.kind = TemporalKind::DATETIME;
        extractDateFields(mv, tv.year, tv.month, tv.day);
        tv.hour = intFromMap(mv, "hour");
        tv.minute = intFromMap(mv, "minute");
        tv.second = intFromMap(mv, "second");
        tv.nanos = extractNanosFromMap(mv);
        std::string tz = strFromMap(mv, "timezone");
        tv.tz_offset_min = parseTzOffset(tz);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseDatetimeStr(std::get<std::string>(arg), TemporalKind::DATETIME)};

    return Value{};
}

inline Value datetimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{TemporalValue{TemporalKind::DATETIME}};
    return datetimeImpl(args[0]);
}

inline void datetimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{TemporalValue{TemporalKind::DATETIME}});
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
        TemporalValue tv;
        tv.kind = TemporalKind::DURATION;

        double years = doubleFromMap(mv, "years");
        double months = doubleFromMap(mv, "months");
        double weeks = doubleFromMap(mv, "weeks");
        double days = doubleFromMap(mv, "days");
        double hours = doubleFromMap(mv, "hours");
        double minutes = doubleFromMap(mv, "minutes");
        double seconds = doubleFromMap(mv, "seconds");

        double total_months = years * 12.0 + months;
        double month_frac = total_months - std::floor(total_months);
        tv.dur_months = static_cast<int64_t>(std::floor(total_months));
        if (month_frac != 0.0)
            days += month_frac * (365.2425 / 12.0);

        double week_frac = weeks - std::floor(weeks);
        tv.dur_days = static_cast<int64_t>(std::floor(weeks)) * 7;
        if (week_frac != 0.0)
            days += week_frac * 7.0;

        double day_frac = days - std::floor(days);
        tv.dur_days += static_cast<int64_t>(std::floor(days));
        if (day_frac != 0.0)
            hours += day_frac * 24.0;

        double hour_frac = hours - std::floor(hours);
        tv.dur_seconds = static_cast<int64_t>(std::floor(hours)) * 3600;
        if (hour_frac != 0.0)
            minutes += hour_frac * 60.0;

        double min_frac = minutes - std::floor(minutes);
        tv.dur_seconds += static_cast<int64_t>(std::floor(minutes)) * 60;
        if (min_frac != 0.0)
            seconds += min_frac * 60.0;

        double sec_frac = seconds - std::floor(seconds);
        tv.dur_seconds += static_cast<int64_t>(std::floor(seconds));
        tv.dur_nanos = static_cast<int64_t>(sec_frac * 1'000'000'000.0);

        tv.dur_nanos += extractNanosFromMap(mv);

        if (tv.dur_nanos >= 1'000'000'000LL || tv.dur_nanos <= -1'000'000'000LL) {
            tv.dur_seconds += tv.dur_nanos / 1'000'000'000LL;
            tv.dur_nanos %= 1'000'000'000LL;
        }
        if (tv.dur_nanos < 0) {
            tv.dur_nanos += 1'000'000'000LL;
            tv.dur_seconds -= 1;
        }

        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseDurationFromString(std::get<std::string>(arg))};

    return Value{};
}

inline Value durationScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{TemporalValue{TemporalKind::DURATION}};
    return durationImpl(args[0]);
}

inline void durationBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{TemporalValue{TemporalKind::DURATION}});
        return;
    }
    const auto& col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, durationImpl(col.getValue(i)));
}

// ==================== temporal accessor ====================

namespace {

int64_t ordinalDayOf(int64_t year, int64_t month, int64_t day) {
    int64_t ord = 0;
    for (int64_t m = 1; m < month; m++)
        ord += daysInMonth(year, m);
    ord += day;
    return ord;
}

int64_t dayOfQuarter(int64_t year, int64_t month, int64_t day) {
    int64_t quarter_start_month = ((month - 1) / 3) * 3 + 1;
    int64_t ord = 0;
    for (int64_t m = quarter_start_month; m < month; m++)
        ord += daysInMonth(year, m);
    ord += day;
    return ord;
}

} // namespace

inline Value temporalAccessorImpl(const Value& tv_val, TemporalField field) {
    if (!std::holds_alternative<TemporalValue>(tv_val))
        return Value{};
    const auto& tv = std::get<TemporalValue>(tv_val);

    auto isDateLike = [](TemporalKind k) {
        return k == TemporalKind::DATE || k == TemporalKind::LOCAL_DATETIME || k == TemporalKind::DATETIME;
    };
    auto isTimeLike = [](TemporalKind k) {
        return k == TemporalKind::LOCAL_TIME || k == TemporalKind::TIME || k == TemporalKind::LOCAL_DATETIME ||
               k == TemporalKind::DATETIME;
    };
    auto isZoned = [](TemporalKind k) { return k == TemporalKind::TIME || k == TemporalKind::DATETIME; };

    const TemporalKind k = tv.kind;

    switch (field) {
    // Date accessors
    case TemporalField::YEAR:
        if (!isDateLike(k))
            return Value{};
        return Value{tv.year};
    case TemporalField::QUARTER:
        if (!isDateLike(k))
            return Value{};
        return Value{int64_t((tv.month - 1) / 3 + 1)};
    case TemporalField::MONTH:
        if (!isDateLike(k))
            return Value{};
        return Value{tv.month};
    case TemporalField::DAY:
        if (!isDateLike(k))
            return Value{};
        return Value{tv.day};
    case TemporalField::WEEK_YEAR:
        if (!isDateLike(k))
            return Value{};
        return Value{isoWeekYear(tv.year, tv.month, tv.day)};
    case TemporalField::WEEK:
        if (!isDateLike(k))
            return Value{};
        return Value{isoWeekNumber(tv.year, tv.month, tv.day)};
    case TemporalField::ORDINAL_DAY:
        if (!isDateLike(k))
            return Value{};
        return Value{ordinalDayOf(tv.year, tv.month, tv.day)};
    case TemporalField::WEEK_DAY:
        if (!isDateLike(k))
            return Value{};
        return Value{dayOfWeekIso(tv.year, tv.month, tv.day)};
    case TemporalField::DAY_OF_QUARTER:
        if (!isDateLike(k))
            return Value{};
        return Value{dayOfQuarter(tv.year, tv.month, tv.day)};

    // Time accessors
    case TemporalField::HOUR:
        if (!isTimeLike(k))
            return Value{};
        return Value{tv.hour};
    case TemporalField::MINUTE:
        if (!isTimeLike(k))
            return Value{};
        return Value{tv.minute};
    case TemporalField::SECOND:
        if (!isTimeLike(k))
            return Value{};
        return Value{tv.second};
    case TemporalField::NANOSECOND:
        if (!isTimeLike(k))
            return Value{};
        return Value{tv.nanos};
    case TemporalField::MILLISECOND:
        if (!isTimeLike(k))
            return Value{};
        return Value{tv.nanos / 1'000'000LL};
    case TemporalField::MICROSECOND:
        if (!isTimeLike(k))
            return Value{};
        return Value{tv.nanos / 1'000LL};

    // Timezone accessors
    case TemporalField::TIMEZONE:
        if (!isZoned(k))
            return Value{};
        if (!tv.tz_name.empty())
            return Value{tv.tz_name};
        if (tv.tz_offset_min == 0)
            return Value{std::string("Z")};
        {
            int32_t abs_m = tv.tz_offset_min < 0 ? -tv.tz_offset_min : tv.tz_offset_min;
            char buf[14];
            snprintf(buf, sizeof(buf), "%c%02d:%02d", tv.tz_offset_min >= 0 ? '+' : '-', static_cast<int>(abs_m / 60),
                     static_cast<int>(abs_m % 60));
            return Value{std::string(buf)};
        }
    case TemporalField::OFFSET:
        if (!isZoned(k))
            return Value{};
        if (tv.tz_offset_min == 0)
            return Value{std::string("+00:00")};
        {
            int32_t abs_m = tv.tz_offset_min < 0 ? -tv.tz_offset_min : tv.tz_offset_min;
            char buf[14];
            snprintf(buf, sizeof(buf), "%c%02d:%02d", tv.tz_offset_min >= 0 ? '+' : '-', static_cast<int>(abs_m / 60),
                     static_cast<int>(abs_m % 60));
            return Value{std::string(buf)};
        }
    case TemporalField::OFFSET_MINUTES:
        if (!isZoned(k))
            return Value{};
        return Value{int64_t(tv.tz_offset_min)};
    case TemporalField::OFFSET_SECONDS:
        if (!isZoned(k))
            return Value{};
        return Value{int64_t(tv.tz_offset_min) * 60};

    // Epoch accessors
    case TemporalField::EPOCH_SECONDS:
        if (k != TemporalKind::DATETIME)
            return Value{};
        return Value{temporalToComparable(tv) / 1'000'000'000LL};
    case TemporalField::EPOCH_MILLIS:
        if (k != TemporalKind::DATETIME)
            return Value{};
        return Value{temporalToComparable(tv) / 1'000'000LL};

    // Duration accessors
    case TemporalField::YEARS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_months / 12};
    case TemporalField::QUARTERS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_months / 3};
    case TemporalField::MONTHS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_months};
    case TemporalField::MONTHS_OF_YEAR:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_months % 12};
    case TemporalField::MONTHS_OF_QUARTER:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{(tv.dur_months % 12) % 3};
    case TemporalField::QUARTERS_OF_YEAR:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{(tv.dur_months % 12) / 3};
    case TemporalField::WEEKS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_days / 7};
    case TemporalField::DAYS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_days};
    case TemporalField::DAYS_OF_WEEK:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_days % 7};
    case TemporalField::HOURS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_seconds / 3600};
    case TemporalField::MINUTES:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_seconds / 60};
    case TemporalField::SECONDS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_seconds};
    case TemporalField::MILLISECONDS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_nanos / 1'000'000LL};
    case TemporalField::MICROSECONDS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_nanos / 1'000LL};
    case TemporalField::NANOSECONDS:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_nanos};
    case TemporalField::MINUTES_OF_HOUR:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{(tv.dur_seconds / 60) % 60};
    case TemporalField::SECONDS_OF_MINUTE:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_seconds % 60};
    case TemporalField::MILLISECONDS_OF_SECOND:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{(tv.dur_nanos / 1'000'000LL) % 1000};
    case TemporalField::MICROSECONDS_OF_SECOND:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{(tv.dur_nanos / 1'000LL) % 1000000};
    case TemporalField::NANOSECONDS_OF_SECOND:
        if (k != TemporalKind::DURATION)
            return Value{};
        return Value{tv.dur_nanos % 1'000'000'000LL};
    }

    return Value{};
}

inline Value temporalAccessorScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.size() < 2)
        return Value{};
    if (!std::holds_alternative<int64_t>(args[1]))
        return Value{};
    auto field = static_cast<TemporalField>(std::get<int64_t>(args[1]));
    return temporalAccessorImpl(args[0], field);
}

inline void temporalAccessorBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                    const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& tv_col = *args[0];
    const auto& field_col = *args[1];
    Value field_val = field_col.getValue(0);
    if (!std::holds_alternative<int64_t>(field_val))
        return;
    auto field = static_cast<TemporalField>(std::get<int64_t>(field_val));
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, temporalAccessorImpl(tv_col.getValue(i), field));
}

} // namespace scalar
} // namespace function
} // namespace eugraph
