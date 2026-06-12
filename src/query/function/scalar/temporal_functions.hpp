#pragma once

#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <chrono>
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
    if (day_of_year < monday_week1_day)
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
    // Support both singular (temporal constructors) and plural (duration constructor) forms
    bool has_ms = hasMapKey(mv, "millisecond") || hasMapKey(mv, "milliseconds");
    bool has_us = hasMapKey(mv, "microsecond") || hasMapKey(mv, "microseconds");
    bool has_ns = hasMapKey(mv, "nanosecond") || hasMapKey(mv, "nanoseconds");
    if (has_ns && !has_ms && !has_us) {
        if (hasMapKey(mv, "nanosecond"))
            return intFromMap(mv, "nanosecond");
        return intFromMap(mv, "nanoseconds");
    }
    int64_t total = 0;
    if (hasMapKey(mv, "millisecond"))
        total += intFromMap(mv, "millisecond") * 1'000'000LL;
    else if (hasMapKey(mv, "milliseconds"))
        total += intFromMap(mv, "milliseconds") * 1'000'000LL;
    if (hasMapKey(mv, "microsecond"))
        total += intFromMap(mv, "microsecond") * 1'000LL;
    else if (hasMapKey(mv, "microseconds"))
        total += intFromMap(mv, "microseconds") * 1'000LL;
    if (hasMapKey(mv, "nanosecond"))
        total += intFromMap(mv, "nanosecond");
    else if (hasMapKey(mv, "nanoseconds"))
        total += intFromMap(mv, "nanoseconds");
    return total;
}

int32_t parseTzOffset(const std::string& tz) {
    if (tz.empty() || tz == "Z" || tz == "+00:00" || tz == "-00:00")
        return 0;
    if (tz.find('/') != std::string::npos)
        return 0; // named timezone like "Europe/Stockholm" — offset set separately
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
    return static_cast<int32_t>(sign * (hours * 3600 + minutes * 60 + seconds));
}

void extractDateFields(const MapValue* mv, int64_t& year, int64_t& month, int64_t& day) {
    // Step 1: check for base temporal value ('date' or 'datetime' key)
    bool has_date_base = false;
    for (const auto& [k, vs] : mv->entries) {
        if ((k == "date" || k == "datetime") && std::holds_alternative<DateTimeValue>(vs.value)) {
            const auto& base = std::get<DateTimeValue>(vs.value);
            year = base.year;
            month = base.month;
            day = base.day;
            has_date_base = true;
            break;
        }
    }
    if (!has_date_base) {
        year = 1970;
        month = 1;
        day = 1;
    }

    // Step 2: apply explicit field overrides from map (these take precedence over base)
    bool explicit_year = hasMapKey(mv, "year");
    bool explicit_month = hasMapKey(mv, "month");
    bool explicit_day = hasMapKey(mv, "day");
    // Save original base date fields before explicit overrides, for week/ordinal/quarter dow preservation
    int64_t base_year = year, base_month = month, base_day = day;
    if (explicit_year)
        year = intFromMap(mv, "year", 1970);
    if (explicit_month)
        month = intFromMap(mv, "month", 1);
    if (explicit_day)
        day = intFromMap(mv, "day", 1);

    bool use_week = hasMapKey(mv, "week");
    bool use_ordinal = hasMapKey(mv, "ordinalDay");
    bool use_quarter = hasMapKey(mv, "quarter");
    bool has_week_year = hasMapKey(mv, "weekYear");

    if (use_week) {
        int64_t wk = intFromMap(mv, "week", 1);
        int64_t dow;
        if (hasMapKey(mv, "dayOfWeek")) {
            dow = intFromMap(mv, "dayOfWeek", 1);
        } else if (has_date_base) {
            // Preserve day-of-week from the base date (use original base fields before overrides)
            int64_t days = daysFromCivil(base_year, base_month, base_day);
            dow = ((days % 7) + 10) % 7 + 1; // 1=Mon..7=Sun
        } else {
            dow = 1; // default Monday
        }
        // weekYear overrides the calendar year for ISO week date calculation
        // Explicit 'year' key takes precedence over 'weekYear' per Cypher spec
        // When a base date is present without explicit year/weekYear, use the base date's ISO week-year
        int64_t iso_year = year;
        if (!explicit_year) {
            if (has_week_year)
                iso_year = intFromMap(mv, "weekYear", 1970);
            else if (has_date_base)
                iso_year = isoWeekYear(base_year, base_month, base_day);
        }
        isoWeekToDate(iso_year, wk, dow, year, month, day);
    } else if (use_ordinal) {
        int64_t ord = intFromMap(mv, "ordinalDay", 1);
        ordinalToDate(year, ord, month, day);
    } else if (use_quarter) {
        int64_t q = intFromMap(mv, "quarter", 1);
        int64_t doq;
        if (hasMapKey(mv, "dayOfQuarter")) {
            doq = intFromMap(mv, "dayOfQuarter", 1);
        } else if (has_date_base) {
            // Preserve day-of-quarter from the base date (use original base fields before overrides)
            int64_t start_month = (base_month - 1) / 3 * 3 + 1;
            doq = 0;
            for (int64_t m2 = start_month; m2 < base_month; ++m2)
                doq += daysInMonth(base_year, m2);
            doq += base_day;
        } else {
            doq = 1; // default first day of quarter
        }
        quarterToDate(year, q, doq, month, day);
    } else {
        if (explicit_year || explicit_month || explicit_day)
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
DateTimeValue parseDateFromString(const std::string& s) {
    DateTimeValue tv;
    if (s.empty())
        return tv;

    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };

    // Extended year format: [+-]?YYYY...-MM-DD (more than 4 year digits)
    // Find last two dashes: ...-MM-DD
    if (s.size() >= 10) {
        size_t last_dash = s.rfind('-');
        if (last_dash != std::string::npos && last_dash >= 4) {
            size_t mid_dash = s.rfind('-', last_dash - 1);
            if (mid_dash != std::string::npos && last_dash - mid_dash == 3) {
                // Found ...-MM-DD pattern with exactly 2-digit month
                tv.year = std::stoll(s.substr(0, mid_dash));
                tv.month = std::stoll(s.substr(mid_dash + 1, 2));
                tv.day = std::stoll(s.substr(last_dash + 1));
                return tv;
            }
        }
    }

    if (s.size() >= 10 && s[4] == '-' && s[7] == '-') {
        // YYYY-MM-DD
        tv.year = std::stoll(s.substr(0, 4));
        tv.month = std::stoll(s.substr(5, 2));
        tv.day = std::stoll(s.substr(8));
    } else if (s.size() == 8 && s.find('-') == std::string::npos && s.find('W') == std::string::npos &&
               s.find('w') == std::string::npos) {
        // YYYYMMDD
        tv.year = std::stoll(s.substr(0, 4));
        tv.month = std::stoll(s.substr(4, 2));
        tv.day = std::stoll(s.substr(6, 2));
    } else if (s.find('W') != std::string::npos || s.find('w') != std::string::npos) {
        // ISO week date: YYYY-Www-D, YYYY-Www, YYYYWwwD, YYYYWww
        size_t wpos = s.find('W');
        if (wpos == std::string::npos)
            wpos = s.find('w');
        int64_t iso_year = std::stoll(s.substr(0, wpos));
        // Check for extended format: dash before W (year-W...)
        bool extended = (wpos > 0 && s[wpos - 1] == '-');
        size_t num_start = wpos + 1;
        int64_t iso_week = 0;
        int64_t iso_dow = 1;
        if (extended) {
            // Extended: YYYY-Www-D or YYYY-Www
            // Skip dash after W if present (it shouldn't be in standard, but be tolerant)
            size_t num_end = num_start;
            while (num_end < s.size() && isDigit(s[num_end]))
                num_end++;
            iso_week = std::stoll(s.substr(num_start, num_end - num_start));
            if (num_end < s.size()) {
                if (s[num_end] == '-')
                    num_end++;
                iso_dow = std::stoll(s.substr(num_end));
            }
        } else {
            // Compact: YYYYWwwD or YYYYWww (2-digit week, optional 1-digit dow)
            if (num_start + 2 <= s.size())
                iso_week = std::stoll(s.substr(num_start, 2));
            if (num_start + 2 < s.size())
                iso_dow = std::stoll(s.substr(num_start + 2));
        }
        isoWeekToDate(iso_year, iso_week, iso_dow, tv.year, tv.month, tv.day);
    } else if (s.size() == 7 && s[4] == '-' && s.rfind('-') == 4) {
        // YYYY-MM (month only, 2 digits — must come before ordinal YYYY-DDD)
        tv.year = std::stoll(s.substr(0, 4));
        tv.month = std::stoll(s.substr(5, 2));
        tv.day = 1;
    } else if (s.size() == 8 && s[4] == '-') {
        // YYYY-DDD (ordinal day with dash, 3 digits)
        tv.year = std::stoll(s.substr(0, 4));
        int64_t ordinal = std::stoll(s.substr(5));
        ordinalToDate(tv.year, ordinal, tv.month, tv.day);
    } else if (s.size() == 7 && isDigit(s[0]) && isDigit(s[1]) && isDigit(s[2]) && isDigit(s[3]) && isDigit(s[4]) &&
               isDigit(s[5]) && isDigit(s[6])) {
        // YYYYDDD (compact ordinal day)
        tv.year = std::stoll(s.substr(0, 4));
        int64_t ordinal = std::stoll(s.substr(4));
        ordinalToDate(tv.year, ordinal, tv.month, tv.day);
    } else if (s.size() == 6 && isDigit(s[0]) && isDigit(s[1]) && isDigit(s[2]) && isDigit(s[3]) && isDigit(s[4]) &&
               isDigit(s[5])) {
        // YYYYMM (compact month)
        tv.year = std::stoll(s.substr(0, 4));
        tv.month = std::stoll(s.substr(4, 2));
        tv.day = 1;
    } else if (s.size() == 4 && isDigit(s[0]) && isDigit(s[1]) && isDigit(s[2]) && isDigit(s[3])) {
        // YYYY (year only)
        tv.year = std::stoll(s);
        tv.month = 1;
        tv.day = 1;
    }
    return tv;
}

TimeValue parseTimeStr(const std::string& s, TimeKind kind) {
    TimeValue tv;
    tv.kind = kind;
    if (s.empty())
        return tv;
    size_t pos = 0;
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    // Read initial digit run
    size_t num_end = pos;
    while (num_end < s.size() && isDigit(s[num_end]))
        num_end++;
    if (num_end == pos)
        return tv;

    // Compact format: no colon immediately after the initial digits
    // (e.g., "2140", "214032.142", "22+18:00")
    bool is_compact = (num_end >= s.size() || s[num_end] != ':') && (num_end - pos >= 4);
    if (is_compact) {
        // Fixed 2-digit components: HHMM[SS[.fff]][tz]
        tv.hour = std::stoll(s.substr(pos, 2));
        pos += 2;
        tv.minute = std::stoll(s.substr(pos, 2));
        pos += 2;
        if (pos < s.size() && isDigit(s[pos])) {
            tv.second = std::stoll(s.substr(pos, 2));
            pos += 2;
        }
    } else {
        // Separator format: variable-length hour, colon-delimited components
        tv.hour = std::stoll(s.substr(pos, num_end - pos));
        pos = num_end;
        if (pos < s.size() && s[pos] == ':')
            pos++;
        if (pos < s.size() && isDigit(s[pos])) {
            tv.minute = std::stoll(s.substr(pos, 2));
            pos += 2;
        }
        if (pos < s.size() && s[pos] == ':') {
            pos++;
            tv.second = std::stoll(s.substr(pos, 2));
            pos += 2;
        } else if (pos < s.size() && isDigit(s[pos])) {
            tv.second = std::stoll(s.substr(pos, 2));
            pos += 2;
        }
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
    if (kind == TimeKind::TIME && pos < s.size()) {
        std::string tz = s.substr(pos);
        std::string tz_name_str;
        auto bracket = tz.find('[');
        if (bracket != std::string::npos) {
            auto close = tz.find(']', bracket);
            if (close != std::string::npos) {
                tz_name_str = tz.substr(bracket + 1, close - bracket - 1);
                tz = tz.substr(0, bracket);
            }
        }
        if (tz != "Z" && !tz.empty())
            tv.tz_offset_sec = parseTzOffset(tz);
        if (!tz_name_str.empty())
            tv.tz_name = tz_name_str;
    }
    return tv;
}

DateTimeValue parseDatetimeStr(const std::string& s, DateTimeKind kind) {
    DateTimeValue tv;
    tv.kind = kind;
    if (s.empty())
        return tv;

    // Find the T separator to split date and time parts
    size_t t_pos = std::string::npos;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == 'T' || s[i] == 't' || s[i] == ' ') {
            t_pos = i;
            break;
        }
    }

    if (t_pos != std::string::npos) {
        // Parse date portion using parseDateFromString (handles week, ordinal, etc.)
        std::string date_part = s.substr(0, t_pos);
        auto date_tv = parseDateFromString(date_part);
        tv.year = date_tv.year;
        tv.month = date_tv.month;
        tv.day = date_tv.day;

        // Parse time portion using parseTimeStr (handles compact and separator formats)
        TimeKind time_kind = (kind == DateTimeKind::DATETIME) ? TimeKind::TIME : TimeKind::LOCAL_TIME;
        auto time_tv = parseTimeStr(s.substr(t_pos + 1), time_kind);
        tv.hour = time_tv.hour;
        tv.minute = time_tv.minute;
        tv.second = time_tv.second;
        tv.nanos = time_tv.nanos;
        if (kind == DateTimeKind::DATETIME) {
            tv.tz_offset_sec = time_tv.tz_offset_sec;
            tv.tz_name = time_tv.tz_name;
        }
        return tv;
    }

    // No T separator — parse as date-only
    auto date_tv = parseDateFromString(s);
    tv.year = date_tv.year;
    tv.month = date_tv.month;
    tv.day = date_tv.day;
    return tv;
}

