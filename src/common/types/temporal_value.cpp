#include "common/types/temporal_value.hpp"
#include "common/types/query_error.hpp"

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
    // 必须 O(1)：调用方给的 day 可以很大（date + duration({seconds: 1e15}) 就是 ~1.2e10 天），
    // 逐月加减会空转上亿次，等于挂住查询。
    if (month < 1 || month > 12) {
        const int64_t zero_based = month - 1;
        const int64_t year_shift = zero_based >= 0 ? zero_based / 12 : -((-zero_based + 11) / 12);
        year += year_shift;
        month = zero_based - year_shift * 12 + 1;
    }
    // 该月的 1 号对应的纪元日 + (day - 1)，直接反算年月日（day 越界也没关系）。
    const int64_t epoch_day = daysFromCivil(year, month, 1) + (day - 1);
    civilFromDays(epoch_day, year, month, day);
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

__int128 temporalToComparable(const DateTimeValue& tv) {
    // 128 位：把日期折成"纪元纳秒"后，int64 只能表示到公元 2262 年
    // （days * 8.64e13 溢出），再往后的 datetime 排序/比较会得出错误结果。
    const __int128 days = daysFromCivil(tv.year, tv.month, tv.day);
    __int128 day_ns = (static_cast<__int128>(tv.hour) * 3600 + tv.minute * 60 + tv.second) * 1'000'000'000LL + tv.nanos;
    if (tv.kind == DateTimeKind::DATETIME)
        day_ns -= static_cast<__int128>(tv.tz_offset_sec) * 1'000'000'000LL;
    return days * 86'400'000'000'000LL + day_ns;
}

int64_t temporalToComparable(const TimeValue& tv) {
    int64_t day_ns = ((tv.hour * 3600 + tv.minute * 60 + tv.second) * 1'000'000'000LL) + tv.nanos;
    if (tv.kind == TimeKind::TIME)
        day_ns -= static_cast<int64_t>(tv.tz_offset_sec) * 1'000'000'000LL;
    return day_ns;
}

// ==================== Ordering ====================

// Local wall-clock ordering, ignoring any time zone. Used directly for the
// zone-less kinds and as the tie-break when two zoned values share an instant.
static bool localFieldsLess(const DateTimeValue& a, const DateTimeValue& b) {
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
}

static bool localFieldsLess(const TimeValue& a, const TimeValue& b) {
    if (a.hour != b.hour)
        return a.hour < b.hour;
    if (a.minute != b.minute)
        return a.minute < b.minute;
    if (a.second != b.second)
        return a.second < b.second;
    return a.nanos < b.nanos;
}

bool temporalLess(const DateTimeValue& a, const DateTimeValue& b) {
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case DateTimeKind::DATE:
    case DateTimeKind::LOCAL_DATETIME:
        return localFieldsLess(a, b);
    case DateTimeKind::DATETIME: {
        // Zoned values order by instant; equal instants fall back to the local
        // wall clock, which is what neo4j does:
        //   datetime('2024-01-01T00:00:00Z') < datetime('2024-01-01T08:00:00+08:00')
        // is TRUE (one instant, the first has the earlier local time), and
        //   datetime('2024-01-01T00:00:00Z') >= datetime('2024-01-01T00:00:00+08:00')
        // is TRUE as well because those two instants are 8h apart.
        const __int128 a_inst = temporalToComparable(a);
        const __int128 b_inst = temporalToComparable(b);
        if (a_inst != b_inst)
            return a_inst < b_inst;
        return localFieldsLess(a, b);
    }
    default:
        return false;
    }
}

bool temporalLess(const TimeValue& a, const TimeValue& b) {
    if (a.kind != b.kind)
        return false;
    if (a.kind == TimeKind::TIME) {
        const __int128 a_inst = temporalToComparable(a);
        const __int128 b_inst = temporalToComparable(b);
        if (a_inst != b_inst)
            return a_inst < b_inst;
    }
    return localFieldsLess(a, b);
}

// ==================== Arithmetic: DateTime +/- Duration ====================

