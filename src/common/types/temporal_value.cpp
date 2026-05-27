#include "common/types/temporal_value.hpp"

#include <sstream>

namespace eugraph {

std::optional<TemporalField> temporalFieldFromString(const std::string& s) {
    if (s == "year")
        return TemporalField::YEAR;
    if (s == "quarter")
        return TemporalField::QUARTER;
    if (s == "month")
        return TemporalField::MONTH;
    if (s == "day")
        return TemporalField::DAY;
    if (s == "weekYear")
        return TemporalField::WEEK_YEAR;
    if (s == "week")
        return TemporalField::WEEK;
    if (s == "ordinalDay")
        return TemporalField::ORDINAL_DAY;
    if (s == "weekDay")
        return TemporalField::WEEK_DAY;
    if (s == "dayOfQuarter")
        return TemporalField::DAY_OF_QUARTER;
    if (s == "hour")
        return TemporalField::HOUR;
    if (s == "minute")
        return TemporalField::MINUTE;
    if (s == "second")
        return TemporalField::SECOND;
    if (s == "nanosecond")
        return TemporalField::NANOSECOND;
    if (s == "millisecond")
        return TemporalField::MILLISECOND;
    if (s == "microsecond")
        return TemporalField::MICROSECOND;
    if (s == "timezone")
        return TemporalField::TIMEZONE;
    if (s == "offset")
        return TemporalField::OFFSET;
    if (s == "offsetMinutes")
        return TemporalField::OFFSET_MINUTES;
    if (s == "offsetSeconds")
        return TemporalField::OFFSET_SECONDS;
    if (s == "epochSeconds")
        return TemporalField::EPOCH_SECONDS;
    if (s == "epochMillis")
        return TemporalField::EPOCH_MILLIS;
    if (s == "years")
        return TemporalField::YEARS;
    if (s == "quarters")
        return TemporalField::QUARTERS;
    if (s == "months")
        return TemporalField::MONTHS;
    if (s == "monthsOfYear")
        return TemporalField::MONTHS_OF_YEAR;
    if (s == "monthsOfQuarter")
        return TemporalField::MONTHS_OF_QUARTER;
    if (s == "quartersOfYear")
        return TemporalField::QUARTERS_OF_YEAR;
    if (s == "weeks")
        return TemporalField::WEEKS;
    if (s == "days")
        return TemporalField::DAYS;
    if (s == "daysOfWeek")
        return TemporalField::DAYS_OF_WEEK;
    if (s == "hours")
        return TemporalField::HOURS;
    if (s == "minutes")
        return TemporalField::MINUTES;
    if (s == "seconds")
        return TemporalField::SECONDS;
    if (s == "milliseconds")
        return TemporalField::MILLISECONDS;
    if (s == "microseconds")
        return TemporalField::MICROSECONDS;
    if (s == "nanoseconds")
        return TemporalField::NANOSECONDS;
    if (s == "minutesOfHour")
        return TemporalField::MINUTES_OF_HOUR;
    if (s == "secondsOfMinute")
        return TemporalField::SECONDS_OF_MINUTE;
    if (s == "millisecondsOfSecond")
        return TemporalField::MILLISECONDS_OF_SECOND;
    if (s == "microsecondsOfSecond")
        return TemporalField::MICROSECONDS_OF_SECOND;
    if (s == "nanosecondsOfSecond")
        return TemporalField::NANOSECONDS_OF_SECOND;
    return std::nullopt;
}

bool temporalFieldReturnsString(TemporalField f) {
    switch (f) {
    case TemporalField::TIMEZONE:
    case TemporalField::OFFSET:
        return true;
    default:
        return false;
    }
}

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

TemporalValue addDurationToTemporal(const TemporalValue& temporal, const TemporalValue& duration) {
    if (duration.kind != TemporalKind::DURATION)
        return temporal;

    TemporalValue result = temporal;

    bool hasDate = (result.kind == TemporalKind::DATE || result.kind == TemporalKind::DATETIME ||
                    result.kind == TemporalKind::LOCAL_DATETIME);
    bool hasTime = (result.kind != TemporalKind::DATE);

    if (hasDate) {
        result.month += duration.dur_months;
        result.day += duration.dur_days;
        normalizeDate(result.year, result.month, result.day);
    }

    if (hasTime) {
        int64_t total_nanos = duration.dur_seconds * 1'000'000'000LL + duration.dur_nanos;
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

        if (hasDate) {
            int64_t extra_days = result.hour / 24;
            if (result.hour < 0)
                extra_days = (result.hour - 23) / 24;
            result.hour -= extra_days * 24;
            result.day += extra_days;
            normalizeDate(result.year, result.month, result.day);
        }

        if (result.nanos < 0) {
            result.nanos += 1'000'000'000LL;
            result.second -= 1;
        }
        if (result.second < 0) {
            int64_t borrow_min = (-result.second + 59) / 60;
            result.second += borrow_min * 60;
            result.minute -= borrow_min;
        }
        if (result.minute < 0) {
            int64_t borrow_hour = (-result.minute + 59) / 60;
            result.minute += borrow_hour * 60;
            result.hour -= borrow_hour;
        }
        if (result.hour < 0 && hasDate) {
            int64_t borrow_day = (-result.hour + 23) / 24;
            result.hour += borrow_day * 24;
            result.day -= borrow_day;
            normalizeDate(result.year, result.month, result.day);
        }
    }

    return result;
}

TemporalValue subDurationFromTemporal(const TemporalValue& temporal, const TemporalValue& duration) {
    TemporalValue neg = duration;
    neg.dur_months = -neg.dur_months;
    neg.dur_days = -neg.dur_days;
    neg.dur_seconds = -neg.dur_seconds;
    neg.dur_nanos = -neg.dur_nanos;
    return addDurationToTemporal(temporal, neg);
}

TemporalValue subtractTemporals(const TemporalValue& a, const TemporalValue& b) {
    if (a.kind != b.kind || a.kind == TemporalKind::DURATION)
        return TemporalValue{TemporalKind::DURATION};

    int64_t a_ns = temporalToComparable(a);
    int64_t b_ns = temporalToComparable(b);
    int64_t diff_ns = a_ns - b_ns;

    TemporalValue result;
    result.kind = TemporalKind::DURATION;
    result.dur_months = 0;
    int64_t ns_per_day = 86'400'000'000'000LL;
    result.dur_days = diff_ns / ns_per_day;
    int64_t remaining_ns = diff_ns % ns_per_day;
    result.dur_seconds = remaining_ns / 1'000'000'000LL;
    result.dur_nanos = remaining_ns % 1'000'000'000LL;
    if (result.dur_nanos < 0) {
        result.dur_nanos += 1'000'000'000LL;
        result.dur_seconds -= 1;
    }
    return result;
}

TemporalValue addDurations(const TemporalValue& a, const TemporalValue& b) {
    TemporalValue result;
    result.kind = TemporalKind::DURATION;
    result.dur_months = a.dur_months + b.dur_months;
    result.dur_days = a.dur_days + b.dur_days;
    int64_t total_a = a.dur_seconds * 1'000'000'000LL + a.dur_nanos;
    int64_t total_b = b.dur_seconds * 1'000'000'000LL + b.dur_nanos;
    int64_t total = total_a + total_b;
    result.dur_seconds = total / 1'000'000'000LL;
    result.dur_nanos = total % 1'000'000'000LL;
    if (result.dur_nanos < 0) {
        result.dur_nanos += 1'000'000'000LL;
        result.dur_seconds -= 1;
    }
    return result;
}

TemporalValue subDurations(const TemporalValue& a, const TemporalValue& b) {
    TemporalValue neg_b = b;
    neg_b.dur_months = -neg_b.dur_months;
    neg_b.dur_days = -neg_b.dur_days;
    neg_b.dur_seconds = -neg_b.dur_seconds;
    neg_b.dur_nanos = -neg_b.dur_nanos;
    return addDurations(a, neg_b);
}

TemporalValue mulDuration(const TemporalValue& dur, int64_t factor) {
    TemporalValue result;
    result.kind = TemporalKind::DURATION;
    result.dur_months = dur.dur_months * factor;
    result.dur_days = dur.dur_days * factor;
    int64_t total = (dur.dur_seconds * 1'000'000'000LL + dur.dur_nanos) * factor;
    result.dur_seconds = total / 1'000'000'000LL;
    result.dur_nanos = total % 1'000'000'000LL;
    if (result.dur_nanos < 0) {
        result.dur_nanos += 1'000'000'000LL;
        result.dur_seconds -= 1;
    }
    return result;
}

TemporalValue divDuration(const TemporalValue& dur, int64_t divisor) {
    if (divisor == 0)
        return TemporalValue{TemporalKind::DURATION};
    TemporalValue result;
    result.kind = TemporalKind::DURATION;
    result.dur_months = dur.dur_months / divisor;
    result.dur_days = dur.dur_days / divisor;
    int64_t total = dur.dur_seconds * 1'000'000'000LL + dur.dur_nanos;
    total /= divisor;
    result.dur_seconds = total / 1'000'000'000LL;
    result.dur_nanos = total % 1'000'000'000LL;
    if (result.dur_nanos < 0) {
        result.dur_nanos += 1'000'000'000LL;
        result.dur_seconds -= 1;
    }
    return result;
}

int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
    // Convert year/month/day to days since 1970-01-01 (algorithm from Howard Hinnant)
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = static_cast<int64_t>(y - era * 400);
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    // Reverse of daysFromCivil: days since 1970-01-01 to year/month/day (Howard Hinnant)
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
    // Pad to 9 digits with leading zeros, then strip trailing zeros
    char buf[10];
    snprintf(buf, sizeof(buf), "%09lld", static_cast<long long>(nanos));
    std::string s(buf);
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    return "." + s;
}

std::string fmtTimezone(int32_t offset_min, const std::string& tz_name) {
    if (!tz_name.empty())
        return "+00:00[" + tz_name + "]";
    if (offset_min == 0)
        return "Z";
    int32_t abs_min = offset_min < 0 ? -offset_min : offset_min;
    std::string sign = offset_min >= 0 ? "+" : "-";
    return sign + pad2(abs_min / 60) + ":" + pad2(abs_min % 60);
}

} // anonymous namespace

