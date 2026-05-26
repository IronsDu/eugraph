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

namespace {

int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
    // Convert year/month/day to days since 1970-01-01 (algorithm from Howard Hinnant)
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = static_cast<int64_t>(y - era * 400);
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

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
    while (nanos > 0 && nanos % 10 == 0)
        nanos /= 10;
    return "." + std::to_string(nanos);
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
    return temporalToComparable(a) < temporalToComparable(b);
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
        if (tv.dur_months != 0)
            oss << tv.dur_months << "M";
        if (tv.dur_days != 0)
            oss << tv.dur_days << "D";
        bool has_t = false;
        if (tv.dur_seconds != 0 || tv.dur_nanos != 0) {
            if (!has_t) {
                oss << "T";
                has_t = true;
            }
            if (tv.dur_nanos != 0) {
                // Format seconds with fractional nanoseconds
                int64_t total_nanos = tv.dur_seconds * 1'000'000'000LL + tv.dur_nanos;
                int64_t secs = total_nanos / 1'000'000'000LL;
                int64_t nanos = total_nanos % 1'000'000'000LL;
                if (nanos < 0) {
                    secs -= 1;
                    nanos += 1'000'000'000LL;
                }
                oss << secs;
                if (nanos != 0) {
                    while (nanos > 0 && nanos % 10 == 0)
                        nanos /= 10;
                    oss << "." << nanos;
                }
                oss << "S";
            } else {
                oss << tv.dur_seconds << "S";
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