DateTimeValue addDuration(const DateTimeValue& temporal, const DurationValue& duration) {
    // 粗筛（保守下限：1 个月按 28 天算）：任一分量超过整个支持范围就意味着结果必然越界。
    // 提前报错，既不做注定越界的日历运算，也保证后面的 int64 中间量不会溢出。
    constexpr int64_t kMaxEpochDay = 365241780471LL; // ±999'999'999 年
    constexpr int64_t kMaxSeconds = kMaxEpochDay * 86'400LL;
    if (duration.days < -kMaxEpochDay || duration.days > kMaxEpochDay || duration.months < -kMaxEpochDay / 28 ||
        duration.months > kMaxEpochDay / 28 || duration.seconds < -kMaxSeconds || duration.seconds > kMaxSeconds)
        throw QueryException(QueryErrorKind::Arithmetic,
                             "Invalid value for EpochDay (valid values -365243219162 - 365241780471)");
    DateTimeValue result = temporal;
    // These fields leave their normal range between an addition and its
    // normalisation, so the arithmetic runs in wide locals and is narrowed once, at
    // the end. Storing them narrow throughout would silently overflow.
    int64_t year = result.year, month = result.month, day = result.day, hour = result.hour, minute = result.minute,
            second = result.second, nanos = result.nanos;
    // There are two exits (DATE returns early), so the narrowing lives in one place.
    auto narrow = [&] {
        // 日期落在 neo4j 支持的 EpochDay 范围（±999'999'999 年）之外时报 ArithmeticError，
        // 而不是让收窄成 int32 静默回绕：date('2024-01-01') + duration({seconds: 1e18})
        // 在 neo4j 上是 "Invalid value for EpochDay (valid values -365243219162 - 365241780471)"。
        const int64_t epoch_day = daysFromCivil(year, month, day);
        if (epoch_day < -365243219162LL || epoch_day > 365241780471LL)
            throw QueryException(QueryErrorKind::Arithmetic,
                                 "Invalid value for EpochDay (valid values -365243219162 - 365241780471): " +
                                     std::to_string(epoch_day));
        result.year = static_cast<int32_t>(year);
        result.month = static_cast<int8_t>(month);
        result.day = static_cast<int8_t>(day);
        result.hour = static_cast<int8_t>(hour);
        result.minute = static_cast<int8_t>(minute);
        result.second = static_cast<int8_t>(second);
        result.nanos = static_cast<int32_t>(nanos);
    };
    // Months first, clamping the day to the last day of the target month. Adding
    // months and days together and then normalizing (the old behaviour) turned
    // date('2024-03-31') - duration('P1M') into 2024-03-02: February has no 31st,
    // so the overflow spilled into March. Neo4j clamps instead:
    // 2024-03-31 - P1M = 2024-02-29, 2024-05-31 - P1M = 2024-04-30, and
    // 2024-03-31 - P1M1D = 2024-02-28.
    if (duration.months != 0) {
        int64_t total_months = absoluteMonths(year, month) + duration.months;
        int64_t new_year = total_months >= 0 ? total_months / 12 : -((-total_months + 11) / 12);
        year = new_year;
        month = total_months - new_year * 12 + 1;
        int64_t last_day = daysInMonth(year, month);
        if (last_day > 0 && day > last_day)
            day = last_day;
    }
    day += duration.days;
    normalizeDate(year, month, day);

    if (result.kind == DateTimeKind::DATE) {
        static constexpr int64_t kDayNanos = 86'400LL * 1'000'000'000LL;
        // 128 位：|seconds| > 9.2e9（约 292 年）时 seconds * 1e9 就溢出 int64，
        // 而 duration 的 seconds 可以到 ±9.2e18（UBSan 之前在这里报 signed integer overflow）。
        const __int128 total_nanos = static_cast<__int128>(duration.seconds) * 1'000'000'000LL + duration.nanos;
        int64_t extra_days = static_cast<int64_t>(total_nanos / kDayNanos);
        day += extra_days;
        normalizeDate(year, month, day);
        narrow();
        return result;
    }

    const __int128 total_nanos = static_cast<__int128>(duration.seconds) * 1'000'000'000LL + duration.nanos;
    int64_t add_seconds = static_cast<int64_t>(total_nanos / 1'000'000'000LL);
    int64_t add_nanos = static_cast<int64_t>(total_nanos % 1'000'000'000LL);
    if (add_nanos < 0) {
        add_nanos += 1'000'000'000LL;
        add_seconds -= 1;
    }

    nanos += add_nanos;
    second += add_seconds;
    second += nanos / 1'000'000'000LL;
    nanos %= 1'000'000'000LL;
    minute += second / 60;
    second %= 60;
    hour += minute / 60;
    minute %= 60;

    int64_t extra_days = hour / 24;
    if (hour < 0)
        extra_days = (hour - 23) / 24;
    hour -= extra_days * 24;
    day += extra_days;
    normalizeDate(year, month, day);

    if (nanos < 0) {
        nanos += 1'000'000'000LL;
        second -= 1;
    }
    if (second < 0) {
        int64_t bm = (-second + 59) / 60;
        second += bm * 60;
        minute -= bm;
    }
    if (minute < 0) {
        int64_t bh = (-minute + 59) / 60;
        minute += bh * 60;
        hour -= bh;
    }
    if (hour < 0) {
        int64_t bd = (-hour + 23) / 24;
        hour += bd * 24;
        day -= bd;
        normalizeDate(year, month, day);
    }

    narrow();
    return result;
}

TimeValue addDuration(const TimeValue& temporal, const DurationValue& duration) {
    TimeValue result = temporal;
    // Same wide-local rule as the DateTimeValue overload above.
    int64_t hour = result.hour, minute = result.minute, second = result.second, nanos = result.nanos;

    const __int128 total_nanos = static_cast<__int128>(duration.seconds) * 1'000'000'000LL + duration.nanos;
    int64_t add_seconds = static_cast<int64_t>(total_nanos / 1'000'000'000LL);
    int64_t add_nanos = static_cast<int64_t>(total_nanos % 1'000'000'000LL);
    if (add_nanos < 0) {
        add_nanos += 1'000'000'000LL;
        add_seconds -= 1;
    }

    nanos += add_nanos;
    second += add_seconds;
    second += nanos / 1'000'000'000LL;
    nanos %= 1'000'000'000LL;
    minute += second / 60;
    second %= 60;
    hour += minute / 60;
    minute %= 60;
    hour %= 24;
    if (hour < 0)
        hour += 24;

    if (nanos < 0) {
        nanos += 1'000'000'000LL;
        second -= 1;
    }
    if (second < 0) {
        int64_t bm = (-second + 59) / 60;
        second += bm * 60;
        minute -= bm;
    }
    if (minute < 0) {
        int64_t bh = (-minute + 59) / 60;
        minute += bh * 60;
        hour -= bh;
    }

    result.hour = static_cast<int32_t>(hour);
    result.minute = static_cast<int8_t>(minute);
    result.second = static_cast<int8_t>(second);
    result.nanos = static_cast<int8_t>(nanos);
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

namespace {
/// 128 位中间结果收进 int64：越界报错，避免有符号溢出（UB）。
int64_t checkedNarrow(__int128 value, const char* what) {
    if (value > std::numeric_limits<int64_t>::max() || value < std::numeric_limits<int64_t>::min())
        throw QueryException(QueryErrorKind::Argument, std::string(what) + " out of range");
    return static_cast<int64_t>(value);
}
} // namespace

void normalizeDurationNanos(DurationValue& dur) {
    // java.time.Duration 的不变量：纳秒分量恒在 [0, 1e9)，符号由 seconds 承担。
    // neo4j 的 duration 继承了这个不变量，所以 .seconds / .nanosecondsOfSecond
    // 的取值必须与它一致（文本形式两者本来就相同）。
    if (dur.nanos >= 0 && dur.nanos < 1'000'000'000LL)
        return;
    int64_t carry = dur.nanos / 1'000'000'000LL; // C++ 向零截断
    int64_t rem = dur.nanos % 1'000'000'000LL;
    if (rem < 0) {
        rem += 1'000'000'000LL;
        --carry;
    }
    // 极端输入（例如参数里 seconds=INT64_MAX, nanos=1e9）下 carry 会把 seconds 顶出范围：
    // 报错而不是让有符号溢出变成 UB。
    if (carry > 0 && dur.seconds > std::numeric_limits<int64_t>::max() - carry)
        throw QueryException(QueryErrorKind::Argument, "duration out of range");
    if (carry < 0 && dur.seconds < std::numeric_limits<int64_t>::min() - carry)
        throw QueryException(QueryErrorKind::Argument, "duration out of range");
    dur.seconds += carry;
    dur.nanos = rem;
}

DurationValue subtractDateTimes(const DateTimeValue& a, const DateTimeValue& b) {
    // `a - b` is the interval from b to a, so it shares duration.between()'s month
    // and day split: date('2024-03-01') - date('2024-01-31') is P1M1D, not P30D.
    // (Neo4j itself rejects temporal - temporal with "expected Duration but was
    // Date"; we keep supporting it, but consistently with duration.between.)
    return durationBetween(b, a);
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
    const __int128 a_ns = temporalToComparable(a_norm);
    const __int128 b_ns = temporalToComparable(b_norm);
    const __int128 diff_ns = a_ns - b_ns; // time 只有一天范围，128 位只是为了统一类型
    result.seconds = static_cast<int64_t>(diff_ns / 1'000'000'000LL);
    result.nanos = static_cast<int64_t>(diff_ns % 1'000'000'000LL);
    normalizeDurationNanos(result);
    return result;
}

// ==================== Arithmetic: Duration +/- Duration ====================

DurationValue addDurations(const DurationValue& a, const DurationValue& b) {
    DurationValue result;
    result.months = checkedNarrow(static_cast<__int128>(a.months) + b.months, "duration months");
    result.days = checkedNarrow(static_cast<__int128>(a.days) + b.days, "duration days");
    // 纳秒级累加用 128 位：a.seconds * 1e9 在 |seconds| > 292 年时就溢出 int64。
    const __int128 total = (static_cast<__int128>(a.seconds) * 1'000'000'000LL + a.nanos) +
                           (static_cast<__int128>(b.seconds) * 1'000'000'000LL + b.nanos);
    result.seconds = checkedNarrow(total / 1'000'000'000LL, "duration seconds");
    result.nanos = static_cast<int64_t>(total % 1'000'000'000LL);
    normalizeDurationNanos(result);
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
    result.months = checkedNarrow(static_cast<__int128>(dur.months) * factor, "duration months");
    result.days = checkedNarrow(static_cast<__int128>(dur.days) * factor, "duration days");
    const __int128 total =
        (static_cast<__int128>(dur.seconds) * 1'000'000'000LL + dur.nanos) * static_cast<__int128>(factor);
    result.seconds = checkedNarrow(total / 1'000'000'000LL, "duration seconds");
    result.nanos = static_cast<int64_t>(total % 1'000'000'000LL);
    normalizeDurationNanos(result);
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
    const __int128 ns_per_divisor = (static_cast<__int128>(dur.seconds) * 1'000'000'000LL + dur.nanos) / divisor;
    const __int128 cascade_ns = static_cast<__int128>(std::llround(frac_d * 86400.0 * 1e9));
    const __int128 total_sec = ns_per_divisor / 1'000'000'000LL + cascade_ns / 1'000'000'000LL;
    const __int128 total_nanos = (ns_per_divisor % 1'000'000'000LL) + (cascade_ns % 1'000'000'000LL);
    result.seconds = checkedNarrow(total_sec + total_nanos / 1'000'000'000LL, "duration seconds");
    result.nanos = static_cast<int64_t>(total_nanos % 1'000'000'000LL);
    normalizeDurationNanos(result);
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

    // 秒与纳秒分开乘（neo4j 同样如此）。旧写法把两者合成 `seconds * 1e9 + nanos` 再转 double：
    // |seconds| > 9.2e9 时 int64 那一步就溢出（UB），总量超过 2^53 后纳秒还会被 double 吃掉
    // —— duration({seconds: 1e12, nanoseconds: 5e8}) * 1.5 会丢掉 0.75 秒。
    const double sec_product = static_cast<double>(dur.seconds) * factor;
    const double ns_product = static_cast<double>(dur.nanos) * factor + frac_d * 86400.0 * 1e9;
    const int64_t extra_seconds = static_cast<int64_t>(std::trunc(ns_product / 1e9));
    result.seconds = static_cast<int64_t>(std::trunc(sec_product)) + extra_seconds;
    result.nanos = static_cast<int64_t>(ns_product - static_cast<double>(extra_seconds) * 1e9);

    normalizeDurationNanos(result);
    return result;
}

DurationValue divDuration(const DurationValue& dur, double divisor) {
    if (divisor == 0.0)
        return DurationValue{};
    return mulDuration(dur, 1.0 / divisor);
}

__int128 durationOrderNanos(const DurationValue& dur) {
    static constexpr int64_t kSecondsPerMonth = 2'629'746; // 365.2425/12 天 = 30 天 + 37'746 秒
    const __int128 total_seconds =
        static_cast<__int128>(dur.months) * kSecondsPerMonth + static_cast<__int128>(dur.days) * 86'400 + dur.seconds;
    return total_seconds * 1'000'000'000 + dur.nanos;
}

// ==================== Duration between ====================

namespace {

/// `a` shifted by whole months, clamping the day to the target month's last day --
/// the same rule addDuration() applies, so the two stay consistent.
DateTimeValue addMonthsClamped(const DateTimeValue& a, int64_t months) {
    DateTimeValue r = a;
    if (months == 0)
        return r;
    int64_t total = absoluteMonths(r.year, r.month) + months;
    int64_t y = total >= 0 ? total / 12 : -((-total + 11) / 12);
    r.year = y;
    r.month = total - y * 12 + 1;
    int64_t last = daysInMonth(r.year, r.month);
    if (last > 0 && r.day > last)
        r.day = last;
    return r;
}

/// 本地字段（忽略时区偏移）的纳秒坐标。
///
/// neo4j 的 duration.between 只在**两侧都带时区**时按绝对时刻计算；
/// 只要有一侧是无时区类型（date / localdatetime），整段计算就退化为本地字段之差：
/// duration.between(date('2015-07-21'), datetime('2015-07-21T21:40:32+0100'))
/// 是 PT21H40M32S（本地 21:40:32），而不是按 UTC 的 PT20H40M32S。
__int128 localFieldsNanos(const DateTimeValue& tv) {
    // 128 位：与 temporalToComparable 同理，days * 8.64e13 在公元 2262 年后溢出 int64
    // （UBSan 会直接报 signed integer overflow）。
    const __int128 days = daysFromCivil(tv.year, tv.month, tv.day);
    const __int128 day_ns =
        (static_cast<__int128>(tv.hour) * 3600 + tv.minute * 60 + tv.second) * 1'000'000'000LL + tv.nanos;
    return days * 86'400'000'000'000LL + day_ns;
}

/// Nanoseconds since local midnight; subtracted by the zone offset for zoned
/// values so that a difference of two instants crosses zones correctly.
int64_t timeOfDayNanos(const DateTimeValue& v, bool utc) {
    int64_t ns = ((v.hour * 3600 + v.minute * 60 + v.second) * 1'000'000'000LL) + v.nanos;
    if (utc && v.kind == DateTimeKind::DATETIME)
        ns -= static_cast<int64_t>(v.tz_offset_sec) * 1'000'000'000LL;
    return ns;
}

} // namespace

DurationValue durationBetween(const DateTimeValue& a, const DateTimeValue& b) {
    // Port of neo4j's DurationValue.durationBetween (community/values
    // DurationValue.java), which is a composition of java.time units:
    //
    //   months = ChronoUnit.MONTHS.between(from, to)
    //   from  += months                    (plusMonths clamps the day-of-month)
    //   days   = ChronoUnit.DAYS.between(from, to)
    //   nanos  = ChronoUnit.NANOS.between(from, to)
    //
    // The month count is java.time's packed day-of-month comparison, and for values
    // that carry a time of day an end time earlier than the start first moves the
    // end *date* back one day. Nothing is rebalanced afterwards: the time part may
    // come out negative and the printer borrows a day for it. That is why
    // duration.between(datetime('2024-01-31T10:00:00Z'), datetime('2024-03-01T09:00:00Z'))
    // is P29DT23H and not P1MT23H -- both are exact decompositions, this is the one
    // neo4j picks.
    const bool has_time = (a.kind != DateTimeKind::DATE);
    // 参照系：两侧都带时区才是绝对时刻，否则一律按本地字段（见 localFieldsNanos）。
    // 借位的比较必须与后面 days/rem 的算法用同一个参照系，否则会出现
    // 11M30D 这种"月按本地算、余量按时刻算"的混合结果。
    const bool both_zoned = (a.kind == DateTimeKind::DATETIME && b.kind == DateTimeKind::DATETIME);

    int64_t end_year = b.year;
    int64_t end_month = b.month;
    int64_t end_day = b.day;
    if (has_time && timeOfDayNanos(b, both_zoned) < timeOfDayNanos(a, both_zoned)) {
        // LocalDate.minusDays(1)
        if (--end_day < 1) {
            if (--end_month < 1) {
                end_month = 12;
                --end_year;
            }
            end_day = daysInMonth(end_year, end_month);
        }
    }
    const int64_t packed_a = packedMonthDay(a.year, a.month, a.day);
    const int64_t packed_b = packedMonthDay(end_year, end_month, end_day);
    const int64_t months = (packed_b - packed_a) / 32; // C++ truncation matches Java's

    const DateTimeValue mid = addMonthsClamped(a, months);
    constexpr int64_t kDayNs = 86'400LL * 1'000'000'000LL;
    int64_t days = daysFromCivil(b.year, b.month, b.day) - daysFromCivil(mid.year, mid.month, mid.day);
    const auto frame = [both_zoned](const DateTimeValue& v) {
        return both_zoned ? temporalToComparable(v) : localFieldsNanos(v);
    };
    // rem_ns 已经扣掉整月/整日，必然小于一天；但减法用 128 位算，避免跨世纪的中间值溢出。
    int64_t rem_ns = static_cast<int64_t>(frame(b) - frame(mid) - static_cast<__int128>(days) * kDayNs);

    // neo4j hands back a day borrowed from the time part when the two disagree in
    // sign -- duration.between(datetime('2024-01-31T10:00:00Z'),
    // datetime('2024-03-01T09:00:00Z')) arrives as 29 days + 23h, not 30 days - 1h
    // (both are the same length). Note this fold belongs to between() only:
    // duration({days: 1, seconds: -3600}) keeps its mixed signs, and
    // toString() renders that as P1DT-1H.
    if (days > 0 && rem_ns < 0) {
        --days;
        rem_ns += kDayNs;
    } else if (days < 0 && rem_ns > 0) {
        ++days;
        rem_ns -= kDayNs;
    }

    DurationValue result;
    result.months = months;
    result.days = days;
    result.seconds = rem_ns / 1'000'000'000LL;
    result.nanos = rem_ns % 1'000'000'000LL;
    normalizeDurationNanos(result);
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

    // 128 位：seconds * 1e9 在 |seconds| > ~9.2e9（公元 2262 年之后）就溢出 int64，
    // 而 date/datetime 的合法范围一直开到 ±999'999'999 年（约 ±3.2e16 秒）。
    const __int128 total_nanos = static_cast<__int128>(seconds) * NANOS_PER_SEC + nanos;

    // Split into days and day-nanos（|days| ≤ 3.7e11，收窄回 int64 安全）
    int64_t days = static_cast<int64_t>(total_nanos / NANOS_PER_DAY);
    int64_t day_ns = static_cast<int64_t>(total_nanos % NANOS_PER_DAY);
    if (day_ns < 0) {
        day_ns += NANOS_PER_DAY;
        days--;
    }

    // Convert days since epoch to year/month/day
    {
        int64_t y = 0, m = 0, d = 0;
        civilFromDays(days, y, m, d);
        // 超出可表示的年份（±999'999'999，与 neo4j 一致）时报错，而不是让收窄成 int32
        // 静默回绕。调用方要么已经做过范围检查（datetime({epochSeconds: ...})），
        // 要么拿到一个明确的 ArgumentError（datetime.fromepoch / Bolt 参数）。
        if (y < -999999999 || y > 999999999)
            throw QueryException(QueryErrorKind::Argument, "datetime out of range");
        result.year = static_cast<int32_t>(y);
        result.month = static_cast<int8_t>(m);
        result.day = static_cast<int8_t>(d);
    }

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

/// ISO-8601 扩展年份（与 neo4j 一致）：
///   0..9999   -> 四位补零（0000、0001、1970、9999）
///   -9999..-1 -> '-' + 四位补零（-0001、-1199）
///   |year| > 9999 -> 显式符号 + 原样位数（+10000、+11476、-100000、-999999999）
/// pad4/ppad2 是按位取数，只能处理 0..9999 的非负数：负年份会算出非数字字符（'/'、''' 之类），
/// 五位以上会被截成低四位。
std::string formatYear(int32_t year) {
    const int64_t abs_year = year < 0 ? -static_cast<int64_t>(year) : year;
    std::string digits = std::to_string(abs_year);
    if (abs_year <= 9999)
        digits.insert(0, 4 - digits.size(), '0');
    if (year < 0)
        return "-" + digits;
    return abs_year <= 9999 ? digits : "+" + digits;
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

namespace {

/// 时间部分：`always_seconds` 为真时秒始终输出（服务端 `toString()`：neo4j 渲染
/// toString(localdatetime('2024-06-15T12:30:00')) 为 '2024-06-15T12:30:00'）；
/// 为假时按 ISO-8601 省略零秒与零小数（各语言驱动渲染 Bolt 时间值的形式，
/// openCypher TCK 的期望文本也是这个约定）。
std::string fmtTimeOfDay(int64_t hour, int64_t minute, int64_t second, int64_t nanos, bool always_seconds) {
    std::string s = pad2(hour) + ":" + pad2(minute);
    if (always_seconds || second != 0 || nanos != 0)
        s += ":" + pad2(second) + fmtSubsecond(nanos);
    return s;
}

std::string fmtDateTime(const DateTimeValue& tv, bool always_seconds) {
    switch (tv.kind) {
    case DateTimeKind::DATE:
        return formatYear(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day);
    case DateTimeKind::LOCAL_DATETIME:
        return formatYear(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day) + "T" +
               fmtTimeOfDay(tv.hour, tv.minute, tv.second, tv.nanos, always_seconds);
    case DateTimeKind::DATETIME:
        return formatYear(tv.year) + "-" + pad2(tv.month) + "-" + pad2(tv.day) + "T" +
               fmtTimeOfDay(tv.hour, tv.minute, tv.second, tv.nanos, always_seconds) +
               fmtTimezone(tv.tz_offset_sec, tzNameOrEmpty(tv.tz_name));
    default:
        return "";
    }
}

std::string fmtTime(const TimeValue& tv, bool always_seconds) {
    std::string s = fmtTimeOfDay(tv.hour, tv.minute, tv.second, tv.nanos, always_seconds);
    if (tv.kind == TimeKind::TIME)
        s += fmtTimezone(tv.tz_offset_sec, tzNameOrEmpty(tv.tz_name));
    return s;
}

} // namespace

std::string temporalToString(const DateTimeValue& tv) {
    return fmtDateTime(tv, /*always_seconds=*/true);
}

std::string temporalToIsoString(const DateTimeValue& tv) {
    return fmtDateTime(tv, /*always_seconds=*/false);
}

std::string temporalToString(const TimeValue& tv) {
    return fmtTime(tv, /*always_seconds=*/true);
}

std::string temporalToIsoString(const TimeValue& tv) {
    return fmtTime(tv, /*always_seconds=*/false);
}

std::string temporalToIsoString(const DurationValue& tv) {
    // duration 的渲染两边一致（Java Duration.toString()）。
    return temporalToString(tv);
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