DurationValue parseDurationFromString(const std::string& s) {
    DurationValue dv;
    if (s.empty() || s[0] != 'P')
        return dv;
    size_t pos = 1;

    // Check for date-based duration format: PYYYY-MM-DDThh:mm:ss
    // Must start with 4+ digits followed by '-' after P
    if (s.size() >= 12 && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' && s[2] <= '9' && s[3] >= '0' && s[3] <= '9' &&
        s[4] >= '0' && s[4] <= '9' && s[5] == '-') {
        // Date-based format: extract components from date-time string
        auto date_tv = parseDatetimeStr(s.substr(1), DateTimeKind::DATE);
        dv.months = date_tv.year * 12 + date_tv.month;
        dv.days = date_tv.day;
        dv.seconds = date_tv.hour * 3600 + date_tv.minute * 60 + date_tv.second;
        dv.nanos = date_tv.nanos;
        return dv;
    }

    static constexpr double kDaysPerMonth = 365.2425 / 12.0;
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
        double frac = val - static_cast<double>(int_part);
        switch (unit) {
        case 'Y':
            dv.months += int_part * 12;
            if (frac != 0) {
                double frac_days = frac * 12.0 * kDaysPerMonth;
                int64_t frac_days_int = static_cast<int64_t>(frac_days);
                dv.days += frac_days_int;
                double frac_secs = (frac_days - static_cast<double>(frac_days_int)) * 86400.0;
                int64_t frac_secs_int = static_cast<int64_t>(frac_secs);
                dv.seconds += frac_secs_int;
                dv.nanos += static_cast<int64_t>((frac_secs - static_cast<double>(frac_secs_int)) * 1'000'000'000.0);
            }
            break;
        case 'M':
            if (!in_time) {
                dv.months += int_part;
                if (frac != 0) {
                    double frac_days = frac * kDaysPerMonth;
                    int64_t frac_days_int = static_cast<int64_t>(frac_days);
                    dv.days += frac_days_int;
                    double frac_secs = (frac_days - static_cast<double>(frac_days_int)) * 86400.0;
                    int64_t frac_secs_int = static_cast<int64_t>(frac_secs);
                    dv.seconds += frac_secs_int;
                    dv.nanos +=
                        static_cast<int64_t>((frac_secs - static_cast<double>(frac_secs_int)) * 1'000'000'000.0);
                }
            } else {
                dv.seconds += int_part * 60;
                if (frac != 0) {
                    double frac_secs = frac * 60.0;
                    int64_t frac_secs_int = static_cast<int64_t>(frac_secs);
                    dv.seconds += frac_secs_int;
                    dv.nanos +=
                        static_cast<int64_t>((frac_secs - static_cast<double>(frac_secs_int)) * 1'000'000'000.0);
                }
            }
            break;
        case 'W':
            dv.days += int_part * 7;
            if (frac != 0) {
                double frac_secs = frac * 7.0 * 86400.0;
                dv.seconds += static_cast<int64_t>(frac_secs);
                dv.nanos += static_cast<int64_t>((frac_secs - static_cast<int64_t>(frac_secs)) * 1'000'000'000.0);
            }
            break;
        case 'D':
            dv.days += int_part;
            if (frac != 0) {
                double frac_secs = frac * 86400.0;
                dv.seconds += static_cast<int64_t>(frac_secs);
                dv.nanos += static_cast<int64_t>((frac_secs - static_cast<int64_t>(frac_secs)) * 1'000'000'000.0);
            }
            break;
        case 'H':
            dv.seconds += int_part * 3600;
            if (frac != 0) {
                double frac_secs = frac * 3600.0;
                dv.seconds += static_cast<int64_t>(frac_secs);
                dv.nanos += static_cast<int64_t>((frac_secs - static_cast<int64_t>(frac_secs)) * 1'000'000'000.0);
            }
            break;
        case 'S': {
            dv.seconds += int_part;
            if (frac != 0) {
                // Parse fractional seconds as nanoseconds to avoid floating-point precision loss
                std::string num_str = s.substr(num_start, pos - num_start - 1);
                auto dot_pos = num_str.find('.');
                if (dot_pos != std::string::npos) {
                    std::string frac_str = num_str.substr(dot_pos + 1);
                    // Pad or truncate to 9 digits
                    if (frac_str.size() > 9)
                        frac_str.resize(9);
                    else if (frac_str.size() < 9)
                        frac_str.append(9 - frac_str.size(), '0');
                    int64_t frac_ns = std::stoll(frac_str);
                    if (neg)
                        frac_ns = -frac_ns;
                    dv.nanos += frac_ns;
                }
            }
            break;
        }
        default:
            break;
        }
    }
    normalizeDuration(dv);
    // Normalize seconds into days for parsed durations
    static constexpr int64_t kSecPerDay = 86'400;
    if (dv.seconds >= kSecPerDay || dv.seconds <= -kSecPerDay) {
        int64_t extra_days = dv.seconds / kSecPerDay;
        dv.days += extra_days;
        dv.seconds -= extra_days * kSecPerDay;
    }
    return dv;
}

} // anonymous namespace

// ==================== date ====================

inline Value dateImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};

    if (auto* mv = asMap(arg)) {
        DateTimeValue tv;
        tv.kind = DateTimeKind::DATE;
        extractDateFields(mv, tv.year, tv.month, tv.day);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseDateFromString(std::get<std::string>(arg))};

    if (std::holds_alternative<DateTimeValue>(arg)) {
        auto tv = std::get<DateTimeValue>(arg);
        tv.kind = DateTimeKind::DATE;
        return Value{tv};
    }

    return Value{};
}

inline Value dateScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty()) {
        DateTimeValue tv;
        tv.kind = DateTimeKind::DATE;
        return Value{tv};
    }
    return dateImpl(args[0]);
}

inline void dateBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        DateTimeValue tv;
        tv.kind = DateTimeKind::DATE;
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{tv});
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
        TimeValue tv;
        tv.kind = TimeKind::LOCAL_TIME;
        // Check for 'time' key as base
        for (const auto& [k, vs] : mv->entries) {
            if (k == "time" && !std::holds_alternative<std::monostate>(vs.value)) {
                if (std::holds_alternative<TimeValue>(vs.value)) {
                    tv = std::get<TimeValue>(vs.value);
                    tv.kind = TimeKind::LOCAL_TIME;
                } else if (std::holds_alternative<DateTimeValue>(vs.value)) {
                    const auto& dtv = std::get<DateTimeValue>(vs.value);
                    tv.hour = dtv.hour;
                    tv.minute = dtv.minute;
                    tv.second = dtv.second;
                    tv.nanos = dtv.nanos;
                }
                break;
            }
        }
        if (hasMapKey(mv, "hour"))
            tv.hour = intFromMap(mv, "hour");
        if (hasMapKey(mv, "minute"))
            tv.minute = intFromMap(mv, "minute");
        if (hasMapKey(mv, "second"))
            tv.second = intFromMap(mv, "second");
        if (hasMapKey(mv, "nanosecond") || hasMapKey(mv, "millisecond") || hasMapKey(mv, "microsecond"))
            tv.nanos = extractNanosFromMap(mv);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseTimeStr(std::get<std::string>(arg), TimeKind::LOCAL_TIME)};

    if (std::holds_alternative<TimeValue>(arg)) {
        auto tv = std::get<TimeValue>(arg);
        tv.kind = TimeKind::LOCAL_TIME;
        return Value{tv};
    }

    if (std::holds_alternative<DateTimeValue>(arg)) {
        const auto& dtv = std::get<DateTimeValue>(arg);
        TimeValue tv;
        tv.kind = TimeKind::LOCAL_TIME;
        tv.hour = dtv.hour;
        tv.minute = dtv.minute;
        tv.second = dtv.second;
        tv.nanos = dtv.nanos;
        return Value{tv};
    }

    return Value{};
}

