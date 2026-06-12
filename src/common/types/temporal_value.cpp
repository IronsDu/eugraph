#include "common/types/temporal_value.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace eugraph {

// ==================== Field name lookup ====================

std::optional<DateTimeField> dateTimeFieldFromString(const std::string& s) {
    if (s == "year")
        return DateTimeField::YEAR;
    if (s == "quarter")
        return DateTimeField::QUARTER;
    if (s == "month")
        return DateTimeField::MONTH;
    if (s == "day")
        return DateTimeField::DAY;
    if (s == "weekYear")
        return DateTimeField::WEEK_YEAR;
    if (s == "week")
        return DateTimeField::WEEK;
    if (s == "ordinalDay")
        return DateTimeField::ORDINAL_DAY;
    if (s == "weekDay")
        return DateTimeField::WEEK_DAY;
    if (s == "dayOfQuarter")
        return DateTimeField::DAY_OF_QUARTER;
    if (s == "hour")
        return DateTimeField::HOUR;
    if (s == "minute")
        return DateTimeField::MINUTE;
    if (s == "second")
        return DateTimeField::SECOND;
    if (s == "nanosecond")
        return DateTimeField::NANOSECOND;
    if (s == "millisecond")
        return DateTimeField::MILLISECOND;
    if (s == "microsecond")
        return DateTimeField::MICROSECOND;
    if (s == "timezone")
        return DateTimeField::TIMEZONE;
    if (s == "offset")
        return DateTimeField::OFFSET;
    if (s == "offsetMinutes")
        return DateTimeField::OFFSET_MINUTES;
    if (s == "offsetSeconds")
        return DateTimeField::OFFSET_SECONDS;
    if (s == "epochSeconds")
        return DateTimeField::EPOCH_SECONDS;
    if (s == "epochMillis")
        return DateTimeField::EPOCH_MILLIS;
    return std::nullopt;
}

std::optional<TimeField> timeFieldFromString(const std::string& s) {
    if (s == "hour")
        return TimeField::HOUR;
    if (s == "minute")
        return TimeField::MINUTE;
    if (s == "second")
        return TimeField::SECOND;
    if (s == "nanosecond")
        return TimeField::NANOSECOND;
    if (s == "millisecond")
        return TimeField::MILLISECOND;
    if (s == "microsecond")
        return TimeField::MICROSECOND;
    if (s == "timezone")
        return TimeField::TIMEZONE;
    if (s == "offset")
        return TimeField::OFFSET;
    if (s == "offsetMinutes")
        return TimeField::OFFSET_MINUTES;
    if (s == "offsetSeconds")
        return TimeField::OFFSET_SECONDS;
    return std::nullopt;
}

std::optional<DurationField> durationFieldFromString(const std::string& s) {
    if (s == "years")
        return DurationField::YEARS;
    if (s == "quarters")
        return DurationField::QUARTERS;
    if (s == "months")
        return DurationField::MONTHS;
    if (s == "monthsOfYear")
        return DurationField::MONTHS_OF_YEAR;
    if (s == "monthsOfQuarter")
        return DurationField::MONTHS_OF_QUARTER;
    if (s == "quartersOfYear")
        return DurationField::QUARTERS_OF_YEAR;
    if (s == "weeks")
        return DurationField::WEEKS;
    if (s == "days")
        return DurationField::DAYS;
    if (s == "daysOfWeek")
        return DurationField::DAYS_OF_WEEK;
    if (s == "hours")
        return DurationField::HOURS;
    if (s == "minutes")
        return DurationField::MINUTES;
    if (s == "seconds")
        return DurationField::SECONDS;
    if (s == "milliseconds")
        return DurationField::MILLISECONDS;
    if (s == "microseconds")
        return DurationField::MICROSECONDS;
    if (s == "nanoseconds")
        return DurationField::NANOSECONDS;
    if (s == "minutesOfHour")
        return DurationField::MINUTES_OF_HOUR;
    if (s == "secondsOfMinute")
        return DurationField::SECONDS_OF_MINUTE;
    if (s == "millisecondsOfSecond")
        return DurationField::MILLISECONDS_OF_SECOND;
    if (s == "microsecondsOfSecond")
        return DurationField::MICROSECONDS_OF_SECOND;
    if (s == "nanosecondsOfSecond")
        return DurationField::NANOSECONDS_OF_SECOND;
    return std::nullopt;
}

