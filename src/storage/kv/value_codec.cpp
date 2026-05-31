#include "storage/kv/value_codec.hpp"

#include <cstring>
#include <stdexcept>

namespace {

void appendU64(std::string& result, uint64_t v) {
    for (int i = 7; i >= 0; --i)
        result.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}

uint64_t readU64(std::string_view data, size_t& off) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | static_cast<uint8_t>(data[off++]);
    return v;
}

void appendU32(std::string& result, uint32_t v) {
    result.push_back(static_cast<char>((v >> 24) & 0xFF));
    result.push_back(static_cast<char>((v >> 16) & 0xFF));
    result.push_back(static_cast<char>((v >> 8) & 0xFF));
    result.push_back(static_cast<char>(v & 0xFF));
}

uint32_t readU32(std::string_view data, size_t& off) {
    uint32_t v = (static_cast<uint32_t>(static_cast<uint8_t>(data[off])) << 24) |
                 (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 1])) << 16) |
                 (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 2])) << 8) |
                 static_cast<uint32_t>(static_cast<uint8_t>(data[off + 3]));
    off += 4;
    return v;
}

void appendU16(std::string& result, uint16_t v) {
    result.push_back(static_cast<char>((v >> 8) & 0xFF));
    result.push_back(static_cast<char>(v & 0xFF));
}

uint16_t readU16(std::string_view data, size_t& off) {
    uint16_t v = (static_cast<uint16_t>(static_cast<uint8_t>(data[off])) << 8) |
                 static_cast<uint16_t>(static_cast<uint8_t>(data[off + 1]));
    off += 2;
    return v;
}

} // namespace