inline Value localtimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty()) {
        TimeValue tv;
        tv.kind = TimeKind::LOCAL_TIME;
        return Value{tv};
    }
    return localtimeImpl(args[0]);
}

inline void localtimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        TimeValue tv;
        tv.kind = TimeKind::LOCAL_TIME;
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{tv});
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
        TimeValue tv;
        tv.kind = TimeKind::TIME;
        // Check for 'time' key as base
        bool has_base = false;
        bool base_has_tz = false;
        for (const auto& [k, vs] : mv->entries) {
            if (k == "time" && !std::holds_alternative<std::monostate>(vs.value)) {
                if (std::holds_alternative<TimeValue>(vs.value)) {
                    const auto& base_tv = std::get<TimeValue>(vs.value);
                    base_has_tz = (base_tv.kind == TimeKind::TIME);
                    tv = base_tv;
                    tv.kind = TimeKind::TIME;
                } else if (std::holds_alternative<DateTimeValue>(vs.value)) {
                    const auto& dtv = std::get<DateTimeValue>(vs.value);
                    base_has_tz = (dtv.kind == DateTimeKind::DATETIME);
                    tv.hour = dtv.hour;
                    tv.minute = dtv.minute;
                    tv.second = dtv.second;
                    tv.nanos = dtv.nanos;
                    tv.tz_offset_sec = dtv.tz_offset_sec;
                }
                has_base = true;
                break;
            }
        }
        if (hasMapKey(mv, "hour"))
            tv.hour = intFromMap(mv, "hour");
        if (hasMapKey(mv, "minute"))
            tv.minute = intFromMap(mv, "minute");
        if (hasMapKey(mv, "second"))
            tv.second = intFromMap(mv, "second");
        if (hasMapKey(mv, "nanosecond") || hasMapKey(mv, "millisecond") || hasMapKey(mv, "microsecond"))
            tv.nanos = extractNanosFromMap(mv);
        bool explicit_tz = hasMapKey(mv, "timezone");
        if (!has_base || explicit_tz) {
            std::string tz = strFromMap(mv, "timezone");
            if (tz.find('/') != std::string::npos) {
                tv.tz_name = tz;
                if (has_base && explicit_tz && base_has_tz) {
                    // Convert time from old timezone to new named timezone
                    int64_t nano_day = tv.hour * 3600LL * 1'000'000'000LL + tv.minute * 60LL * 1'000'000'000LL +
                                       tv.second * 1'000'000'000LL + tv.nanos;
                    int64_t utc_nano = nano_day - static_cast<int64_t>(tv.tz_offset_sec) * 1'000'000'000LL;
                    int32_t new_offset = lookupNamedTimezoneOffset(0, 1, 1, tz);
                    int64_t new_nano = utc_nano + static_cast<int64_t>(new_offset) * 1'000'000'000LL;
                    static constexpr int64_t kDayNs = 86'400LL * 1'000'000'000LL;
                    new_nano = ((new_nano % kDayNs) + kDayNs) % kDayNs;
                    tv.hour = new_nano / (3600LL * 1'000'000'000LL);
                    new_nano %= 3600LL * 1'000'000'000LL;
                    tv.minute = new_nano / (60LL * 1'000'000'000LL);
                    new_nano %= 60LL * 1'000'000'000LL;
                    tv.second = new_nano / 1'000'000'000LL;
                    tv.nanos = new_nano % 1'000'000'000LL;
                    tv.tz_offset_sec = new_offset;
                }
            } else {
                int32_t new_offset = parseTzOffset(tz);
                if (has_base && explicit_tz && base_has_tz) {
                    // Convert time from old timezone to new numeric timezone
                    int64_t nano_day = tv.hour * 3600LL * 1'000'000'000LL + tv.minute * 60LL * 1'000'000'000LL +
                                       tv.second * 1'000'000'000LL + tv.nanos;
                    int64_t utc_nano = nano_day - static_cast<int64_t>(tv.tz_offset_sec) * 1'000'000'000LL;
                    int64_t new_nano = utc_nano + static_cast<int64_t>(new_offset) * 1'000'000'000LL;
                    static constexpr int64_t kDayNs = 86'400LL * 1'000'000'000LL;
                    new_nano = ((new_nano % kDayNs) + kDayNs) % kDayNs;
                    tv.hour = new_nano / (3600LL * 1'000'000'000LL);
                    new_nano %= 3600LL * 1'000'000'000LL;
                    tv.minute = new_nano / (60LL * 1'000'000'000LL);
                    new_nano %= 60LL * 1'000'000'000LL;
                    tv.second = new_nano / 1'000'000'000LL;
                    tv.nanos = new_nano % 1'000'000'000LL;
                }
                tv.tz_offset_sec = new_offset;
                tv.tz_name.clear();
            }
        }
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseTimeStr(std::get<std::string>(arg), TimeKind::TIME)};

    if (std::holds_alternative<TimeValue>(arg)) {
        auto tv = std::get<TimeValue>(arg);
        tv.kind = TimeKind::TIME;
        return Value{tv};
    }

    if (std::holds_alternative<DateTimeValue>(arg)) {
        const auto& dtv = std::get<DateTimeValue>(arg);
        TimeValue tv;
        tv.kind = TimeKind::TIME;
        tv.hour = dtv.hour;
        tv.minute = dtv.minute;
        tv.second = dtv.second;
        tv.nanos = dtv.nanos;
        tv.tz_offset_sec = dtv.tz_offset_sec;
        return Value{tv};
    }

    return Value{};
}

inline Value timeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty()) {
        TimeValue tv;
        tv.kind = TimeKind::TIME;
        return Value{tv};
    }
    return timeImpl(args[0]);
}

inline void timeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        TimeValue tv;
        tv.kind = TimeKind::TIME;
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{tv});
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
        DateTimeValue tv;
        tv.kind = DateTimeKind::LOCAL_DATETIME;
        // Check for 'datetime' or 'date' key as base (for date part)
        for (const auto& [k, vs] : mv->entries) {
            if ((k == "datetime" || k == "date") && !std::holds_alternative<std::monostate>(vs.value)) {
                if (std::holds_alternative<DateTimeValue>(vs.value)) {
                    const auto& base = std::get<DateTimeValue>(vs.value);
                    tv.year = base.year;
                    tv.month = base.month;
                    tv.day = base.day;
                    if (k == "datetime") {
                        tv.hour = base.hour;
                        tv.minute = base.minute;
                        tv.second = base.second;
                        tv.nanos = base.nanos;
                    }
                } else if (std::holds_alternative<TimeValue>(vs.value)) {
                    const auto& timv = std::get<TimeValue>(vs.value);
                    tv.hour = timv.hour;
                    tv.minute = timv.minute;
                    tv.second = timv.second;
                    tv.nanos = timv.nanos;
                }
                break;
            }
        }
        // Also check for 'time' key (for time-of-day part)
        for (const auto& [k, vs] : mv->entries) {
            if (k == "time" && !std::holds_alternative<std::monostate>(vs.value)) {
                if (std::holds_alternative<TimeValue>(vs.value)) {
                    const auto& timv = std::get<TimeValue>(vs.value);
                    tv.hour = timv.hour;
                    tv.minute = timv.minute;
                    tv.second = timv.second;
                    tv.nanos = timv.nanos;
                } else if (std::holds_alternative<DateTimeValue>(vs.value)) {
                    const auto& dtv = std::get<DateTimeValue>(vs.value);
                    tv.hour = dtv.hour;
                    tv.minute = dtv.minute;
                    tv.second = dtv.second;
                    tv.nanos = dtv.nanos;
                }
                break;
            }
        }
        tv.kind = DateTimeKind::LOCAL_DATETIME;
        extractDateFields(mv, tv.year, tv.month, tv.day);
        if (hasMapKey(mv, "hour"))
            tv.hour = intFromMap(mv, "hour");
        if (hasMapKey(mv, "minute"))
            tv.minute = intFromMap(mv, "minute");
        if (hasMapKey(mv, "second"))
            tv.second = intFromMap(mv, "second");
        if (hasMapKey(mv, "nanosecond") || hasMapKey(mv, "millisecond") || hasMapKey(mv, "microsecond"))
            tv.nanos = extractNanosFromMap(mv);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseDatetimeStr(std::get<std::string>(arg), DateTimeKind::LOCAL_DATETIME)};

    if (std::holds_alternative<DateTimeValue>(arg)) {
        auto tv = std::get<DateTimeValue>(arg);
        tv.kind = DateTimeKind::LOCAL_DATETIME;
        return Value{tv};
    }

    return Value{};
}

inline Value localdatetimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty()) {
        DateTimeValue tv;
        tv.kind = DateTimeKind::LOCAL_DATETIME;
        return Value{tv};
    }
    return localdatetimeImpl(args[0]);
}