bool TemporalValue::operator==(const TemporalValue& o) const {
    if (kind != o.kind)
        return false;
    switch (kind) {
    case TemporalKind::DATE:
        return year == o.year && month == o.month && day == o.day;
    case TemporalKind::LOCAL_TIME:
        return hour == o.hour && minute == o.minute && second == o.second && nanos == o.nanos;
    case TemporalKind::TIME:
        return hour == o.hour && minute == o.minute && second == o.second && nanos == o.nanos &&
               tz_offset_min == o.tz_offset_min && tz_name == o.tz_name;
    case TemporalKind::LOCAL_DATETIME:
        return year == o.year && month == o.month && day == o.day && hour == o.hour && minute == o.minute &&
               second == o.second && nanos == o.nanos;
    case TemporalKind::DATETIME:
        return year == o.year && month == o.month && day == o.day && hour == o.hour && minute == o.minute &&
               second == o.second && nanos == o.nanos && tz_offset_min == o.tz_offset_min && tz_name == o.tz_name;
    case TemporalKind::DURATION:
        return dur_months == o.dur_months && dur_days == o.dur_days && dur_seconds == o.dur_seconds &&
               dur_nanos == o.dur_nanos;
    }
    return false;
}

int64_t temporalToComparable(const TemporalValue& tv) {
    switch (tv.kind) {
    case TemporalKind::DATE:
    case TemporalKind::LOCAL_DATETIME: {
        int64_t days = daysFromCivil(tv.year, tv.month, tv.day);
        int64_t day_ns = ((tv.hour * 3600 + tv.minute * 60 + tv.second) * 1'000'000'000LL) + tv.nanos;
        return days * 86'400'000'000'000LL + day_ns;
    }
    case TemporalKind::TIME:
    case TemporalKind::DATETIME: {
        int64_t days = daysFromCivil(tv.year, tv.month, tv.day);
        int64_t day_ns = ((tv.hour * 3600 + tv.minute * 60 + tv.second) * 1'000'000'000LL) + tv.nanos;
        // Adjust by timezone offset to get UTC
        int64_t utc_day_ns = day_ns - static_cast<int64_t>(tv.tz_offset_min) * 60'000'000'000LL;
        return days * 86'400'000'000'000LL + utc_day_ns;
    }
    case TemporalKind::LOCAL_TIME:
        return ((tv.hour * 3600 + tv.minute * 60 + tv.second) * 1'000'000'000LL) + tv.nanos;
    case TemporalKind::DURATION:
        // Normalize to nanoseconds (approximate: 30 days/month)
        return ((tv.dur_months * 30 + tv.dur_days) * 86'400'000'000'000LL) + tv.dur_seconds * 1'000'000'000LL +
               tv.dur_nanos;
    }
    return 0;
}

bool temporalLess(const TemporalValue& a, const TemporalValue& b) {
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case TemporalKind::DATE:
        if (a.year != b.year)
            return a.year < b.year;
        if (a.month != b.month)
            return a.month < b.month;
        return a.day < b.day;
    case TemporalKind::LOCAL_TIME:
        if (a.hour != b.hour)
            return a.hour < b.hour;
        if (a.minute != b.minute)
            return a.minute < b.minute;
        if (a.second != b.second)
            return a.second < b.second;
        return a.nanos < b.nanos;
    case TemporalKind::TIME: {
        // Compare by UTC-adjusted nanoseconds (no date component, no overflow)
        int64_t a_ns = ((a.hour * 3600 + a.minute * 60 + a.second) * 1'000'000'000LL) + a.nanos -
                       static_cast<int64_t>(a.tz_offset_min) * 60'000'000'000LL;
        int64_t b_ns = ((b.hour * 3600 + b.minute * 60 + b.second) * 1'000'000'000LL) + b.nanos -
                       static_cast<int64_t>(b.tz_offset_min) * 60'000'000'000LL;
        return a_ns < b_ns;
    }
    case TemporalKind::LOCAL_DATETIME:
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
    case TemporalKind::DATETIME: {
        // Compare by UTC: compare days first, then time-of-day (avoids overflow)
        int64_t a_days = daysFromCivil(a.year, a.month, a.day);
        int64_t b_days = daysFromCivil(b.year, b.month, b.day);
        if (a_days != b_days)
            return a_days < b_days;
        int64_t a_ns = ((a.hour * 3600 + a.minute * 60 + a.second) * 1'000'000'000LL) + a.nanos -
                       static_cast<int64_t>(a.tz_offset_min) * 60'000'000'000LL;
        int64_t b_ns = ((b.hour * 3600 + b.minute * 60 + b.second) * 1'000'000'000LL) + b.nanos -
                       static_cast<int64_t>(b.tz_offset_min) * 60'000'000'000LL;
        return a_ns < b_ns;
    }
    case TemporalKind::DURATION:
        return temporalToComparable(a) < temporalToComparable(b);
    }
    return false;
}

std::string temporalToString(const TemporalValue& tv) {
    switch (tv.kind) {
    case TemporalKind::DATE:
        return pad4(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day);

    case TemporalKind::LOCAL_TIME:
        return pad2(tv.hour) + ":" + pad2(tv.minute) + ":" + pad2(tv.second) + fmtSubsecond(tv.nanos);

    case TemporalKind::TIME:
        return pad2(tv.hour) + ":" + pad2(tv.minute) + ":" + pad2(tv.second) + fmtSubsecond(tv.nanos) +
               fmtTimezone(tv.tz_offset_min, tv.tz_name);

    case TemporalKind::LOCAL_DATETIME:
        return pad4(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day) + "T" + pad2(tv.hour) + ":" + pad2(tv.minute) +
               ":" + pad2(tv.second) + fmtSubsecond(tv.nanos);

    case TemporalKind::DATETIME:
        return pad4(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day) + "T" + pad2(tv.hour) + ":" + pad2(tv.minute) +
               ":" + pad2(tv.second) + fmtSubsecond(tv.nanos) + fmtTimezone(tv.tz_offset_min, tv.tz_name);

    case TemporalKind::DURATION: {
        std::ostringstream oss;
        oss << "P";
        int64_t years = tv.dur_months / 12;
        int64_t months = tv.dur_months % 12;
        int64_t weeks = tv.dur_days / 7;
        int64_t days = tv.dur_days % 7;
        int64_t hours = tv.dur_seconds / 3600;
        int64_t minutes = (tv.dur_seconds % 3600) / 60;
        int64_t seconds = tv.dur_seconds % 60;

        bool has_time = (hours != 0 || minutes != 0 || seconds != 0 || tv.dur_nanos != 0);

        if (years != 0)
            oss << years << "Y";
        if (months != 0)
            oss << months << "M";
        if (weeks != 0)
            oss << weeks << "W";
        if (days != 0)
            oss << days << "D";

        if (has_time) {
            oss << "T";
            if (hours != 0)
                oss << hours << "H";
            if (minutes != 0)
                oss << minutes << "M";
            if (seconds != 0 || tv.dur_nanos != 0) {
                if (tv.dur_nanos != 0) {
                    oss << seconds << fmtSubsecond(tv.dur_nanos) << "S";
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
    }
    return "";
}

} // namespace eugraph