namespace eugraph {

std::string ValueCodec::encode(const PropertyValue& value) {
    std::string result;

    std::visit(
        [&result](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                result.push_back(static_cast<char>(TAG_NULL));
            } else if constexpr (std::is_same_v<T, bool>) {
                result.push_back(static_cast<char>(TAG_BOOL));
                result.push_back(arg ? '\x01' : '\x00');
            } else if constexpr (std::is_same_v<T, int64_t>) {
                result.push_back(static_cast<char>(TAG_INT64));
                appendU64(result, static_cast<uint64_t>(arg));
            } else if constexpr (std::is_same_v<T, double>) {
                result.push_back(static_cast<char>(TAG_DOUBLE));
                uint64_t bits;
                std::memcpy(&bits, &arg, sizeof(double));
                appendU64(result, bits);
            } else if constexpr (std::is_same_v<T, std::string>) {
                result.push_back(static_cast<char>(TAG_STRING));
                appendU32(result, static_cast<uint32_t>(arg.size()));
                result.append(arg);
            } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                result.push_back(static_cast<char>(TAG_INT64_ARRAY));
                appendU32(result, static_cast<uint32_t>(arg.size()));
                for (int64_t v : arg)
                    appendU64(result, static_cast<uint64_t>(v));
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                result.push_back(static_cast<char>(TAG_DOUBLE_ARRAY));
                appendU32(result, static_cast<uint32_t>(arg.size()));
                for (double d : arg) {
                    uint64_t bits;
                    std::memcpy(&bits, &d, sizeof(double));
                    appendU64(result, bits);
                }
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                result.push_back(static_cast<char>(TAG_STRING_ARRAY));
                appendU32(result, static_cast<uint32_t>(arg.size()));
                for (const auto& s : arg) {
                    appendU32(result, static_cast<uint32_t>(s.size()));
                    result.append(s);
                }
            } else if constexpr (std::is_same_v<T, std::vector<DateTimeValue>>) {
                result.push_back(static_cast<char>(TAG_DATETIME_ARRAY));
                appendU32(result, static_cast<uint32_t>(arg.size()));
                for (const auto& tv : arg) {
                    result.push_back(static_cast<uint8_t>(tv.kind));
                    appendU64(result, static_cast<uint64_t>(tv.year));
                    appendU64(result, static_cast<uint64_t>(tv.month));
                    appendU64(result, static_cast<uint64_t>(tv.day));
                    appendU64(result, static_cast<uint64_t>(tv.hour));
                    appendU64(result, static_cast<uint64_t>(tv.minute));
                    appendU64(result, static_cast<uint64_t>(tv.second));
                    appendU64(result, static_cast<uint64_t>(tv.nanos));
                    appendU32(result, static_cast<uint32_t>(tv.tz_offset_sec));
                    appendU16(result, static_cast<uint16_t>(tv.tz_name.size()));
                    result.append(tv.tz_name);
                }
            } else if constexpr (std::is_same_v<T, std::vector<TimeValue>>) {
                result.push_back(static_cast<char>(TAG_TIME_ARRAY));
                appendU32(result, static_cast<uint32_t>(arg.size()));
                for (const auto& tv : arg) {
                    result.push_back(static_cast<uint8_t>(tv.kind));
                    appendU64(result, static_cast<uint64_t>(tv.hour));
                    appendU64(result, static_cast<uint64_t>(tv.minute));
                    appendU64(result, static_cast<uint64_t>(tv.second));
                    appendU64(result, static_cast<uint64_t>(tv.nanos));
                    appendU32(result, static_cast<uint32_t>(tv.tz_offset_sec));
                    appendU16(result, static_cast<uint16_t>(tv.tz_name.size()));
                    result.append(tv.tz_name);
                }
            } else if constexpr (std::is_same_v<T, std::vector<DurationValue>>) {
                result.push_back(static_cast<char>(TAG_DURATION_ARRAY));
                appendU32(result, static_cast<uint32_t>(arg.size()));
                for (const auto& dv : arg) {
                    appendU64(result, static_cast<uint64_t>(dv.months));
                    appendU64(result, static_cast<uint64_t>(dv.days));
                    appendU64(result, static_cast<uint64_t>(dv.seconds));
                    appendU64(result, static_cast<uint64_t>(dv.nanos));
                }
            } else if constexpr (std::is_same_v<T, DateTimeValue>) {
                result.push_back(static_cast<char>(TAG_DATETIME));
                result.push_back(static_cast<uint8_t>(arg.kind));
                appendU64(result, static_cast<uint64_t>(arg.year));
                appendU64(result, static_cast<uint64_t>(arg.month));
                appendU64(result, static_cast<uint64_t>(arg.day));
                appendU64(result, static_cast<uint64_t>(arg.hour));
                appendU64(result, static_cast<uint64_t>(arg.minute));
                appendU64(result, static_cast<uint64_t>(arg.second));
                appendU64(result, static_cast<uint64_t>(arg.nanos));
                appendU32(result, static_cast<uint32_t>(arg.tz_offset_sec));
                appendU16(result, static_cast<uint16_t>(arg.tz_name.size()));
                result.append(arg.tz_name);
            } else if constexpr (std::is_same_v<T, TimeValue>) {
                result.push_back(static_cast<char>(TAG_TIME));
                result.push_back(static_cast<uint8_t>(arg.kind));
                appendU64(result, static_cast<uint64_t>(arg.hour));
                appendU64(result, static_cast<uint64_t>(arg.minute));
                appendU64(result, static_cast<uint64_t>(arg.second));
                appendU64(result, static_cast<uint64_t>(arg.nanos));
                appendU32(result, static_cast<uint32_t>(arg.tz_offset_sec));
                appendU16(result, static_cast<uint16_t>(arg.tz_name.size()));
                result.append(arg.tz_name);
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                result.push_back(static_cast<char>(TAG_DURATION));
                appendU64(result, static_cast<uint64_t>(arg.months));
                appendU64(result, static_cast<uint64_t>(arg.days));
                appendU64(result, static_cast<uint64_t>(arg.seconds));
                appendU64(result, static_cast<uint64_t>(arg.nanos));
            }
        },
        value);

    return result;
}