bool dateTimeFieldReturnsString(DateTimeField f) {
    return f == DateTimeField::TIMEZONE || f == DateTimeField::OFFSET;
}

bool timeFieldReturnsString(TimeField f) {
    return f == TimeField::TIMEZONE || f == TimeField::OFFSET;
}

// ==================== Calendar helpers ====================

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

int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
    y -= (m <= 2);
    // Floor division: C++11 truncates toward zero, but the algorithm
    // requires truncation toward negative infinity for negative years.
    int64_t era;
    if (y >= 0) {
        era = y / 400;
    } else {
        int64_t num = y - 399;
        era = num / 400;
        if (num < 0 && num % 400 != 0)
            era--;
    }
    int64_t yoe = static_cast<int64_t>(y - era * 400);
    // Handle yoe overflow (can exceed 399 for negative years divisible by 400)
    if (yoe >= 400) {
        yoe -= 400;
        era++;
    } else if (yoe < 0) {
        yoe += 400;
        era--;
    }
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = yoe + era * 400;
    y += (m <= 2 ? 1 : 0);
}

// ==================== Equality ====================

bool DateTimeValue::operator==(const DateTimeValue& o) const {
    if (kind != o.kind)
        return false;
    switch (kind) {
    case DateTimeKind::DATE:
        return year == o.year && month == o.month && day == o.day;
    case DateTimeKind::LOCAL_DATETIME:
        return year == o.year && month == o.month && day == o.day && hour == o.hour && minute == o.minute &&
               second == o.second && nanos == o.nanos;
    case DateTimeKind::DATETIME:
        return year == o.year && month == o.month && day == o.day && hour == o.hour && minute == o.minute &&
               second == o.second && nanos == o.nanos && tz_offset_sec == o.tz_offset_sec && tz_name == o.tz_name;
    default:
        return false;
    }
}

bool TimeValue::operator==(const TimeValue& o) const {
    if (kind != o.kind)
        return false;
    switch (kind) {
    case TimeKind::LOCAL_TIME:
        return hour == o.hour && minute == o.minute && second == o.second && nanos == o.nanos;
    case TimeKind::TIME:
        return hour == o.hour && minute == o.minute && second == o.second && nanos == o.nanos &&
               tz_offset_sec == o.tz_offset_sec && tz_name == o.tz_name;
    default:
        return false;
    }
}

bool DurationValue::operator==(const DurationValue& o) const {
    return months == o.months && days == o.days && seconds == o.seconds && nanos == o.nanos;
}

// ==================== Comparable ====================

