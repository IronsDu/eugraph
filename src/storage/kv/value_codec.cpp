#include "storage/kv/value_codec.hpp"

#include <cstring>
#include <stdexcept>

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
                uint64_t v = static_cast<uint64_t>(arg);
                for (int i = 7; i >= 0; --i) {
                    result.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
                }
            } else if constexpr (std::is_same_v<T, double>) {
                result.push_back(static_cast<char>(TAG_DOUBLE));
                uint64_t bits;
                std::memcpy(&bits, &arg, sizeof(double));
                for (int i = 7; i >= 0; --i) {
                    result.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                result.push_back(static_cast<char>(TAG_STRING));
                uint32_t len = static_cast<uint32_t>(arg.size());
                result.push_back(static_cast<char>((len >> 24) & 0xFF));
                result.push_back(static_cast<char>((len >> 16) & 0xFF));
                result.push_back(static_cast<char>((len >> 8) & 0xFF));
                result.push_back(static_cast<char>(len & 0xFF));
                result.append(arg);
            } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                result.push_back(static_cast<char>(TAG_INT64_ARRAY));
                uint32_t count = static_cast<uint32_t>(arg.size());
                result.push_back(static_cast<char>((count >> 24) & 0xFF));
                result.push_back(static_cast<char>((count >> 16) & 0xFF));
                result.push_back(static_cast<char>((count >> 8) & 0xFF));
                result.push_back(static_cast<char>(count & 0xFF));
                for (int64_t v : arg) {
                    uint64_t uv = static_cast<uint64_t>(v);
                    for (int i = 7; i >= 0; --i) {
                        result.push_back(static_cast<char>((uv >> (i * 8)) & 0xFF));
                    }
                }
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                result.push_back(static_cast<char>(TAG_DOUBLE_ARRAY));
                uint32_t count = static_cast<uint32_t>(arg.size());
                result.push_back(static_cast<char>((count >> 24) & 0xFF));
                result.push_back(static_cast<char>((count >> 16) & 0xFF));
                result.push_back(static_cast<char>((count >> 8) & 0xFF));
                result.push_back(static_cast<char>(count & 0xFF));
                for (double d : arg) {
                    uint64_t bits;
                    std::memcpy(&bits, &d, sizeof(double));
                    for (int i = 7; i >= 0; --i) {
                        result.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
                    }
                }
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                result.push_back(static_cast<char>(TAG_STRING_ARRAY));
                uint32_t count = static_cast<uint32_t>(arg.size());
                result.push_back(static_cast<char>((count >> 24) & 0xFF));
                result.push_back(static_cast<char>((count >> 16) & 0xFF));
                result.push_back(static_cast<char>((count >> 8) & 0xFF));
                result.push_back(static_cast<char>(count & 0xFF));
                for (const auto& s : arg) {
                    uint32_t len = static_cast<uint32_t>(s.size());
                    result.push_back(static_cast<char>((len >> 24) & 0xFF));
                    result.push_back(static_cast<char>((len >> 16) & 0xFF));
                    result.push_back(static_cast<char>((len >> 8) & 0xFF));
                    result.push_back(static_cast<char>(len & 0xFF));
                    result.append(s);
                }
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

    case TAG_INT64: {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | static_cast<uint8_t>(data[off + i]);
        }
        return static_cast<int64_t>(v);
    }

    case TAG_DOUBLE: {
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            bits = (bits << 8) | static_cast<uint8_t>(data[off + i]);
        }
        double d;
        std::memcpy(&d, &bits, sizeof(double));
        return d;
    }

    case TAG_STRING: {
        uint32_t len = (static_cast<uint32_t>(static_cast<uint8_t>(data[off])) << 24) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 1])) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 2])) << 8) |
                       static_cast<uint32_t>(static_cast<uint8_t>(data[off + 3]));
        off += 4;
        return std::string(data.substr(off, len));
    }

    case TAG_INT64_ARRAY: {
        uint32_t count = (static_cast<uint32_t>(static_cast<uint8_t>(data[off])) << 24) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 1])) << 16) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 2])) << 8) |
                         static_cast<uint32_t>(static_cast<uint8_t>(data[off + 3]));
        off += 4;
        std::vector<int64_t> arr(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t v = 0;
            for (int j = 0; j < 8; ++j) {
                v = (v << 8) | static_cast<uint8_t>(data[off + j]);
            }
            arr[i] = static_cast<int64_t>(v);
            off += 8;
        }
        return arr;
    }

    case TAG_DOUBLE_ARRAY: {
        uint32_t count = (static_cast<uint32_t>(static_cast<uint8_t>(data[off])) << 24) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 1])) << 16) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 2])) << 8) |
                         static_cast<uint32_t>(static_cast<uint8_t>(data[off + 3]));
        off += 4;
        std::vector<double> arr(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t bits = 0;
            for (int j = 0; j < 8; ++j) {
                bits = (bits << 8) | static_cast<uint8_t>(data[off + j]);
            }
            std::memcpy(&arr[i], &bits, sizeof(double));
            off += 8;
        }
        return arr;
    }

    case TAG_STRING_ARRAY: {
        uint32_t count = (static_cast<uint32_t>(static_cast<uint8_t>(data[off])) << 24) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 1])) << 16) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 2])) << 8) |
                         static_cast<uint32_t>(static_cast<uint8_t>(data[off + 3]));
        off += 4;
        std::vector<std::string> arr(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t len = (static_cast<uint32_t>(static_cast<uint8_t>(data[off])) << 24) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 1])) << 16) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(data[off + 2])) << 8) |
                           static_cast<uint32_t>(static_cast<uint8_t>(data[off + 3]));
            off += 4;
            arr[i] = std::string(data.substr(off, len));
            off += len;
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

} // namespace eugraph
