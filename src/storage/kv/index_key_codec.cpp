#include "storage/kv/index_key_codec.hpp"

#include <cmath>
#include <cstring>

namespace eugraph {

// Encoding scheme for sortable index keys:
//   null:    1 byte  = 0x00
//   bool:    2 bytes = 0x01 + (0x00=false / 0x01=true)
//   int64:   9 bytes = 0x02 + sign-flipped big-endian
//   double:  9 bytes = 0x03 + IEEE sign-flipped big-endian
//   string:  1+N     = 0x04 + raw bytes

std::string IndexKeyCodec::encodeSortableValue(const PropertyValue& value) {
    std::string buf;
    buf.reserve(16);

    std::visit(
        [&buf](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                buf.push_back(static_cast<char>(0x00));
            } else if constexpr (std::is_same_v<T, bool>) {
                buf.push_back(static_cast<char>(0x01));
                buf.push_back(static_cast<char>(arg ? 1 : 0));
            } else if constexpr (std::is_same_v<T, int64_t>) {
                buf.push_back(static_cast<char>(0x02));
                uint64_t flipped = static_cast<uint64_t>(arg) ^ 0x8000000000000000ULL;
                for (int i = 7; i >= 0; --i)
                    buf.push_back(static_cast<char>((flipped >> (i * 8)) & 0xFF));
            } else if constexpr (std::is_same_v<T, double>) {
                buf.push_back(static_cast<char>(0x03));
                uint64_t bits;
                std::memcpy(&bits, &arg, sizeof(double));
                if (bits & 0x8000000000000000ULL) {
                    bits = ~bits;
                } else {
                    bits ^= 0x8000000000000000ULL;
                }
                for (int i = 7; i >= 0; --i)
                    buf.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
            } else if constexpr (std::is_same_v<T, std::string>) {
                buf.push_back(static_cast<char>(0x04));
                buf.append(arg);
            } else {
                // array types not indexable
                buf.push_back(static_cast<char>(0xFE));
            }
        },
        value);

    return buf;
}

std::string IndexKeyCodec::encodeIndexKey(const PropertyValue& value, uint64_t entity_id) {
    auto buf = encodeSortableValue(value);
    for (int i = 7; i >= 0; --i)
        buf.push_back(static_cast<char>((entity_id >> (i * 8)) & 0xFF));
    return buf;
}

uint64_t IndexKeyCodec::decodeEntityId(std::string_view key) {
    if (key.size() < 8)
        return 0;
    uint64_t id = 0;
    size_t off = key.size() - 8;
    for (int i = 0; i < 8; ++i)
        id = (id << 8) | static_cast<uint8_t>(key[off + i]);
    return id;
}

std::string IndexKeyCodec::encodeEqualityPrefix(const PropertyValue& value) {
    return encodeSortableValue(value);
}

} // namespace eugraph
