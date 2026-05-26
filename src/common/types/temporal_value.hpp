#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace eugraph {

enum class TemporalKind : uint8_t {
    DATE,
    TIME,
    DATETIME,
    LOCAL_TIME,
    LOCAL_DATETIME,
    DURATION
};

enum class TemporalField : uint8_t {
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
    // Time fields
    HOUR,
    MINUTE,
    SECOND,
    NANOSECOND,
    MILLISECOND,
    MICROSECOND,
    // Timezone fields
    TIMEZONE,
    OFFSET,
    OFFSET_MINUTES,
    OFFSET_SECONDS,
    // Epoch fields
    EPOCH_SECONDS,
    EPOCH_MILLIS,
    // Duration fields
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

std::optional<TemporalField> temporalFieldFromString(const std::string& s);

// Returns true if the field returns a String type; otherwise Int64.
bool temporalFieldReturnsString(TemporalField f);

struct TemporalValue {
    TemporalKind kind = TemporalKind::DATE;
    // Date / datetime fields
    int64_t year = 1970;
    int64_t month = 1;
    int64_t day = 1;
    // Time fields
    int64_t hour = 0;
    int64_t minute = 0;
    int64_t second = 0;
    int64_t nanos = 0;         // subsecond precision (0-999999999)
    int32_t tz_offset_min = 0; // timezone offset in minutes (0 for local / UTC)
    std::string tz_name;       // zone name like "Europe/Stockholm" (empty for non-zoned)

    // Duration-specific fields
    int64_t dur_months = 0;
    int64_t dur_days = 0;
    int64_t dur_seconds = 0;
    int64_t dur_nanos = 0;

    bool operator==(const TemporalValue& o) const;
};

bool temporalLess(const TemporalValue& a, const TemporalValue& b);
std::string temporalToString(const TemporalValue& tv);
int64_t temporalToComparable(const TemporalValue& tv);

} // namespace eugraph