inline void localdatetimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                 const EvalContext&) {
    if (args.empty()) {
        DateTimeValue tv;
        tv.kind = DateTimeKind::LOCAL_DATETIME;
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{tv});
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
        DateTimeValue tv;
        tv.kind = DateTimeKind::DATETIME;
        // Check for 'datetime' or 'date' key as base (for date part)
        bool has_base = false;
        bool base_has_tz = false;
        for (const auto& [k, vs] : mv->entries) {
            if ((k == "datetime" || k == "date") && !std::holds_alternative<std::monostate>(vs.value)) {
                if (std::holds_alternative<DateTimeValue>(vs.value)) {
                    const auto& base = std::get<DateTimeValue>(vs.value);
                    tv.year = base.year;
                    tv.month = base.month;
                    tv.day = base.day;
                    if (k == "datetime") {
                        tv.hour = base.hour;
                        tv.minute = base.minute;
                        tv.second = base.second;
                        tv.nanos = base.nanos;
                        tv.tz_offset_sec = base.tz_offset_sec;
                        tv.tz_name = base.tz_name;
                        base_has_tz = (base.kind == DateTimeKind::DATETIME);
                    }
                } else if (std::holds_alternative<TimeValue>(vs.value)) {
                    const auto& timv = std::get<TimeValue>(vs.value);
                    tv.hour = timv.hour;
                    tv.minute = timv.minute;
                    tv.second = timv.second;
                    tv.nanos = timv.nanos;
                    tv.tz_offset_sec = timv.tz_offset_sec;
                    tv.tz_name = timv.tz_name;
                    base_has_tz = (timv.kind == TimeKind::TIME);
                }
                has_base = true;
                break;
            }
        }
        // Also check for 'time' key (for time-of-day part, including timezone)
        for (const auto& [k, vs] : mv->entries) {
            if (k == "time" && !std::holds_alternative<std::monostate>(vs.value)) {
                if (std::holds_alternative<TimeValue>(vs.value)) {
                    const auto& timv = std::get<TimeValue>(vs.value);
                    tv.hour = timv.hour;
                    tv.minute = timv.minute;
                    tv.second = timv.second;
                    tv.nanos = timv.nanos;
                    if (timv.kind == TimeKind::TIME) {
                        tv.tz_offset_sec = timv.tz_offset_sec;
                        tv.tz_name = timv.tz_name;
                        base_has_tz = true;
                    }
                } else if (std::holds_alternative<DateTimeValue>(vs.value)) {
                    const auto& dtv = std::get<DateTimeValue>(vs.value);
                    tv.hour = dtv.hour;
                    tv.minute = dtv.minute;
                    tv.second = dtv.second;
                    tv.nanos = dtv.nanos;
                    tv.tz_offset_sec = dtv.tz_offset_sec;
                    tv.tz_name = dtv.tz_name;
                    base_has_tz = (dtv.kind == DateTimeKind::DATETIME);
                }
                has_base = true;
                break;
            }
        }
        tv.kind = DateTimeKind::DATETIME;
        extractDateFields(mv, tv.year, tv.month, tv.day);
        if (hasMapKey(mv, "hour"))
            tv.hour = intFromMap(mv, "hour");
        if (hasMapKey(mv, "minute"))
            tv.minute = intFromMap(mv, "minute");
        if (hasMapKey(mv, "second"))
            tv.second = intFromMap(mv, "second");
        if (hasMapKey(mv, "nanosecond") || hasMapKey(mv, "millisecond") || hasMapKey(mv, "microsecond"))
            tv.nanos = extractNanosFromMap(mv);
        // Recompute base named timezone offset for final date before any tz conversion
        if (base_has_tz && !tv.tz_name.empty())
            tv.tz_offset_sec =
                lookupNamedTimezoneOffset(tv.year, tv.month, tv.day, tv.hour, tv.minute, tv.second, tv.tz_name);
        bool explicit_tz = hasMapKey(mv, "timezone");
        if (!has_base || explicit_tz) {
            std::string tz = strFromMap(mv, "timezone");
            if (tz.find('/') != std::string::npos) {
                int32_t new_offset = lookupNamedTimezoneOffset(tv.year, tv.month, tv.day, tz);
                if (has_base && explicit_tz && base_has_tz) {
                    int64_t nano_day = tv.hour * 3600LL * 1'000'000'000LL + tv.minute * 60LL * 1'000'000'000LL +
                                       tv.second * 1'000'000'000LL + tv.nanos;
                    int64_t utc_nano = nano_day - static_cast<int64_t>(tv.tz_offset_sec) * 1'000'000'000LL;
                    int64_t new_nano = utc_nano + static_cast<int64_t>(new_offset) * 1'000'000'000LL;
                    static constexpr int64_t kDayNs = 86'400LL * 1'000'000'000LL;
                    int64_t day_shift = 0;
                    if (new_nano < 0) {
                        day_shift = new_nano / kDayNs - 1;
                        new_nano = new_nano % kDayNs + kDayNs;
                        if (new_nano == kDayNs) {
                            new_nano = 0;
                            day_shift++;
                        }
                    } else if (new_nano >= kDayNs) {
                        day_shift = new_nano / kDayNs;
                        new_nano %= kDayNs;
                    }
                    tv.hour = new_nano / (3600LL * 1'000'000'000LL);
                    new_nano %= 3600LL * 1'000'000'000LL;
                    tv.minute = new_nano / (60LL * 1'000'000'000LL);
                    new_nano %= 60LL * 1'000'000'000LL;
                    tv.second = new_nano / 1'000'000'000LL;
                    tv.nanos = new_nano % 1'000'000'000LL;
                    if (day_shift != 0) {
                        int64_t total_days = daysFromCivil(tv.year, tv.month, tv.day) + day_shift;
                        civilFromDays(total_days, tv.year, tv.month, tv.day);
                    }
                }
                tv.tz_offset_sec = new_offset;
                tv.tz_name = tz;
            } else {
                int32_t new_offset = parseTzOffset(tz);
                if (has_base && explicit_tz && base_has_tz) {
                    int64_t nano_day = tv.hour * 3600LL * 1'000'000'000LL + tv.minute * 60LL * 1'000'000'000LL +
                                       tv.second * 1'000'000'000LL + tv.nanos;
                    int64_t utc_nano = nano_day - static_cast<int64_t>(tv.tz_offset_sec) * 1'000'000'000LL;
                    int64_t new_nano = utc_nano + static_cast<int64_t>(new_offset) * 1'000'000'000LL;
                    static constexpr int64_t kDayNs = 86'400LL * 1'000'000'000LL;
                    int64_t day_shift = 0;
                    if (new_nano < 0) {
                        day_shift = new_nano / kDayNs - 1;
                        new_nano = new_nano % kDayNs + kDayNs;
                        if (new_nano == kDayNs) {
                            new_nano = 0;
                            day_shift++;
                        }
                    } else if (new_nano >= kDayNs) {
                        day_shift = new_nano / kDayNs;
                        new_nano %= kDayNs;
                    }
                    tv.hour = new_nano / (3600LL * 1'000'000'000LL);
                    new_nano %= 3600LL * 1'000'000'000LL;
                    tv.minute = new_nano / (60LL * 1'000'000'000LL);
                    new_nano %= 60LL * 1'000'000'000LL;
                    tv.second = new_nano / 1'000'000'000LL;
                    tv.nanos = new_nano % 1'000'000'000LL;
                    if (day_shift != 0) {
                        int64_t total_days = daysFromCivil(tv.year, tv.month, tv.day) + day_shift;
                        civilFromDays(total_days, tv.year, tv.month, tv.day);
                    }
                }
                tv.tz_offset_sec = new_offset;
                tv.tz_name.clear();
            }
        }
        // Recompute named timezone offset for final date (may have changed via field overrides)
        if (!tv.tz_name.empty())
            tv.tz_offset_sec =
                lookupNamedTimezoneOffset(tv.year, tv.month, tv.day, tv.hour, tv.minute, tv.second, tv.tz_name);
        return Value{tv};
    }

    if (std::holds_alternative<std::string>(arg)) {
        auto tv = parseDatetimeStr(std::get<std::string>(arg), DateTimeKind::DATETIME);
        if (!tv.tz_name.empty() && tv.tz_offset_sec == 0)
            tv.tz_offset_sec =
                lookupNamedTimezoneOffset(tv.year, tv.month, tv.day, tv.hour, tv.minute, tv.second, tv.tz_name);
        return Value{tv};
    }

    if (std::holds_alternative<DateTimeValue>(arg)) {
        auto tv = std::get<DateTimeValue>(arg);
        tv.kind = DateTimeKind::DATETIME;
        return Value{tv};
    }

    return Value{};
}

inline Value datetimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty()) {
        DateTimeValue tv;
        tv.kind = DateTimeKind::DATETIME;
        return Value{tv};
    }
    return datetimeImpl(args[0]);
}

inline void datetimeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        DateTimeValue tv;
        tv.kind = DateTimeKind::DATETIME;
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{tv});
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
        DurationValue dv;

        double years = doubleFromMap(mv, "years");
        double months = doubleFromMap(mv, "months");
        double weeks = doubleFromMap(mv, "weeks");
        double days = doubleFromMap(mv, "days");
        double hours = doubleFromMap(mv, "hours");
        double minutes = doubleFromMap(mv, "minutes");
        double seconds = doubleFromMap(mv, "seconds");

        double total_months = years * 12.0 + months;
        double month_frac = total_months - std::floor(total_months);
        dv.months = static_cast<int64_t>(std::floor(total_months));
        if (month_frac != 0.0)
            days += month_frac * (365.2425 / 12.0);

        double week_frac = weeks - std::floor(weeks);
        dv.days = static_cast<int64_t>(std::floor(weeks)) * 7;
        if (week_frac != 0.0)
            days += week_frac * 7.0;

        double day_frac = days - std::floor(days);
        dv.days += static_cast<int64_t>(std::floor(days));
        if (day_frac != 0.0)
            hours += day_frac * 24.0;

        double hour_frac = hours - std::floor(hours);
        dv.seconds = static_cast<int64_t>(std::floor(hours)) * 3600;
        if (hour_frac != 0.0)
            minutes += hour_frac * 60.0;

        double min_frac = minutes - std::floor(minutes);
        dv.seconds += static_cast<int64_t>(std::floor(minutes)) * 60;
        if (min_frac != 0.0)
            seconds += min_frac * 60.0;

        double sec_frac = seconds - std::floor(seconds);
        dv.seconds += static_cast<int64_t>(std::floor(seconds));
        dv.nanos = static_cast<int64_t>(sec_frac * 1'000'000'000.0);

        dv.nanos += extractNanosFromMap(mv);

        if (dv.nanos >= 1'000'000'000LL || dv.nanos <= -1'000'000'000LL) {
            dv.seconds += dv.nanos / 1'000'000'000LL;
            dv.nanos %= 1'000'000'000LL;
        }
        if (dv.nanos < 0) {
            dv.nanos += 1'000'000'000LL;
            dv.seconds -= 1;
        }

        normalizeDuration(dv);
        return Value{dv};
    }

    if (std::holds_alternative<std::string>(arg))
        return Value{parseDurationFromString(std::get<std::string>(arg))};

    return Value{};
}

inline Value durationScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{DurationValue{}};
    return durationImpl(args[0]);
}

inline void durationBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty()) {
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, Value{DurationValue{}});
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