PropertyValue ValueCodec::decode(std::string_view data) {
    if (data.empty()) {
        return std::monostate{};
    }

    uint8_t tag = static_cast<uint8_t>(data[0]);
    size_t off = 1;

    switch (tag) {
    case TAG_NULL:
        return std::monostate{};

    case TAG_BOOL:
        return static_cast<bool>(data[off]);

    case TAG_INT64:
        return static_cast<int64_t>(readU64(data, off));

    case TAG_DOUBLE: {
        uint64_t bits = readU64(data, off);
        double d;
        std::memcpy(&d, &bits, sizeof(double));
        return d;
    }

    case TAG_STRING: {
        uint32_t len = readU32(data, off);
        return std::string(data.substr(off, len));
    }

    case TAG_INT64_ARRAY: {
        uint32_t count = readU32(data, off);
        std::vector<int64_t> arr(count);
        for (uint32_t i = 0; i < count; ++i)
            arr[i] = static_cast<int64_t>(readU64(data, off));
        return arr;
    }

    case TAG_DOUBLE_ARRAY: {
        uint32_t count = readU32(data, off);
        std::vector<double> arr(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t bits = readU64(data, off);
            std::memcpy(&arr[i], &bits, sizeof(double));
        }
        return arr;
    }

    case TAG_STRING_ARRAY: {
        uint32_t count = readU32(data, off);
        std::vector<std::string> arr(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t len = readU32(data, off);
            arr[i] = std::string(data.substr(off, len));
            off += len;
        }
        return arr;
    }

    case TAG_DATETIME: {
        DateTimeValue tv;
        tv.kind = static_cast<DateTimeKind>(static_cast<uint8_t>(data[off++]));
        tv.year = static_cast<int64_t>(readU64(data, off));
        tv.month = static_cast<int64_t>(readU64(data, off));
        tv.day = static_cast<int64_t>(readU64(data, off));
        tv.hour = static_cast<int64_t>(readU64(data, off));
        tv.minute = static_cast<int64_t>(readU64(data, off));
        tv.second = static_cast<int64_t>(readU64(data, off));
        tv.nanos = static_cast<int64_t>(readU64(data, off));
        tv.tz_offset_sec = static_cast<int32_t>(readU32(data, off));
        uint16_t tz_len = readU16(data, off);
        tv.tz_name = std::string(data.substr(off, tz_len));
        return tv;
    }

    case TAG_TIME: {
        TimeValue tv;
        tv.kind = static_cast<TimeKind>(static_cast<uint8_t>(data[off++]));
        tv.hour = static_cast<int64_t>(readU64(data, off));
        tv.minute = static_cast<int64_t>(readU64(data, off));
        tv.second = static_cast<int64_t>(readU64(data, off));
        tv.nanos = static_cast<int64_t>(readU64(data, off));
        tv.tz_offset_sec = static_cast<int32_t>(readU32(data, off));
        uint16_t tz_len = readU16(data, off);
        tv.tz_name = std::string(data.substr(off, tz_len));
        return tv;
    }

    case TAG_DURATION: {
        DurationValue tv;
        tv.months = static_cast<int64_t>(readU64(data, off));
        tv.days = static_cast<int64_t>(readU64(data, off));
        tv.seconds = static_cast<int64_t>(readU64(data, off));
        tv.nanos = static_cast<int64_t>(readU64(data, off));
        return tv;
    }

    case TAG_DATETIME_ARRAY: {
        uint32_t count = readU32(data, off);
        std::vector<DateTimeValue> arr(count);
        for (uint32_t i = 0; i < count; ++i) {
            arr[i].kind = static_cast<DateTimeKind>(static_cast<uint8_t>(data[off++]));
            arr[i].year = static_cast<int64_t>(readU64(data, off));
            arr[i].month = static_cast<int64_t>(readU64(data, off));
            arr[i].day = static_cast<int64_t>(readU64(data, off));
            arr[i].hour = static_cast<int64_t>(readU64(data, off));
            arr[i].minute = static_cast<int64_t>(readU64(data, off));
            arr[i].second = static_cast<int64_t>(readU64(data, off));
            arr[i].nanos = static_cast<int64_t>(readU64(data, off));
            arr[i].tz_offset_sec = static_cast<int32_t>(readU32(data, off));
            uint16_t tz_len = readU16(data, off);
            arr[i].tz_name = std::string(data.substr(off, tz_len));
            off += tz_len;
        }
        return arr;
    }

    case TAG_TIME_ARRAY: {
        uint32_t count = readU32(data, off);
        std::vector<TimeValue> arr(count);
        for (uint32_t i = 0; i < count; ++i) {
            arr[i].kind = static_cast<TimeKind>(static_cast<uint8_t>(data[off++]));
            arr[i].hour = static_cast<int64_t>(readU64(data, off));
            arr[i].minute = static_cast<int64_t>(readU64(data, off));
            arr[i].second = static_cast<int64_t>(readU64(data, off));
            arr[i].nanos = static_cast<int64_t>(readU64(data, off));
            arr[i].tz_offset_sec = static_cast<int32_t>(readU32(data, off));
            uint16_t tz_len = readU16(data, off);
            arr[i].tz_name = std::string(data.substr(off, tz_len));
            off += tz_len;
        }
        return arr;
    }

    case TAG_DURATION_ARRAY: {
        uint32_t count = readU32(data, off);
        std::vector<DurationValue> arr(count);
        for (uint32_t i = 0; i < count; ++i) {
            arr[i].months = static_cast<int64_t>(readU64(data, off));
            arr[i].days = static_cast<int64_t>(readU64(data, off));
            arr[i].seconds = static_cast<int64_t>(readU64(data, off));
            arr[i].nanos = static_cast<int64_t>(readU64(data, off));
        }
        return arr;
    }

    default:
        throw std::runtime_error("Unknown value type tag: " + std::to_string(tag));
    }
}

