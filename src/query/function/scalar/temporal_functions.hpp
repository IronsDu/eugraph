#pragma once

#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <cstring>
#include <ctime>
#include <string>

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

void isoWeekToDate(int64_t iso_year, int64_t iso_week, int64_t iso_dow, int64_t& out_year, int64_t& out_month,
                   int64_t& out_day) {
    struct tm tm_base = {};
    tm_base.tm_year = static_cast<int>(iso_year - 1900);
    tm_base.tm_mon = 0;
    tm_base.tm_mday = 4;
    tm_base.tm_isdst = -1;
    time_t jan4 = timegm(&tm_base);
    int jan4_wday = gmtime(&jan4)->tm_wday;
    int jan4_mon = (jan4_wday + 6) % 7;
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

int64_t dayOfWeekIso(int64_t year, int64_t month, int64_t day) {
    // Returns 1=Monday to 7=Sunday using Zeller-like algorithm
    struct tm tm_val = {};
    tm_val.tm_year = static_cast<int>(year - 1900);
    tm_val.tm_mon = static_cast<int>(month - 1);
    tm_val.tm_mday = static_cast<int>(day);
    tm_val.tm_isdst = -1;
    time_t t = timegm(&tm_val);
    int wday = gmtime(&t)->tm_wday; // 0=Sun
    return (wday + 6) % 7 + 1;      // 1=Mon
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
    // Parse HH:MM or HHMM
    int64_t hours = 0, minutes = 0;
    size_t colon = tz.find(':', start);
    if (colon != std::string::npos) {
        hours = std::stoll(tz.substr(start, colon - start));
        minutes = std::stoll(tz.substr(colon + 1));
    } else {
        size_t len = tz.size() - start;
        if (len >= 2) {
            hours = std::stoll(tz.substr(start, 2));
            if (len >= 4)
                minutes = std::stoll(tz.substr(start + 2, 2));
        }
    }
    return static_cast<int32_t>(sign * (hours * 60 + minutes));
}

void extractDateFields(const MapValue* mv, int64_t& year, int64_t& month, int64_t& day) {
    year = intFromMap(mv, "year", 1970);
    month = 1;
    day = 1;

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
        return Value{TemporalValue{TemporalKind::DATE}}; // basic: epoch default

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
        return Value{TemporalValue{TemporalKind::LOCAL_TIME}};

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
        return Value{TemporalValue{TemporalKind::TIME}};

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
        return Value{TemporalValue{TemporalKind::LOCAL_DATETIME}};

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
        return Value{TemporalValue{TemporalKind::DATETIME}};

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
        tv.dur_months = intFromMap(mv, "years") * 12 + intFromMap(mv, "months");
        tv.dur_days = intFromMap(mv, "weeks") * 7 + intFromMap(mv, "days");
        tv.dur_seconds = intFromMap(mv, "hours") * 3600 + intFromMap(mv, "minutes") * 60 + intFromMap(mv, "seconds");
        tv.dur_nanos = extractNanosFromMap(mv);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{TemporalValue{TemporalKind::DURATION}};

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
            char buf[7];
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
            char buf[7];
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