inline Value temporalAccessorImpl(const Value& tv_val, int64_t field_raw) {
    // Try DateTimeValue
    if (std::holds_alternative<DateTimeValue>(tv_val)) {
        const auto& tv = std::get<DateTimeValue>(tv_val);
        const DateTimeKind k = tv.kind;
        auto field = static_cast<DateTimeField>(field_raw);

        auto isTimeLike = [](DateTimeKind kk) {
            return kk == DateTimeKind::LOCAL_DATETIME || kk == DateTimeKind::DATETIME;
        };
        auto isZoned = [](DateTimeKind kk) { return kk == DateTimeKind::DATETIME; };

        switch (field) {
        // Date accessors
        case DateTimeField::YEAR:
            return Value{tv.year};
        case DateTimeField::QUARTER:
            return Value{int64_t((tv.month - 1) / 3 + 1)};
        case DateTimeField::MONTH:
            return Value{tv.month};
        case DateTimeField::DAY:
            return Value{tv.day};
        case DateTimeField::WEEK_YEAR:
            return Value{isoWeekYear(tv.year, tv.month, tv.day)};
        case DateTimeField::WEEK:
            return Value{isoWeekNumber(tv.year, tv.month, tv.day)};
        case DateTimeField::ORDINAL_DAY:
            return Value{ordinalDayOf(tv.year, tv.month, tv.day)};
        case DateTimeField::WEEK_DAY:
            return Value{dayOfWeekIso(tv.year, tv.month, tv.day)};
        case DateTimeField::DAY_OF_QUARTER:
            return Value{dayOfQuarter(tv.year, tv.month, tv.day)};

        // Time accessors (only for datetime-like)
        case DateTimeField::HOUR:
            if (!isTimeLike(k))
                return Value{};
            return Value{tv.hour};
        case DateTimeField::MINUTE:
            if (!isTimeLike(k))
                return Value{};
            return Value{tv.minute};
        case DateTimeField::SECOND:
            if (!isTimeLike(k))
                return Value{};
            return Value{tv.second};
        case DateTimeField::NANOSECOND:
            if (!isTimeLike(k))
                return Value{};
            return Value{tv.nanos};
        case DateTimeField::MILLISECOND:
            if (!isTimeLike(k))
                return Value{};
            return Value{tv.nanos / 1'000'000LL};
        case DateTimeField::MICROSECOND:
            if (!isTimeLike(k))
                return Value{};
            return Value{tv.nanos / 1'000LL};

        // Timezone accessors (only for zoned DATETIME)
        case DateTimeField::TIMEZONE: {
            if (!isZoned(k))
                return Value{};
            if (!tv.tz_name.empty())
                return Value{tv.tz_name};
            if (tv.tz_offset_sec == 0)
                return Value{std::string("Z")};
            int32_t abs_s = tv.tz_offset_sec < 0 ? -tv.tz_offset_sec : tv.tz_offset_sec;
            int32_t h = abs_s / 3600;
            int32_t m = (abs_s % 3600) / 60;
            int32_t s = abs_s % 60;
            char sign = tv.tz_offset_sec >= 0 ? '+' : '-';
            char buf[20];
            if (s != 0)
                snprintf(buf, sizeof(buf), "%c%02d:%02d:%02d", sign, h, m, s);
            else
                snprintf(buf, sizeof(buf), "%c%02d:%02d", sign, h, m);
            return Value{std::string(buf)};
        }
        case DateTimeField::OFFSET: {
            if (!isZoned(k))
                return Value{};
            if (tv.tz_offset_sec == 0)
                return Value{std::string("+00:00")};
            int32_t abs_s = tv.tz_offset_sec < 0 ? -tv.tz_offset_sec : tv.tz_offset_sec;
            int32_t h = abs_s / 3600;
            int32_t m = (abs_s % 3600) / 60;
            int32_t s = abs_s % 60;
            char sign = tv.tz_offset_sec >= 0 ? '+' : '-';
            char buf[20];
            if (s != 0)
                snprintf(buf, sizeof(buf), "%c%02d:%02d:%02d", sign, h, m, s);
            else
                snprintf(buf, sizeof(buf), "%c%02d:%02d", sign, h, m);
            return Value{std::string(buf)};
        }
        case DateTimeField::OFFSET_MINUTES:
            if (!isZoned(k))
                return Value{};
            return Value{int64_t(tv.tz_offset_sec) / 60};
        case DateTimeField::OFFSET_SECONDS:
            if (!isZoned(k))
                return Value{};
            return Value{int64_t(tv.tz_offset_sec)};

        // Epoch accessors (only for zoned DATETIME)
        case DateTimeField::EPOCH_SECONDS:
            if (k != DateTimeKind::DATETIME)
                return Value{};
            return Value{temporalToComparable(tv) / 1'000'000'000LL};
        case DateTimeField::EPOCH_MILLIS:
            if (k != DateTimeKind::DATETIME)
                return Value{};
            return Value{temporalToComparable(tv) / 1'000'000LL};

        default:
            return Value{};
        }
    }

    // Try TimeValue
    if (std::holds_alternative<TimeValue>(tv_val)) {
        const auto& tv = std::get<TimeValue>(tv_val);
        const TimeKind k = tv.kind;
        // Remap DateTimeField→TimeField when property stored with ANY type.
        // DateTimeField time values (HOUR=9 ... OFFSET_SECONDS=18) map to
        // TimeField (HOUR=0 ... OFFSET_SECONDS=9) by subtracting 9.
        if (field_raw >= 9 && field_raw <= 18)
            field_raw -= 9;
        else if (field_raw > 18 || field_raw < 0)
            return Value{};
        auto field = static_cast<TimeField>(field_raw);

        auto isZoned = [](TimeKind kk) { return kk == TimeKind::TIME; };

        switch (field) {
        case TimeField::HOUR:
            return Value{tv.hour};
        case TimeField::MINUTE:
            return Value{tv.minute};
        case TimeField::SECOND:
            return Value{tv.second};
        case TimeField::NANOSECOND:
            return Value{tv.nanos};
        case TimeField::MILLISECOND:
            return Value{tv.nanos / 1'000'000LL};
        case TimeField::MICROSECOND:
            return Value{tv.nanos / 1'000LL};

        // Timezone accessors
        case TimeField::TIMEZONE: {
            if (!isZoned(k))
                return Value{};
            if (!tv.tz_name.empty())
                return Value{tv.tz_name};
            if (tv.tz_offset_sec == 0)
                return Value{std::string("Z")};
            int32_t abs_s = tv.tz_offset_sec < 0 ? -tv.tz_offset_sec : tv.tz_offset_sec;
            int32_t h = abs_s / 3600;
            int32_t m = (abs_s % 3600) / 60;
            int32_t s = abs_s % 60;
            char sign = tv.tz_offset_sec >= 0 ? '+' : '-';
            char buf[20];
            if (s != 0)
                snprintf(buf, sizeof(buf), "%c%02d:%02d:%02d", sign, h, m, s);
            else
                snprintf(buf, sizeof(buf), "%c%02d:%02d", sign, h, m);
            return Value{std::string(buf)};
        }
        case TimeField::OFFSET: {
            if (!isZoned(k))
                return Value{};
            if (tv.tz_offset_sec == 0)
                return Value{std::string("+00:00")};
            int32_t abs_s = tv.tz_offset_sec < 0 ? -tv.tz_offset_sec : tv.tz_offset_sec;
            int32_t h = abs_s / 3600;
            int32_t m = (abs_s % 3600) / 60;
            int32_t s = abs_s % 60;
            char sign = tv.tz_offset_sec >= 0 ? '+' : '-';
            char buf[20];
            if (s != 0)
                snprintf(buf, sizeof(buf), "%c%02d:%02d:%02d", sign, h, m, s);
            else
                snprintf(buf, sizeof(buf), "%c%02d:%02d", sign, h, m);
            return Value{std::string(buf)};
        }
        case TimeField::OFFSET_MINUTES:
            if (!isZoned(k))
                return Value{};
            return Value{int64_t(tv.tz_offset_sec) / 60};
        case TimeField::OFFSET_SECONDS:
            if (!isZoned(k))
                return Value{};
            return Value{int64_t(tv.tz_offset_sec)};

        default:
            return Value{};
        }
    }

    // Try DurationValue
    if (std::holds_alternative<DurationValue>(tv_val)) {
        const auto& dv = std::get<DurationValue>(tv_val);
        auto field = static_cast<DurationField>(field_raw);

        switch (field) {
        // Duration accessors
        case DurationField::YEARS:
            return Value{dv.months / 12};
        case DurationField::QUARTERS:
            return Value{dv.months / 3};
        case DurationField::MONTHS:
            return Value{dv.months};
        case DurationField::MONTHS_OF_YEAR:
            return Value{dv.months % 12};
        case DurationField::MONTHS_OF_QUARTER:
            return Value{(dv.months % 12) % 3};
        case DurationField::QUARTERS_OF_YEAR:
            return Value{(dv.months % 12) / 3};
        case DurationField::WEEKS:
            return Value{dv.days / 7};
        case DurationField::DAYS:
            return Value{dv.days};
        case DurationField::DAYS_OF_WEEK:
            return Value{dv.days % 7};
        case DurationField::HOURS:
            return Value{dv.seconds / 3600};
        case DurationField::MINUTES:
            return Value{dv.seconds / 60};
        case DurationField::SECONDS:
            return Value{dv.seconds};
        case DurationField::MILLISECONDS:
            return Value{dv.seconds * 1000 + dv.nanos / 1'000'000LL};
        case DurationField::MICROSECONDS:
            return Value{dv.seconds * 1'000'000 + dv.nanos / 1'000LL};
        case DurationField::NANOSECONDS:
            return Value{dv.seconds * 1'000'000'000 + dv.nanos};
        case DurationField::MINUTES_OF_HOUR:
            return Value{(dv.seconds / 60) % 60};
        case DurationField::SECONDS_OF_MINUTE:
            return Value{dv.seconds % 60};
        case DurationField::MILLISECONDS_OF_SECOND:
            return Value{(dv.nanos / 1'000'000LL) % 1000};
        case DurationField::MICROSECONDS_OF_SECOND:
            return Value{(dv.nanos / 1'000LL) % 1000000};
        case DurationField::NANOSECONDS_OF_SECOND:
            return Value{dv.nanos % 1'000'000'000LL};

        default:
            return Value{};
        }
    }

    // Accept STRING: parse as temporal first (for property round-trip)
    if (std::holds_alternative<std::string>(tv_val)) {
        const auto& s = std::get<std::string>(tv_val);
        // Try date format first
        if (s.size() >= 8 && s.find('-') != std::string::npos && s.find(':') == std::string::npos) {
            auto dtv = parseDateFromString(s);
            return temporalAccessorImpl(Value{dtv}, field_raw);
        }
        // Try time format (no date separators, has colon)
        if (s.size() >= 5 && s.find(':') != std::string::npos && s.find('-') == std::string::npos &&
            s.find('T') == std::string::npos && s.find('t') == std::string::npos) {
            auto tvv = parseTimeStr(s, TimeKind::LOCAL_TIME);
            return temporalAccessorImpl(Value{tvv}, field_raw);
        }
        // Try datetime format
        if (s.find('T') != std::string::npos || s.find('t') != std::string::npos ||
            (s.find('-') != std::string::npos && s.find(':') != std::string::npos)) {
            auto dtv = parseDatetimeStr(s, DateTimeKind::LOCAL_DATETIME);
            return temporalAccessorImpl(Value{dtv}, field_raw);
        }
        // Fallback: try date
        auto dtv = parseDateFromString(s);
        return temporalAccessorImpl(Value{dtv}, field_raw);
    }

    return Value{};
}

inline Value temporalAccessorScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.size() < 2)
        return Value{};
    if (!std::holds_alternative<int64_t>(args[1]))
        return Value{};
    auto field_raw = std::get<int64_t>(args[1]);
    return temporalAccessorImpl(args[0], field_raw);
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
    auto field_raw = std::get<int64_t>(field_val);
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, temporalAccessorImpl(tv_col.getValue(i), field_raw));
}

// ==================== truncate ====================

namespace {

int64_t truncateToUnit(int64_t value, const std::string& unit) {
    if (unit == "millennium")
        return (value / 1000) * 1000;
    if (unit == "century")
        return (value / 100) * 100;
    if (unit == "decade")
        return (value / 10) * 10;
    if (unit == "year" || unit == "quarter" || unit == "month" || unit == "week" || unit == "day" || unit == "hour" ||
        unit == "minute" || unit == "second" || unit == "millisecond" || unit == "microsecond" || unit == "nanosecond")
        return value; // field-level truncation handled by zeroing sub-fields
    return value;
}

DateTimeValue temporalTruncate(const DateTimeValue& tv, const std::string& unit, const MapValue* fields) {
    DateTimeValue result = tv;

    bool trunc_month = false, trunc_day = false;
    bool trunc_hour = false, trunc_min = false, trunc_sec = false, trunc_nanos = false;

    if (unit == "millennium") {
        result.year = truncateToUnit(result.year, "millennium");
        trunc_month = trunc_day = true;
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "century") {
        result.year = truncateToUnit(result.year, "century");
        trunc_month = trunc_day = true;
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "decade") {
        result.year = truncateToUnit(result.year, "decade");
        trunc_month = trunc_day = true;
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "year") {
        trunc_month = trunc_day = true;
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "weekYear") {
        // Truncate to first day of ISO week-year (Monday of ISO week 1)
        int64_t wy = isoWeekYear(result.year, result.month, result.day);
        isoWeekToDate(wy, 1, 1, result.year, result.month, result.day);
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "quarter") {
        result.month = ((result.month - 1) / 3) * 3 + 1;
        trunc_day = true;
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "month") {
        trunc_day = true;
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "week" || unit == "weekday") {
        // Truncate to Monday of current week
        int64_t days = daysFromCivil(result.year, result.month, result.day);
        int64_t dow = ((days % 7) + 10) % 7 + 1; // 1=Mon
        days -= (dow - 1);
        civilFromDays(days, result.year, result.month, result.day);
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "day") {
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "hour") {
        trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "minute") {
        trunc_sec = trunc_nanos = true;
    } else if (unit == "second") {
        trunc_nanos = true;
    } else if (unit == "millisecond") {
        result.nanos = (result.nanos / 1'000'000) * 1'000'000;
    } else if (unit == "microsecond") {
        result.nanos = (result.nanos / 1'000) * 1'000;
    }
    // "nanosecond" -- no truncation needed

    if (trunc_month)
        result.month = 1;
    if (trunc_day)
        result.day = 1;
    if (trunc_hour)
        result.hour = 0;
    if (trunc_min)
        result.minute = 0;
    if (trunc_sec)
        result.second = 0;
    if (trunc_nanos)
        result.nanos = 0;

    // Apply fields from map (overlay)
    if (fields) {
        int64_t day_of_week_override = 0; // 1=Mon..7=Sun, 0=not set
        int32_t tz_override = 0;
        bool has_tz = false;
        std::string tz_name_override;

        for (const auto& [k, vs] : fields->entries) {
            if (std::holds_alternative<std::monostate>(vs.value))
                continue;
            int64_t val = 0;
            std::string str_val;
            if (std::holds_alternative<int64_t>(vs.value))
                val = std::get<int64_t>(vs.value);
            else if (std::holds_alternative<double>(vs.value))
                val = static_cast<int64_t>(std::get<double>(vs.value));
            else if (std::holds_alternative<std::string>(vs.value)) {
                str_val = std::get<std::string>(vs.value);
                val = 0;
            } else
                continue;

            if (k == "year")
                result.year = val;
            else if (k == "month")
                result.month = val;
            else if (k == "day")
                result.day = val;
            else if (k == "hour")
                result.hour = val;
            else if (k == "minute")
                result.minute = val;
            else if (k == "second")
                result.second = val;
            else if (k == "nanosecond")
                result.nanos = (result.nanos / 1'000) * 1'000 + val;
            else if (k == "millisecond")
                result.nanos = (result.nanos / 1'000'000) * 1'000'000 + val * 1'000'000;
            else if (k == "microsecond")
                result.nanos = (result.nanos / 1'000) * 1'000 + val * 1'000;
            else if (k == "dayOfWeek")
                day_of_week_override = val;
            else if (k == "timezone") {
                if (!str_val.empty()) {
                    if (str_val.find('/') != std::string::npos)
                        tz_name_override = str_val;
                    else
                        tz_override = parseTzOffset(str_val);
                    has_tz = true;
                }
            }
        }

        if (day_of_week_override > 0) {
            // Set day to the given day of the current week (1=Mon..7=Sun)
            int64_t days = daysFromCivil(result.year, result.month, result.day);
            int64_t current_dow = ((days % 7) + 10) % 7 + 1; // 1=Mon
            days += (day_of_week_override - current_dow);
            civilFromDays(days, result.year, result.month, result.day);
        }

        if (has_tz) {
            if (!tz_name_override.empty()) {
                result.tz_name = tz_name_override;
                result.tz_offset_sec =
                    lookupNamedTimezoneOffset(result.year, result.month, result.day, tz_name_override);
            } else {
                result.tz_offset_sec = tz_override;
            }
        }
    }

    return result;
}

TimeValue temporalTruncateTime(const TimeValue& tv, const std::string& unit, const MapValue* fields) {
    TimeValue result = tv;

    bool trunc_hour = false, trunc_min = false, trunc_sec = false, trunc_nanos = false;

    if (unit == "day") {
        trunc_hour = trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "hour") {
        trunc_min = trunc_sec = trunc_nanos = true;
    } else if (unit == "minute") {
        trunc_sec = trunc_nanos = true;
    } else if (unit == "second") {
        trunc_nanos = true;
    } else if (unit == "millisecond") {
        result.nanos = (result.nanos / 1'000'000) * 1'000'000;
    } else if (unit == "microsecond") {
        result.nanos = (result.nanos / 1'000) * 1'000;
    }

    if (trunc_hour)
        result.hour = 0;
    if (trunc_min)
        result.minute = 0;
    if (trunc_sec)
        result.second = 0;
    if (trunc_nanos)
        result.nanos = 0;

    // Apply fields from map (overlay)
    if (fields) {
        int32_t tz_override = 0;
        bool has_tz = false;
        std::string tz_name_override;

        for (const auto& [k, vs] : fields->entries) {
            if (std::holds_alternative<std::monostate>(vs.value))
                continue;
            int64_t val = 0;
            std::string str_val;
            if (std::holds_alternative<int64_t>(vs.value))
                val = std::get<int64_t>(vs.value);
            else if (std::holds_alternative<double>(vs.value))
                val = static_cast<int64_t>(std::get<double>(vs.value));
            else if (std::holds_alternative<std::string>(vs.value)) {
                str_val = std::get<std::string>(vs.value);
                val = 0;
            } else
                continue;

            if (k == "hour")
                result.hour = val;
            else if (k == "minute")
                result.minute = val;
            else if (k == "second")
                result.second = val;
            else if (k == "nanosecond")
                result.nanos = (result.nanos / 1'000) * 1'000 + val;
            else if (k == "millisecond")
                result.nanos = (result.nanos / 1'000'000) * 1'000'000 + val * 1'000'000;
            else if (k == "microsecond")
                result.nanos = (result.nanos / 1'000) * 1'000 + val * 1'000;
            else if (k == "timezone") {
                if (!str_val.empty()) {
                    if (str_val.find('/') != std::string::npos)
                        tz_name_override = str_val;
                    else
                        tz_override = parseTzOffset(str_val);
                    has_tz = true;
                }
            }
        }

        if (has_tz) {
            if (!tz_name_override.empty())
                result.tz_name = tz_name_override;
            result.tz_offset_sec = tz_override;
        }
    }

    return result;
}

} // namespace

inline Value temporalTruncateImpl(const Value& temporal_val, const std::string& unit, const Value& fields_val,
                                  int target_kind_raw) {
    const MapValue* fields = nullptr;
    if (std::holds_alternative<MapValue>(fields_val))
        fields = &std::get<MapValue>(fields_val);

    if (std::holds_alternative<DateTimeValue>(temporal_val)) {
        auto tv = std::get<DateTimeValue>(temporal_val);
        tv = temporalTruncate(tv, unit, fields);
        tv.kind = static_cast<DateTimeKind>(target_kind_raw);
        return Value{tv};
    }

    if (std::holds_alternative<TimeValue>(temporal_val)) {
        auto tv = std::get<TimeValue>(temporal_val);
        tv = temporalTruncateTime(tv, unit, fields);
        tv.kind = static_cast<TimeKind>(target_kind_raw);
        return Value{tv};
    }

    return Value{};
}

template <int TargetKindRaw, bool IsTimeKind = false>
inline Value temporalTruncateScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.size() < 2)
        return Value{};
    if (!std::holds_alternative<std::string>(args[0]))
        return Value{};
    std::string unit = std::get<std::string>(args[0]);
    Value fields = (args.size() >= 3) ? args[2] : Value{};

    Value temporal_val = args[1];
    if constexpr (IsTimeKind) {
        if (std::holds_alternative<DateTimeValue>(temporal_val)) {
            auto dtv = std::get<DateTimeValue>(temporal_val);
            TimeValue tv;
            tv.kind = TimeKind::LOCAL_TIME;
            tv.hour = dtv.hour;
            tv.minute = dtv.minute;
            tv.second = dtv.second;
            tv.nanos = dtv.nanos;
            tv.tz_offset_sec = dtv.tz_offset_sec;
            tv.tz_name = dtv.tz_name;
            temporal_val = Value{tv};
        }
    }

    return temporalTruncateImpl(temporal_val, unit, fields, TargetKindRaw);
}

template <int TargetKindRaw, bool IsTimeKind = false>
inline void temporalTruncateBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                    const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& unit_col = *args[0];
    const auto& tv_col = *args[1];
    std::string unit = std::get<std::string>(unit_col.getValue(0));
    Value fields = (args.size() >= 3) ? args[2]->getValue(0) : Value{};
    for (size_t i = 0; i < count; ++i) {
        Value temporal_val = tv_col.getValue(i);
        if constexpr (IsTimeKind) {
            if (std::holds_alternative<DateTimeValue>(temporal_val)) {
                auto dtv = std::get<DateTimeValue>(temporal_val);
                TimeValue tv;
                tv.kind = TimeKind::LOCAL_TIME;
                tv.hour = dtv.hour;
                tv.minute = dtv.minute;
                tv.second = dtv.second;
                tv.nanos = dtv.nanos;
                tv.tz_offset_sec = dtv.tz_offset_sec;
                tv.tz_name = dtv.tz_name;
                temporal_val = Value{tv};
            }
        }
        result.setValue(i, temporalTruncateImpl(temporal_val, unit, fields, TargetKindRaw));
    }
}

// ==================== duration.between ====================

namespace {

// (durationBetween is now in temporal_value.cpp as a public function)

} // namespace

inline Value durationBetweenScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.size() < 2)
        return Value{};

    // DateTimeValue x DateTimeValue
    if (std::holds_alternative<DateTimeValue>(args[0]) && std::holds_alternative<DateTimeValue>(args[1]))
        return Value{durationBetween(std::get<DateTimeValue>(args[0]), std::get<DateTimeValue>(args[1]))};

    // TimeValue x TimeValue
    if (std::holds_alternative<TimeValue>(args[0]) && std::holds_alternative<TimeValue>(args[1]))
        return Value{durationBetween(std::get<TimeValue>(args[0]), std::get<TimeValue>(args[1]))};

    // Cross-type: convert DateTimeValue to TimeValue and compare time components only
    auto toTime = [](const Value& v) -> std::optional<TimeValue> {
        if (std::holds_alternative<DateTimeValue>(v)) {
            const auto& dtv = std::get<DateTimeValue>(v);
            TimeValue tv;
            tv.kind = (dtv.kind == DateTimeKind::DATETIME) ? TimeKind::TIME : TimeKind::LOCAL_TIME;
            tv.hour = dtv.hour;
            tv.minute = dtv.minute;
            tv.second = dtv.second;
            tv.nanos = dtv.nanos;
            tv.tz_offset_sec = dtv.tz_offset_sec;
            tv.tz_name = dtv.tz_name;
            return tv;
        }
        if (std::holds_alternative<TimeValue>(v))
            return std::get<TimeValue>(v);
        return std::nullopt;
    };

    auto a = toTime(args[0]);
    auto b = toTime(args[1]);
    if (a && b)
        return Value{durationBetween(*a, *b)};

    return Value{};
}

inline void durationBetweenBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                   const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& col_a = *args[0];
    const auto& col_b = *args[1];
    for (size_t i = 0; i < count; ++i) {
        Value a = col_a.getValue(i), b = col_b.getValue(i);

        if (std::holds_alternative<DateTimeValue>(a) && std::holds_alternative<DateTimeValue>(b))
            result.setValue(i, Value{durationBetween(std::get<DateTimeValue>(a), std::get<DateTimeValue>(b))});
        else if (std::holds_alternative<TimeValue>(a) && std::holds_alternative<TimeValue>(b))
            result.setValue(i, Value{durationBetween(std::get<TimeValue>(a), std::get<TimeValue>(b))});
        else {
            // Cross-type: convert DateTimeValue to TimeValue
            auto toTime = [](const Value& v) -> std::optional<TimeValue> {
                if (std::holds_alternative<DateTimeValue>(v)) {
                    const auto& dtv = std::get<DateTimeValue>(v);
                    TimeValue tv;
                    tv.kind = (dtv.kind == DateTimeKind::DATETIME) ? TimeKind::TIME : TimeKind::LOCAL_TIME;
                    tv.hour = dtv.hour;
                    tv.minute = dtv.minute;
                    tv.second = dtv.second;
                    tv.nanos = dtv.nanos;
                    tv.tz_offset_sec = dtv.tz_offset_sec;
                    tv.tz_name = dtv.tz_name;
                    return tv;
                }
                if (std::holds_alternative<TimeValue>(v))
                    return std::get<TimeValue>(v);
                return std::nullopt;
            };
            auto ta = toTime(a);
            auto tb = toTime(b);
            if (ta && tb)
                result.setValue(i, Value{durationBetween(*ta, *tb)});
            else
                result.setValue(i, Value{});
        }
    }
}

// duration.inMonths: normalize duration to months
inline Value durationInMonthsScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.size() < 2)
        return Value{};

    if (std::holds_alternative<DateTimeValue>(args[0]) && std::holds_alternative<DateTimeValue>(args[1])) {
        auto dur = durationBetween(std::get<DateTimeValue>(args[0]), std::get<DateTimeValue>(args[1]));
        // Normalize days and seconds into months: duration.inMonths returns months only
        static constexpr double kDaysPerMonth = 365.2425 / 12.0;
        double total_days =
            static_cast<double>(dur.months) * kDaysPerMonth + static_cast<double>(dur.days) +
            (static_cast<double>(dur.seconds) + static_cast<double>(dur.nanos) / 1'000'000'000.0) / 86400.0;
        dur.months = static_cast<int64_t>(std::trunc(total_days / kDaysPerMonth));
        dur.days = 0;
        dur.seconds = 0;
        dur.nanos = 0;
        return Value{dur};
    }

    // Cross-type or Time-only: convert to TimeValues, compute duration, then normalize
    auto toTime = [](const Value& v) -> std::optional<TimeValue> {
        if (std::holds_alternative<DateTimeValue>(v)) {
            const auto& dtv = std::get<DateTimeValue>(v);
            TimeValue tv;
            tv.kind = (dtv.kind == DateTimeKind::DATETIME) ? TimeKind::TIME : TimeKind::LOCAL_TIME;
            tv.hour = dtv.hour;
            tv.minute = dtv.minute;
            tv.second = dtv.second;
            tv.nanos = dtv.nanos;
            tv.tz_offset_sec = dtv.tz_offset_sec;
            tv.tz_name = dtv.tz_name;
            return tv;
        }
        if (std::holds_alternative<TimeValue>(v))
            return std::get<TimeValue>(v);
        return std::nullopt;
    };

    auto a = toTime(args[0]);
    auto b = toTime(args[1]);
    if (a && b) {
        auto dur = durationBetween(*a, *b);
        dur.days = 0;
        dur.months = 0;
        dur.seconds = 0;
        dur.nanos = 0;
        return Value{dur};
    }

    return Value{};
}

inline void durationInMonthsBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                    const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& col_a = *args[0];
    const auto& col_b = *args[1];
    for (size_t i = 0; i < count; ++i) {
        Value a = col_a.getValue(i), b = col_b.getValue(i);
        if (std::holds_alternative<DateTimeValue>(a) && std::holds_alternative<DateTimeValue>(b)) {
            auto dur = durationBetween(std::get<DateTimeValue>(a), std::get<DateTimeValue>(b));
            // Normalize days and seconds into months: duration.inMonths returns months only
            static constexpr double kDaysPerMonth = 365.2425 / 12.0;
            double total_days =
                static_cast<double>(dur.months) * kDaysPerMonth + static_cast<double>(dur.days) +
                (static_cast<double>(dur.seconds) + static_cast<double>(dur.nanos) / 1'000'000'000.0) / 86400.0;
            dur.months = static_cast<int64_t>(std::trunc(total_days / kDaysPerMonth));
            dur.days = 0;
            dur.seconds = 0;
            dur.nanos = 0;
            result.setValue(i, Value{dur});
        } else {
            auto toTime = [](const Value& v) -> std::optional<TimeValue> {
                if (std::holds_alternative<DateTimeValue>(v)) {
                    const auto& dtv = std::get<DateTimeValue>(v);
                    TimeValue tv;
                    tv.kind = (dtv.kind == DateTimeKind::DATETIME) ? TimeKind::TIME : TimeKind::LOCAL_TIME;
                    tv.hour = dtv.hour;
                    tv.minute = dtv.minute;
                    tv.second = dtv.second;
                    tv.nanos = dtv.nanos;
                    tv.tz_offset_sec = dtv.tz_offset_sec;
                    tv.tz_name = dtv.tz_name;
                    return tv;
                }
                if (std::holds_alternative<TimeValue>(v))
                    return std::get<TimeValue>(v);
                return std::nullopt;
            };
            auto ta = toTime(a);
            auto tb = toTime(b);
            if (ta && tb) {
                auto dur = durationBetween(*ta, *tb);
                dur.days = 0;
                dur.months = 0;
                dur.seconds = 0;
                dur.nanos = 0;
                result.setValue(i, Value{dur});
            } else {
                result.setValue(i, Value{});
            }
        }
    }
}

// ==================== duration.inSeconds / duration.inDays ====================

inline Value durationInSecondsScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.size() < 2)
        return Value{};

    if (std::holds_alternative<DateTimeValue>(args[0]) && std::holds_alternative<DateTimeValue>(args[1])) {
        const auto& a = std::get<DateTimeValue>(args[0]);
        const auto& b = std::get<DateTimeValue>(args[1]);
        bool a_zoned = (a.kind == DateTimeKind::DATETIME);
        bool b_zoned = (b.kind == DateTimeKind::DATETIME);

        auto utcNanos = [](const DateTimeValue& dtv, const std::string& ref_tz_name, int32_t ref_tz_offset) -> int64_t {
            int64_t days = daysFromCivil(dtv.year, dtv.month, dtv.day);
            int64_t day_ns = ((dtv.hour * 3600 + dtv.minute * 60 + dtv.second) * 1'000'000'000LL) + dtv.nanos;
            if (dtv.kind == DateTimeKind::DATETIME) {
                day_ns -= static_cast<int64_t>(dtv.tz_offset_sec) * 1'000'000'000LL;
            } else if (!ref_tz_name.empty()) {
                int32_t offset = lookupNamedTimezoneOffset(dtv.year, dtv.month, dtv.day, dtv.hour, dtv.minute,
                                                           dtv.second, ref_tz_name);
                day_ns -= static_cast<int64_t>(offset) * 1'000'000'000LL;
            } else {
                day_ns -= static_cast<int64_t>(ref_tz_offset) * 1'000'000'000LL;
            }
            return days * 86'400'000'000'000LL + day_ns;
        };

        if (a_zoned || b_zoned) {
            const auto& zoned = a_zoned ? a : b;
            int64_t a_utc = utcNanos(a, zoned.tz_name, zoned.tz_offset_sec);
            int64_t b_utc = utcNanos(b, zoned.tz_name, zoned.tz_offset_sec);
            int64_t total_ns = b_utc - a_utc;

            DurationValue result;
            result.seconds = total_ns / 1'000'000'000LL;
            result.nanos = total_ns % 1'000'000'000LL;
            if (result.nanos < 0) {
                result.nanos += 1'000'000'000LL;
                result.seconds -= 1;
            }
            return Value{result};
        }

        // Neither zoned: simple wall-clock difference.
        // Convert days to seconds manually to avoid day*nanosecond overflow
        // (days * 86'400'000'000'000LL can overflow int64 for large year spans).
        int64_t exact_days = daysFromCivil(b.year, b.month, b.day) - daysFromCivil(a.year, a.month, a.day);
        auto toNs = [](const DateTimeValue& dtv) -> int64_t {
            return ((dtv.hour * 60 + dtv.minute) * 60 + dtv.second) * 1'000'000'000LL + dtv.nanos;
        };
        int64_t a_ns = toNs(a);
        int64_t b_ns = toNs(b);
        int64_t time_diff_ns = b_ns - a_ns;

        DurationValue result;
        result.days = 0;
        static constexpr int64_t kSecPerDay = 86'400;
        int64_t day_sec = exact_days * kSecPerDay;
        result.seconds = day_sec + time_diff_ns / 1'000'000'000LL;
        result.nanos = time_diff_ns % 1'000'000'000LL;
        if (result.nanos < 0) {
            result.nanos += 1'000'000'000LL;
            result.seconds -= 1;
        }
        return Value{result};
    }

    // Cross-type / Time-only: convert to DateTimeValue with full timezone info
    auto toZonedDt = [](const Value& v, const Value& other) -> std::optional<DateTimeValue> {
        if (std::holds_alternative<DateTimeValue>(v))
            return std::get<DateTimeValue>(v);
        if (!std::holds_alternative<TimeValue>(v))
            return std::nullopt;
        const auto& tv = std::get<TimeValue>(v);
        if (std::holds_alternative<DateTimeValue>(other)) {
            const auto& ref = std::get<DateTimeValue>(other);
            DateTimeValue dtv;
            dtv.kind = DateTimeKind::LOCAL_DATETIME;
            dtv.year = ref.year;
            dtv.month = ref.month;
            dtv.day = ref.day;
            dtv.hour = tv.hour;
            dtv.minute = tv.minute;
            dtv.second = tv.second;
            dtv.nanos = tv.nanos;
            if (ref.kind == DateTimeKind::DATETIME || tv.kind == TimeKind::TIME) {
                dtv.kind = DateTimeKind::DATETIME;
                if (ref.kind == DateTimeKind::DATETIME) {
                    dtv.tz_name = ref.tz_name;
                    dtv.tz_offset_sec = ref.tz_offset_sec;
                }
                if (tv.kind == TimeKind::TIME) {
                    dtv.tz_name = tv.tz_name;
                    dtv.tz_offset_sec = tv.tz_offset_sec;
                }
                if (!dtv.tz_name.empty() && tv.kind == TimeKind::LOCAL_TIME) {
                    dtv.tz_offset_sec = lookupNamedTimezoneOffset(dtv.year, dtv.month, dtv.day, dtv.hour, dtv.minute,
                                                                  dtv.second, dtv.tz_name);
                }
            }
            return dtv;
        }
        if (std::holds_alternative<TimeValue>(other)) {
            DateTimeValue dtv;
            dtv.kind = (tv.kind == TimeKind::TIME) ? DateTimeKind::DATETIME : DateTimeKind::LOCAL_DATETIME;
            dtv.hour = tv.hour;
            dtv.minute = tv.minute;
            dtv.second = tv.second;
            dtv.nanos = tv.nanos;
            dtv.tz_offset_sec = tv.tz_offset_sec;
            dtv.tz_name = tv.tz_name;
            return dtv;
        }
        return std::nullopt;
    };
    auto da = toZonedDt(args[0], args[1]);
    auto db = toZonedDt(args[1], args[0]);
    if (da && db)
        return durationInSecondsScalarFn({Value{*da}, Value{*db}}, EvalContext{});

    return Value{};
}

inline void durationInSecondsBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                     const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& col_a = *args[0];
    const auto& col_b = *args[1];
    for (size_t i = 0; i < count; ++i) {
        Value a = col_a.getValue(i), b = col_b.getValue(i);
        auto r = durationInSecondsScalarFn({a, b}, EvalContext{});
        result.setValue(i, r);
    }
}

inline Value durationInDaysScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.size() < 2)
        return Value{};

    if (std::holds_alternative<DateTimeValue>(args[0]) && std::holds_alternative<DateTimeValue>(args[1])) {
        const auto& a = std::get<DateTimeValue>(args[0]);
        const auto& b = std::get<DateTimeValue>(args[1]);
        // Exact calendar days between date portions
        int64_t exact_days = daysFromCivil(b.year, b.month, b.day) - daysFromCivil(a.year, a.month, a.day);
        // Time difference in nanoseconds (wall-clock comparison)
        auto toNs = [](const DateTimeValue& dtv) -> int64_t {
            return ((dtv.hour * 60 + dtv.minute) * 60 + dtv.second) * 1'000'000'000LL + dtv.nanos;
        };
        int64_t a_ns = toNs(a);
        int64_t b_ns = toNs(b);
        // Only when both are DATETIME: adjust for timezone (compare UTC)
        if (a.kind == DateTimeKind::DATETIME && b.kind == DateTimeKind::DATETIME) {
            a_ns -= static_cast<int64_t>(a.tz_offset_sec) * 1'000'000'000LL;
            b_ns -= static_cast<int64_t>(b.tz_offset_sec) * 1'000'000'000LL;
        }
        int64_t time_diff_ns = b_ns - a_ns;

        static constexpr int64_t kDayNs = 86'400LL * 1'000'000'000LL;
        int64_t total_ns = exact_days * kDayNs + time_diff_ns;
        // Truncate toward zero (C++ default) for whole days
        int64_t total_days = total_ns / kDayNs;

        DurationValue result;
        result.days = total_days;
        return Value{result};
    }

    // Cross-type with TimeValue: return PT0S
    auto toTime = [](const Value& v) -> std::optional<TimeValue> {
        if (std::holds_alternative<DateTimeValue>(v)) {
            const auto& dtv = std::get<DateTimeValue>(v);
            TimeValue tv;
            tv.kind = (dtv.kind == DateTimeKind::DATETIME) ? TimeKind::TIME : TimeKind::LOCAL_TIME;
            tv.hour = dtv.hour;
            tv.minute = dtv.minute;
            tv.second = dtv.second;
            tv.nanos = dtv.nanos;
            tv.tz_offset_sec = dtv.tz_offset_sec;
            tv.tz_name = dtv.tz_name;
            return tv;
        }
        if (std::holds_alternative<TimeValue>(v))
            return std::get<TimeValue>(v);
        return std::nullopt;
    };
    auto a = toTime(args[0]);
    auto b = toTime(args[1]);
    if (a && b) {
        DurationValue result;
        return Value{result};
    }

    return Value{};
}