int64_t temporalToComparable(const DateTimeValue& tv) {
    int64_t days = daysFromCivil(tv.year, tv.month, tv.day);
    int64_t day_ns = ((tv.hour * 3600 + tv.minute * 60 + tv.second) * 1'000'000'000LL) + tv.nanos;
    if (tv.kind == DateTimeKind::DATETIME)
        day_ns -= static_cast<int64_t>(tv.tz_offset_sec) * 1'000'000'000LL;
    return days * 86'400'000'000'000LL + day_ns;
}

int64_t temporalToComparable(const TimeValue& tv) {
    int64_t day_ns = ((tv.hour * 3600 + tv.minute * 60 + tv.second) * 1'000'000'000LL) + tv.nanos;
    if (tv.kind == TimeKind::TIME)
        day_ns -= static_cast<int64_t>(tv.tz_offset_sec) * 1'000'000'000LL;
    return day_ns;
}

// ==================== Ordering ====================

bool temporalLess(const DateTimeValue& a, const DateTimeValue& b) {
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case DateTimeKind::DATE:
        if (a.year != b.year)
            return a.year < b.year;
        if (a.month != b.month)
            return a.month < b.month;
        return a.day < b.day;
    case DateTimeKind::LOCAL_DATETIME:
        if (a.year != b.year)
            return a.year < b.year;
        if (a.month != b.month)
            return a.month < b.month;
        if (a.day != b.day)
            return a.day < b.day;
        if (a.hour != b.hour)
            return a.hour < b.hour;
        if (a.minute != b.minute)
            return a.minute < b.minute;
        if (a.second != b.second)
            return a.second < b.second;
        return a.nanos < b.nanos;
    case DateTimeKind::DATETIME: {
        int64_t a_days = daysFromCivil(a.year, a.month, a.day);
        int64_t b_days = daysFromCivil(b.year, b.month, b.day);
        if (a_days != b_days)
            return a_days < b_days;
        int64_t a_ns = ((a.hour * 3600 + a.minute * 60 + a.second) * 1'000'000'000LL) + a.nanos -
                       static_cast<int64_t>(a.tz_offset_sec) * 1'000'000'000LL;
        int64_t b_ns = ((b.hour * 3600 + b.minute * 60 + b.second) * 1'000'000'000LL) + b.nanos -
                       static_cast<int64_t>(b.tz_offset_sec) * 1'000'000'000LL;
        return a_ns < b_ns;
    }
    default:
        return false;
    }
}

bool temporalLess(const TimeValue& a, const TimeValue& b) {
    if (a.kind != b.kind)
        return false;
    int64_t a_ns = ((a.hour * 3600 + a.minute * 60 + a.second) * 1'000'000'000LL) + a.nanos;
    int64_t b_ns = ((b.hour * 3600 + b.minute * 60 + b.second) * 1'000'000'000LL) + b.nanos;
    if (a.kind == TimeKind::TIME) {
        a_ns -= static_cast<int64_t>(a.tz_offset_sec) * 1'000'000'000LL;
        b_ns -= static_cast<int64_t>(b.tz_offset_sec) * 1'000'000'000LL;
    }
    return a_ns < b_ns;
}

// ==================== Arithmetic: DateTime +/- Duration ====================

DateTimeValue addDuration(const DateTimeValue& temporal, const DurationValue& duration) {
    DateTimeValue result = temporal;
    result.month += duration.months;
    result.day += duration.days;
    normalizeDate(result.year, result.month, result.day);

    if (result.kind == DateTimeKind::DATE) {
        static constexpr int64_t kDayNanos = 86'400LL * 1'000'000'000LL;
        int64_t total_nanos = duration.seconds * 1'000'000'000LL + duration.nanos;
        int64_t extra_days = total_nanos / kDayNanos;
        result.day += extra_days;
        normalizeDate(result.year, result.month, result.day);
        return result;
    }

    int64_t total_nanos = duration.seconds * 1'000'000'000LL + duration.nanos;
    int64_t add_seconds = total_nanos / 1'000'000'000LL;
    int64_t add_nanos = total_nanos % 1'000'000'000LL;
    if (add_nanos < 0) {
        add_nanos += 1'000'000'000LL;
        add_seconds -= 1;
    }

    result.nanos += add_nanos;
    result.second += add_seconds;
    result.second += result.nanos / 1'000'000'000LL;
    result.nanos %= 1'000'000'000LL;
    result.minute += result.second / 60;
    result.second %= 60;
    result.hour += result.minute / 60;
    result.minute %= 60;

    int64_t extra_days = result.hour / 24;
    if (result.hour < 0)
        extra_days = (result.hour - 23) / 24;
    result.hour -= extra_days * 24;
    result.day += extra_days;
    normalizeDate(result.year, result.month, result.day);

    if (result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.second -= 1;
    }
    if (result.second < 0) {
        int64_t bm = (-result.second + 59) / 60;
        result.second += bm * 60;
        result.minute -= bm;
    }
    if (result.minute < 0) {
        int64_t bh = (-result.minute + 59) / 60;
        result.minute += bh * 60;
        result.hour -= bh;
    }
    if (result.hour < 0) {
        int64_t bd = (-result.hour + 23) / 24;
        result.hour += bd * 24;
        result.day -= bd;
        normalizeDate(result.year, result.month, result.day);
    }

    return result;
}

TimeValue addDuration(const TimeValue& temporal, const DurationValue& duration) {
    TimeValue result = temporal;

    int64_t total_nanos = duration.seconds * 1'000'000'000LL + duration.nanos;
    int64_t add_seconds = total_nanos / 1'000'000'000LL;
    int64_t add_nanos = total_nanos % 1'000'000'000LL;
    if (add_nanos < 0) {
        add_nanos += 1'000'000'000LL;
        add_seconds -= 1;
    }

    result.nanos += add_nanos;
    result.second += add_seconds;
    result.second += result.nanos / 1'000'000'000LL;
    result.nanos %= 1'000'000'000LL;
    result.minute += result.second / 60;
    result.second %= 60;
    result.hour += result.minute / 60;
    result.minute %= 60;
    result.hour %= 24;
    if (result.hour < 0)
        result.hour += 24;

    if (result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.second -= 1;
    }
    if (result.second < 0) {
        int64_t bm = (-result.second + 59) / 60;
        result.second += bm * 60;
        result.minute -= bm;
    }
    if (result.minute < 0) {
        int64_t bh = (-result.minute + 59) / 60;
        result.minute += bh * 60;
        result.hour -= bh;
    }

    return result;
}

DateTimeValue subDuration(const DateTimeValue& temporal, const DurationValue& duration) {
    DurationValue neg;
    neg.months = -duration.months;
    neg.days = -duration.days;
    neg.seconds = -duration.seconds;
    neg.nanos = -duration.nanos;
    return addDuration(temporal, neg);
}

TimeValue subDuration(const TimeValue& temporal, const DurationValue& duration) {
    DurationValue neg;
    neg.months = -duration.months;
    neg.days = -duration.days;
    neg.seconds = -duration.seconds;
    neg.nanos = -duration.nanos;
    return addDuration(temporal, neg);
}

// ==================== Arithmetic: Temporal - Temporal ====================

void normalizeDuration(DurationValue& dur) {
    // Normalize nanos into seconds
    if (dur.nanos < 0) {
        dur.nanos += 1'000'000'000LL;
        dur.seconds -= 1;
    } else if (dur.nanos >= 1'000'000'000LL) {
        dur.nanos -= 1'000'000'000LL;
        dur.seconds += 1;
    }
    if (dur.seconds < 0 && dur.nanos > 0) {
        dur.nanos -= 1'000'000'000LL;
        dur.seconds += 1;
    } else if (dur.seconds > 0 && dur.nanos < 0) {
        dur.nanos += 1'000'000'000LL;
        dur.seconds -= 1;
    }
}

DurationValue subtractDateTimes(const DateTimeValue& a, const DateTimeValue& b) {
    DurationValue result;
    int64_t a_ns = temporalToComparable(a);
    int64_t b_ns = temporalToComparable(b);
    int64_t diff_ns = a_ns - b_ns;
    int64_t ns_per_day = 86'400'000'000'000LL;
    result.days = diff_ns / ns_per_day;
    int64_t remaining_ns = diff_ns % ns_per_day;
    result.seconds = remaining_ns / 1'000'000'000LL;
    result.nanos = remaining_ns % 1'000'000'000LL;
    normalizeDuration(result);
    return result;
}

DurationValue subtractTimes(const TimeValue& a, const TimeValue& b) {
    DurationValue result;
    // If kinds differ, normalize both to wall clock (LOCAL_TIME) so neither gets UTC-adjusted
    TimeValue a_norm = a;
    TimeValue b_norm = b;
    if (a.kind != b.kind) {
        a_norm.kind = TimeKind::LOCAL_TIME;
        b_norm.kind = TimeKind::LOCAL_TIME;
    }
    int64_t a_ns = temporalToComparable(a_norm);
    int64_t b_ns = temporalToComparable(b_norm);
    int64_t diff_ns = a_ns - b_ns;
    result.seconds = diff_ns / 1'000'000'000LL;
    result.nanos = diff_ns % 1'000'000'000LL;
    if (result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.seconds -= 1;
    }
    // Ensure consistent sign between seconds and nanos
    if (result.seconds < 0 && result.nanos > 0) {
        result.nanos -= 1'000'000'000LL;
        result.seconds += 1;
    } else if (result.seconds > 0 && result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.seconds -= 1;
    }
    return result;
}

// ==================== Arithmetic: Duration +/- Duration ====================

DurationValue addDurations(const DurationValue& a, const DurationValue& b) {
    DurationValue result;
    result.months = a.months + b.months;
    result.days = a.days + b.days;
    int64_t total_a = a.seconds * 1'000'000'000LL + a.nanos;
    int64_t total_b = b.seconds * 1'000'000'000LL + b.nanos;
    int64_t total = total_a + total_b;
    result.seconds = total / 1'000'000'000LL;
    result.nanos = total % 1'000'000'000LL;
    if (result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.seconds -= 1;
    }
    return result;
}

DurationValue subDurations(const DurationValue& a, const DurationValue& b) {
    DurationValue neg_b;
    neg_b.months = -b.months;
    neg_b.days = -b.days;
    neg_b.seconds = -b.seconds;
    neg_b.nanos = -b.nanos;
    return addDurations(a, neg_b);
}

DurationValue mulDuration(const DurationValue& dur, int64_t factor) {
    DurationValue result;
    result.months = dur.months * factor;
    result.days = dur.days * factor;
    int64_t total = (dur.seconds * 1'000'000'000LL + dur.nanos) * factor;
    result.seconds = total / 1'000'000'000LL;
    result.nanos = total % 1'000'000'000LL;
    if (result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.seconds -= 1;
    }
    return result;
}

DurationValue divDuration(const DurationValue& dur, int64_t divisor) {
    if (divisor == 0)
        return DurationValue{};
    DurationValue result;
    static constexpr double kDaysPerMonth = 365.2425 / 12.0;

    double m = static_cast<double>(dur.months) / static_cast<double>(divisor);
    result.months = static_cast<int64_t>(std::trunc(m));
    double frac_m = m - static_cast<double>(result.months);

    double d = static_cast<double>(dur.days) / static_cast<double>(divisor) + frac_m * kDaysPerMonth;
    result.days = static_cast<int64_t>(std::trunc(d));
    double frac_d = d - static_cast<double>(result.days);

    // Divide seconds+nanos with integer truncation. The cascaded fractional
    // days (frac_d) are added AFTER division — they are already fractional.
    int64_t ns_per_divisor = (dur.seconds * 1'000'000'000LL + dur.nanos) / divisor;
    int64_t cascade_ns = static_cast<int64_t>(std::round(frac_d * 86400.0 * 1e9));
    int64_t total_sec = ns_per_divisor / 1'000'000'000LL + cascade_ns / 1'000'000'000LL;
    int64_t total_nanos = (ns_per_divisor % 1'000'000'000LL) + (cascade_ns % 1'000'000'000LL);
    result.seconds = total_sec + total_nanos / 1'000'000'000LL;
    result.nanos = total_nanos % 1'000'000'000LL;
    if (result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.seconds -= 1;
    }
    return result;
}

DurationValue mulDuration(const DurationValue& dur, double factor) {
    DurationValue result;
    static constexpr double kDaysPerMonth = 365.2425 / 12.0;
    double m = static_cast<double>(dur.months) * factor;
    result.months = static_cast<int64_t>(std::trunc(m));
    double frac_m = m - static_cast<double>(result.months);

    double d = static_cast<double>(dur.days) * factor + frac_m * kDaysPerMonth;
    result.days = static_cast<int64_t>(std::trunc(d));
    double frac_d = d - static_cast<double>(result.days);

    // Use total nanoseconds for better precision (avoid nanos/1e9 rounding)
    double total_ns = static_cast<double>(dur.seconds * 1'000'000'000LL + dur.nanos) * factor + frac_d * 86400.0 * 1e9;
    result.seconds = static_cast<int64_t>(total_ns / 1e9);
    result.nanos = static_cast<int64_t>(total_ns - static_cast<double>(result.seconds) * 1e9);

    if (result.nanos >= 1'000'000'000LL) {
        result.seconds += result.nanos / 1'000'000'000LL;
        result.nanos %= 1'000'000'000LL;
    }
    if (result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.seconds -= 1;
    }
    return result;
}

DurationValue divDuration(const DurationValue& dur, double divisor) {
    if (divisor == 0.0)
        return DurationValue{};
    return mulDuration(dur, 1.0 / divisor);
}

// ==================== Duration between ====================

DurationValue durationBetween(const DateTimeValue& a, const DateTimeValue& b) {
    DurationValue result;

    int64_t months_diff = (b.year - a.year) * 12 + (b.month - a.month);
    bool same_calendar_month = (months_diff == 0);
    if (b.day < a.day) {
        if (same_calendar_month) {
            result.months = 0;
            result.days = b.day - a.day;
        } else {
            months_diff--;
            int64_t prev_month = b.month - 1;
            int64_t prev_year = b.year;
            if (prev_month < 1) {
                prev_month = 12;
                prev_year--;
            }
            int64_t days_in_prev = daysInMonth(prev_year, prev_month);
            result.months = months_diff;
            result.days = (days_in_prev - a.day) + b.day;
        }
    } else {
        result.months = months_diff;
        result.days = b.day - a.day;
    }

    int64_t a_ns = ((a.hour * 3600 + a.minute * 60 + a.second) * 1'000'000'000LL) + a.nanos;
    int64_t b_ns = ((b.hour * 3600 + b.minute * 60 + b.second) * 1'000'000'000LL) + b.nanos;
    if (a.kind == DateTimeKind::DATETIME && b.kind == DateTimeKind::DATETIME)
        a_ns -= static_cast<int64_t>(a.tz_offset_sec) * 1'000'000'000LL;
    if (b.kind == DateTimeKind::DATETIME && a.kind == DateTimeKind::DATETIME)
        b_ns -= static_cast<int64_t>(b.tz_offset_sec) * 1'000'000'000LL;
    int64_t time_diff = b_ns - a_ns;

    // Normalize months/days conflicting signs: fold months into days (30 days/month)
    if (result.months != 0 && result.days != 0 && ((result.months < 0) != (result.days < 0))) {
        result.days += result.months * 30;
        result.months = 0;
    }

    // Normalize time sign: borrow from days if time conflicts with days
    if (time_diff < 0 && result.days > 0) {
        result.days--;
        time_diff += 86'400'000'000'000LL;
    } else if (time_diff > 0 && result.days < 0) {
        result.days++;
        time_diff -= 86'400'000'000'000LL;
    }

    // Fold days into time only for same-calendar-month (not month-normalized) cases
    if (result.months == 0 && result.days != 0 && same_calendar_month) {
        time_diff += result.days * 86'400'000'000'000LL;
        result.days = 0;
    }

    result.seconds = time_diff / 1'000'000'000LL;
    result.nanos = time_diff % 1'000'000'000LL;
    if (result.nanos < 0) {
        result.nanos += 1'000'000'000LL;
        result.seconds -= 1;
    }
    return result;
}

DurationValue durationBetween(const TimeValue& a, const TimeValue& b) {
    return subtractTimes(b, a);
}

// ==================== Epoch conversion ====================

DateTimeValue datetimeFromEpoch(int64_t seconds, int64_t nanos) {
    DateTimeValue result;
    result.kind = DateTimeKind::DATETIME;

    // Convert to total nanoseconds since epoch
    constexpr int64_t NANOS_PER_DAY = 86'400'000'000'000LL;
    constexpr int64_t NANOS_PER_SEC = 1'000'000'000LL;

    int64_t total_nanos = seconds * NANOS_PER_SEC + nanos;

    // Split into days and day-nanos
    int64_t days = total_nanos / NANOS_PER_DAY;
    int64_t day_ns = total_nanos % NANOS_PER_DAY;
    if (day_ns < 0) {
        day_ns += NANOS_PER_DAY;
        days--;
    }

    // Convert days since epoch to year/month/day
    civilFromDays(days, result.year, result.month, result.day);

    // Convert day_ns to hour/minute/second/nanos (UTC, no offset)
    int64_t sec_of_day = day_ns / NANOS_PER_SEC;
    result.nanos = day_ns % NANOS_PER_SEC;
    result.second = sec_of_day % 60;
    sec_of_day /= 60;
    result.minute = sec_of_day % 60;
    result.hour = sec_of_day / 60;

    return result;
}

// ==================== Named timezone offset lookup ====================

int32_t lookupNamedTimezoneOffset(int64_t year, int64_t month, int64_t day, const std::string& tz_name) {
    if (tz_name.find('/') == std::string::npos)
        return 0;
    try {
        auto tz = std::chrono::locate_zone(tz_name);
        auto days = std::chrono::year{static_cast<int>(year)} / std::chrono::month{static_cast<unsigned>(month)} /
                    std::chrono::day{static_cast<unsigned>(day)};
        auto tp = std::chrono::sys_days{days};
        auto info = tz->get_info(tp);
        return static_cast<int32_t>(info.offset.count());
    } catch (const std::exception&) {
        return 0;
    }
}

int32_t lookupNamedTimezoneOffset(int64_t year, int64_t month, int64_t day, int64_t hour, int64_t minute,
                                  int64_t second, const std::string& tz_name) {
    if (tz_name.find('/') == std::string::npos)
        return 0;
    try {
        auto tz = std::chrono::locate_zone(tz_name);
        namespace chr = std::chrono;
        auto ymd = chr::year{static_cast<int>(year)} / chr::month{static_cast<unsigned>(month)} /
                   chr::day{static_cast<unsigned>(day)};
        auto local_tp = chr::local_days{ymd} + chr::hours{static_cast<int>(hour)} +
                        chr::minutes{static_cast<int>(minute)} + chr::seconds{static_cast<int>(second)};
        auto sys_tp = tz->to_sys(local_tp, chr::choose::latest);
        auto info = tz->get_info(sys_tp);
        return static_cast<int32_t>(info.offset.count());
    } catch (const std::exception&) {
        return 0;
    }
}

// ==================== String formatting ====================

namespace {

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

std::string fmtSubsecond(int64_t nanos) {
    if (nanos == 0)
        return "";
    int64_t n = nanos < 0 ? -nanos : nanos;
    char buf[24];
    snprintf(buf, sizeof(buf), "%09lld", static_cast<long long>(n));
    std::string s(buf);
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    return "." + s;
}

std::string fmtTimezone(int32_t offset_sec, const std::string& tz_name) {
    if (!tz_name.empty()) {
        int32_t abs_s = offset_sec < 0 ? -offset_sec : offset_sec;
        int32_t hours = abs_s / 3600;
        int32_t minutes = (abs_s % 3600) / 60;
        int32_t seconds = abs_s % 60;
        std::string sign = offset_sec >= 0 ? "+" : "-";
        std::string result = sign + pad2(hours) + ":" + pad2(minutes);
        if (seconds != 0)
            result += ":" + pad2(seconds);
        return result + "[" + tz_name + "]";
    }
    if (offset_sec == 0)
        return "Z";
    int32_t abs_s = offset_sec < 0 ? -offset_sec : offset_sec;
    int32_t hours = abs_s / 3600;
    int32_t minutes = (abs_s % 3600) / 60;
    int32_t seconds = abs_s % 60;
    std::string sign = offset_sec >= 0 ? "+" : "-";
    std::string result = sign + pad2(hours) + ":" + pad2(minutes);
    if (seconds != 0)
        result += ":" + pad2(seconds);
    return result;
}

} // anonymous namespace

std::string temporalToString(const DateTimeValue& tv) {
    switch (tv.kind) {
    case DateTimeKind::DATE:
        return pad4(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day);
    case DateTimeKind::LOCAL_DATETIME: {
        std::string s =
            pad4(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day) + "T" + pad2(tv.hour) + ":" + pad2(tv.minute);
        if (tv.second != 0 || tv.nanos != 0)
            s += ":" + pad2(tv.second) + fmtSubsecond(tv.nanos);
        return s;
    }
    case DateTimeKind::DATETIME: {
        std::string s =
            pad4(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day) + "T" + pad2(tv.hour) + ":" + pad2(tv.minute);
        if (tv.second != 0 || tv.nanos != 0)
            s += ":" + pad2(tv.second) + fmtSubsecond(tv.nanos);
        s += fmtTimezone(tv.tz_offset_sec, tv.tz_name);
        return s;
    }
    default:
        return "";
    }
}

std::string temporalToString(const TimeValue& tv) {
    std::string s = pad2(tv.hour) + ":" + pad2(tv.minute);
    if (tv.second != 0 || tv.nanos != 0)
        s += ":" + pad2(tv.second) + fmtSubsecond(tv.nanos);
    if (tv.kind == TimeKind::TIME)
        s += fmtTimezone(tv.tz_offset_sec, tv.tz_name);
    return s;
}

std::string temporalToString(const DurationValue& tv) {
    // Normalize nanos into seconds ensuring sign consistency.
    // Avoid total_sec * 1e9 overflow for large durations (use conditional
    // adjustment instead of full multiplication).
    int64_t total_sec = tv.seconds;
    int64_t nanos = tv.nanos;
    // Only do full ns normalization when it's safe (won't overflow)
    static constexpr int64_t kMaxSafeSec = INT64_MAX / 1'000'000'000LL;
    if (total_sec <= kMaxSafeSec && total_sec >= -kMaxSafeSec) {
        int64_t total_ns = total_sec * 1'000'000'000LL + nanos;
        total_sec = total_ns / 1'000'000'000LL;
        nanos = total_ns % 1'000'000'000LL;
    }
    if (total_sec < 0 && nanos > 0) {
        total_sec += 1;
        nanos -= 1'000'000'000LL;
    } else if (total_sec > 0 && nanos < 0) {
        total_sec -= 1;
        nanos += 1'000'000'000LL;
    }

    std::ostringstream oss;
    oss << "P";
    int64_t days = tv.days;
    int64_t years = tv.months / 12;
    int64_t months = tv.months % 12;
    int64_t hours = total_sec / 3600;
    int64_t minutes = (total_sec % 3600) / 60;
    int64_t seconds = total_sec % 60;
    bool has_time = (hours != 0 || minutes != 0 || seconds != 0 || nanos != 0);
    if (years != 0)
        oss << years << "Y";
    if (months != 0)
        oss << months << "M";
    if (days != 0)
        oss << days << "D";
    if (has_time) {
        oss << "T";
        if (hours != 0)
            oss << hours << "H";
        if (minutes != 0)
            oss << minutes << "M";
        if (seconds != 0 || nanos != 0) {
            if (nanos != 0) {
                if (seconds == 0 && nanos < 0)
                    oss << "-0" << fmtSubsecond(nanos) << "S";
                else
                    oss << seconds << fmtSubsecond(nanos) << "S";
            } else {
                oss << seconds << "S";
            }
        }
    }
    std::string s = oss.str();
    if (s == "P")
        return "PT0S";
    return s;
}

} // namespace eugraph