std::string ValueCodec::encodeU64(uint64_t val) {
    std::string result(8, '\0');
    for (int i = 7; i >= 0; --i) {
        result[7 - i] = static_cast<char>((val >> (i * 8)) & 0xFF);
    }
    return result;
}

uint64_t ValueCodec::decodeU64(std::string_view data) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | static_cast<uint8_t>(data[i]);
    }
    return val;
}

std::string ValueCodec::encodeEdgeAdjacency(VertexId src_id, VertexId dst_id, uint64_t seq, EdgeLabelId label_id) {
    std::string result(26, '\0');
    size_t off = 0;
    for (int i = 7; i >= 0; --i)
        result[off++] = static_cast<char>((src_id >> (i * 8)) & 0xFF);
    for (int i = 7; i >= 0; --i)
        result[off++] = static_cast<char>((dst_id >> (i * 8)) & 0xFF);
    for (int i = 7; i >= 0; --i)
        result[off++] = static_cast<char>((seq >> (i * 8)) & 0xFF);
    for (int i = 1; i >= 0; --i)
        result[off++] = static_cast<char>((label_id >> (i * 8)) & 0xFF);
    return result;
}

void ValueCodec::decodeEdgeAdjacency(std::string_view data, VertexId& src_id, VertexId& dst_id, uint64_t& seq,
                                     EdgeLabelId& label_id) {
    src_id = 0;
    dst_id = 0;
    seq = 0;
    label_id = 0;
    if (data.size() < 26)
        return;
    size_t off = 0;
    for (int i = 0; i < 8; ++i)
        src_id = (src_id << 8) | static_cast<uint8_t>(data[off++]);
    for (int i = 0; i < 8; ++i)
        dst_id = (dst_id << 8) | static_cast<uint8_t>(data[off++]);
    for (int i = 0; i < 8; ++i)
        seq = (seq << 8) | static_cast<uint8_t>(data[off++]);
    for (int i = 0; i < 2; ++i)
        label_id = (label_id << 8) | static_cast<uint8_t>(data[off++]);
}

} // namespace eugraph