inline void durationInDaysBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                  const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& col_a = *args[0];
    const auto& col_b = *args[1];
    for (size_t i = 0; i < count; ++i) {
        Value a = col_a.getValue(i), b = col_b.getValue(i);
        auto r = durationInDaysScalarFn({a, b}, EvalContext{});
        result.setValue(i, r);
    }
}

// ==================== datetime.fromepoch / datetime.fromepochmillis ====================

inline Value datetimeFromEpochScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{};
    int64_t seconds = 0;
    int64_t nanos = 0;
    if (args.size() >= 1 && std::holds_alternative<int64_t>(args[0]))
        seconds = std::get<int64_t>(args[0]);
    if (args.size() >= 2 && std::holds_alternative<int64_t>(args[1]))
        nanos = std::get<int64_t>(args[1]);
    auto dtv = datetimeFromEpoch(seconds, nanos);
    return Value{dtv};
}

inline void datetimeFromEpochBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                     const EvalContext&) {
    if (args.empty())
        return;
    const auto& sec_col = *args[0];
    const Column* nanos_col = (args.size() >= 2) ? args[1] : nullptr;
    for (size_t i = 0; i < count; ++i) {
        int64_t seconds = std::get<int64_t>(sec_col.getValue(i));
        int64_t nanos = nanos_col ? std::get<int64_t>(nanos_col->getValue(i)) : 0LL;
        result.setValue(i, Value{datetimeFromEpoch(seconds, nanos)});
    }
}

inline Value datetimeFromEpochMillisScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (args.empty())
        return Value{};
    if (!std::holds_alternative<int64_t>(args[0]))
        return Value{};
    int64_t millis = std::get<int64_t>(args[0]);
    int64_t seconds = millis / 1000;
    int64_t nanos = (millis % 1000) * 1'000'000;
    if (nanos < 0) {
        nanos += 1'000'000'000;
        seconds -= 1;
    }
    auto dtv = datetimeFromEpoch(seconds, nanos);
    return Value{dtv};
}

inline void datetimeFromEpochMillisBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                           const EvalContext&) {
    if (args.empty())
        return;
    const auto& col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        int64_t millis = std::get<int64_t>(col.getValue(i));
        auto r = datetimeFromEpochMillisScalarFn({Value{millis}}, EvalContext{});
        result.setValue(i, r);
    }
}

// ==================== no-arg temporal accessors (transaction/statement/realtime) ====================

namespace {

DateTimeValue currentDateTimeUTC() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto dur = now.time_since_epoch();
    auto secs = duration_cast<seconds>(dur);
    auto nanos = duration_cast<nanoseconds>(dur - secs).count();
    return datetimeFromEpoch(secs.count(), nanos);
}

DateTimeValue currentDateUTC() {
    auto dtv = currentDateTimeUTC();
    dtv.kind = DateTimeKind::DATE;
    return dtv;
}

