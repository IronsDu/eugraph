#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace eugraph {

enum class DateTimeKind : uint8_t {
    DATE,
    LOCAL_DATETIME,
    DATETIME
};

enum class TimeKind : uint8_t {
    LOCAL_TIME,
    TIME
};

enum class DateTimeField : uint8_t {
    // Date fields
    YEAR,
    QUARTER,
    MONTH,
    DAY,
    WEEK_YEAR,
    WEEK,
    ORDINAL_DAY,
    WEEK_DAY,
    DAY_OF_QUARTER,
    // Time fields (applicable to LOCAL_DATETIME / DATETIME)
    HOUR,
    MINUTE,
    SECOND,
    NANOSECOND,
    MILLISECOND,
    MICROSECOND,
    // Timezone fields (only DATETIME)
    TIMEZONE,
    OFFSET,
    OFFSET_MINUTES,
    OFFSET_SECONDS,
    // Epoch fields (only DATETIME)
    EPOCH_SECONDS,
    EPOCH_MILLIS,
};

enum class TimeField : uint8_t {
    HOUR,
    MINUTE,
    SECOND,
    NANOSECOND,
    MILLISECOND,
    MICROSECOND,
    // Timezone fields (only TIME)
    TIMEZONE,
    OFFSET,
    OFFSET_MINUTES,
    OFFSET_SECONDS,
};

enum class DurationField : uint8_t {
    YEARS,
    QUARTERS,
    MONTHS,
    MONTHS_OF_YEAR,
    MONTHS_OF_QUARTER,
    QUARTERS_OF_YEAR,
    WEEKS,
    DAYS,
    DAYS_OF_WEEK,
    HOURS,
    MINUTES,
    SECONDS,
    MILLISECONDS,
    MICROSECONDS,
    NANOSECONDS,
    MINUTES_OF_HOUR,
    SECONDS_OF_MINUTE,
    MILLISECONDS_OF_SECOND,
    MICROSECONDS_OF_SECOND,
    NANOSECONDS_OF_SECOND,
};

std::optional<DateTimeField> dateTimeFieldFromString(const std::string& s);
std::optional<TimeField> timeFieldFromString(const std::string& s);
std::optional<DurationField> durationFieldFromString(const std::string& s);

bool dateTimeFieldReturnsString(DateTimeField f);
bool timeFieldReturnsString(TimeField f);

// ==================== Date/DateTime type ====================

struct DateTimeValue {
    DateTimeKind kind = DateTimeKind::DATE;
    int64_t year = 1970;
    int64_t month = 1;
    int64_t day = 1;
    int64_t hour = 0;
    int64_t minute = 0;
    int64_t second = 0;
    int64_t nanos = 0;
    int32_t tz_offset_min = 0;
    std::string tz_name;

    bool operator==(const DateTimeValue& o) const;
};

// ==================== Time type ====================

struct TimeValue {
    TimeKind kind = TimeKind::LOCAL_TIME;
    int64_t hour = 0;
    int64_t minute = 0;
    int64_t second = 0;
    int64_t nanos = 0;
    int32_t tz_offset_min = 0;
    std::string tz_name;

    bool operator==(const TimeValue& o) const;
};

// ==================== Duration type ====================

struct DurationValue {
    int64_t months = 0;
    int64_t days = 0;
    int64_t seconds = 0;
    int64_t nanos = 0;

    bool operator==(const DurationValue& o) const;
};

// ==================== String / comparison helpers ====================

std::string temporalToString(const DateTimeValue& tv);
std::string temporalToString(const TimeValue& tv);
std::string temporalToString(const DurationValue& tv);

int64_t temporalToComparable(const DateTimeValue& tv);
int64_t temporalToComparable(const TimeValue& tv);

bool temporalLess(const DateTimeValue& a, const DateTimeValue& b);
bool temporalLess(const TimeValue& a, const TimeValue& b);

// ==================== Calendar helpers ====================

bool isLeapYear(int64_t year);
int64_t daysInMonth(int64_t year, int64_t month);
void normalizeDate(int64_t& year, int64_t& month, int64_t& day);
int64_t daysFromCivil(int64_t y, int64_t m, int64_t d);
void civilFromDays(int64_t days, int64_t& y, int64_t& m, int64_t& d);

// ==================== Temporal arithmetic ====================

DateTimeValue addDuration(const DateTimeValue& temporal, const DurationValue& duration);
TimeValue addDuration(const TimeValue& temporal, const DurationValue& duration);
DateTimeValue subDuration(const DateTimeValue& temporal, const DurationValue& duration);
TimeValue subDuration(const TimeValue& temporal, const DurationValue& duration);

DurationValue subtractDateTimes(const DateTimeValue& a, const DateTimeValue& b);
DurationValue subtractTimes(const TimeValue& a, const TimeValue& b);
DurationValue addDurations(const DurationValue& a, const DurationValue& b);
DurationValue subDurations(const DurationValue& a, const DurationValue& b);
DurationValue mulDuration(const DurationValue& dur, int64_t factor);
DurationValue divDuration(const DurationValue& dur, int64_t divisor);
DurationValue mulDuration(const DurationValue& dur, double factor);
DurationValue divDuration(const DurationValue& dur, double divisor);

DurationValue durationBetween(const DateTimeValue& a, const DateTimeValue& b);
DurationValue durationBetween(const TimeValue& a, const TimeValue& b);

DateTimeValue datetimeFromEpoch(int64_t seconds, int64_t nanos);

} // namespace eugraph