TimeValue currentTimeUTC() {
    auto dtv = currentDateTimeUTC();
    TimeValue tv;
    tv.kind = TimeKind::TIME;
    tv.hour = dtv.hour;
    tv.minute = dtv.minute;
    tv.second = dtv.second;
    tv.nanos = dtv.nanos;
    return tv;
}

TimeValue currentLocalTimeUTC() {
    auto tv = currentTimeUTC();
    tv.kind = TimeKind::LOCAL_TIME;
    return tv;
}

} // namespace

inline Value dateTransactionScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentDateUTC()};
}
inline Value dateStatementScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentDateUTC()};
}
inline Value dateRealtimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentDateUTC()};
}

inline Value localtimeTransactionScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentLocalTimeUTC()};
}
inline Value localtimeStatementScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentLocalTimeUTC()};
}
inline Value localtimeRealtimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentLocalTimeUTC()};
}

inline Value timeTransactionScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentTimeUTC()};
}
inline Value timeStatementScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentTimeUTC()};
}
inline Value timeRealtimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentTimeUTC()};
}

inline Value localdatetimeTransactionScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    auto dtv = currentDateTimeUTC();
    dtv.kind = DateTimeKind::LOCAL_DATETIME;
    return Value{dtv};
}
inline Value localdatetimeStatementScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    auto dtv = currentDateTimeUTC();
    dtv.kind = DateTimeKind::LOCAL_DATETIME;
    return Value{dtv};
}
inline Value localdatetimeRealtimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    auto dtv = currentDateTimeUTC();
    dtv.kind = DateTimeKind::LOCAL_DATETIME;
    return Value{dtv};
}

inline Value datetimeTransactionScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentDateTimeUTC()};
}
inline Value datetimeStatementScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentDateTimeUTC()};
}
inline Value datetimeRealtimeScalarFn(const std::vector<Value>& args, const EvalContext&) {
    if (!args.empty() && isNull(args[0]))
        return Value{};
    return Value{currentDateTimeUTC()};
}

} // namespace scalar
} // namespace function
} // namespace eugraph
