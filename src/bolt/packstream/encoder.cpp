#include "bolt/packstream/encoder.hpp"

#include <cstring>
#include <stdexcept>

namespace eugraph {
namespace bolt {
namespace packstream {

void Encoder::reset() {
    buf_.clear();
}

std::vector<uint8_t> Encoder::release() {
    std::vector<uint8_t> result;
    result.swap(buf_);
    return result;
}

void Encoder::writeByte(uint8_t b) {
    buf_.push_back(b);
}

void Encoder::writeNull() {
    writeByte(marker::NULL_VAL);
}

void Encoder::writeBoolean(bool v) {
    writeByte(v ? marker::TRUE : marker::FALSE);
}

void Encoder::writeInt(int64_t v) {
    if (v >= marker::TINY_INT_POS_MIN && v <= marker::TINY_INT_POS_MAX) {
        // Positive tiny int: stored directly as the value
        writeByte(static_cast<uint8_t>(v));
    } else if (v >= marker::TINY_INT_MIN && v <= marker::TINY_INT_NEG_MAX) {
        // Negative tiny int: stored as marker::TINY_INT_NEG_BASE + (-v - 1)
        // but more practically: value stored in low nibble
        // Actually Bolt uses: v in [-16,-1] stored as 0xF0 | (0x10 + v)
        // Wait: -16 → 0xF0, -1 → 0xFF
        // So: writeByte(static_cast<uint8_t>(v)) works because int64_t(-1) cast to uint8_t gives 0xFF
        // Actually no: casting int64_t(-16) to uint8_t gives 0xF0? Let me think...
        // int64_t(-16) in two's complement: 0xFFFFFFFFFFFFFFF0
        // Cast to uint8_t: 0xF0. Yes, this works.
        // int64_t(-1) → 0xFFFFFFFFFFFFFFFF → uint8_t(0xFF). Yes.
        writeByte(static_cast<uint8_t>(v));
    } else if (v >= INT8_MIN && v <= INT8_MAX) {
        writeByte(marker::INT_8);
        writeByte(static_cast<uint8_t>(static_cast<int8_t>(v)));
    } else if (v >= INT16_MIN && v <= INT16_MAX) {
        writeByte(marker::INT_16);
        int16_t iv = static_cast<int16_t>(v);
        uint8_t bytes[2];
        bytes[0] = static_cast<uint8_t>((iv >> 8) & 0xFF);
        bytes[1] = static_cast<uint8_t>(iv & 0xFF);
        buf_.insert(buf_.end(), bytes, bytes + 2);
    } else if (v >= INT32_MIN && v <= INT32_MAX) {
        writeByte(marker::INT_32);
        int32_t iv = static_cast<int32_t>(v);
        uint8_t bytes[4];
        bytes[0] = static_cast<uint8_t>((iv >> 24) & 0xFF);
        bytes[1] = static_cast<uint8_t>((iv >> 16) & 0xFF);
        bytes[2] = static_cast<uint8_t>((iv >> 8) & 0xFF);
        bytes[3] = static_cast<uint8_t>(iv & 0xFF);
        buf_.insert(buf_.end(), bytes, bytes + 4);
    } else {
        writeByte(marker::INT_64);
        uint8_t bytes[8];
        bytes[0] = static_cast<uint8_t>((v >> 56) & 0xFF);
        bytes[1] = static_cast<uint8_t>((v >> 48) & 0xFF);
        bytes[2] = static_cast<uint8_t>((v >> 40) & 0xFF);
        bytes[3] = static_cast<uint8_t>((v >> 32) & 0xFF);
        bytes[4] = static_cast<uint8_t>((v >> 24) & 0xFF);
        bytes[5] = static_cast<uint8_t>((v >> 16) & 0xFF);
        bytes[6] = static_cast<uint8_t>((v >> 8) & 0xFF);
        bytes[7] = static_cast<uint8_t>(v & 0xFF);
        buf_.insert(buf_.end(), bytes, bytes + 8);
    }
}

void Encoder::writeFloat(double v) {
    writeByte(marker::FLOAT_64);
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    uint8_t bytes[8];
    bytes[0] = static_cast<uint8_t>((bits >> 56) & 0xFF);
    bytes[1] = static_cast<uint8_t>((bits >> 48) & 0xFF);
    bytes[2] = static_cast<uint8_t>((bits >> 40) & 0xFF);
    bytes[3] = static_cast<uint8_t>((bits >> 32) & 0xFF);
    bytes[4] = static_cast<uint8_t>((bits >> 24) & 0xFF);
    bytes[5] = static_cast<uint8_t>((bits >> 16) & 0xFF);
    bytes[6] = static_cast<uint8_t>((bits >> 8) & 0xFF);
    bytes[7] = static_cast<uint8_t>(bits & 0xFF);
    buf_.insert(buf_.end(), bytes, bytes + 8);
}

void Encoder::writeString(const std::string& v) {
    size_t len = v.size();
    if (len <= marker::TINY_STRING_MAX_LEN) {
        writeByte(marker::TINY_STRING_BASE | static_cast<uint8_t>(len));
    } else if (len <= UINT8_MAX) {
        writeByte(marker::STRING_8);
        writeByte(static_cast<uint8_t>(len));
    } else if (len <= UINT16_MAX) {
        writeByte(marker::STRING_16);
        uint16_t ul = static_cast<uint16_t>(len);
        writeByte(static_cast<uint8_t>((ul >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(ul & 0xFF));
    } else {
        writeByte(marker::STRING_32);
        uint32_t ul = static_cast<uint32_t>(len);
        writeByte(static_cast<uint8_t>((ul >> 24) & 0xFF));
        writeByte(static_cast<uint8_t>((ul >> 16) & 0xFF));
        writeByte(static_cast<uint8_t>((ul >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(ul & 0xFF));
    }
    buf_.insert(buf_.end(), v.begin(), v.end());
}

void Encoder::writeBytes(const std::vector<uint8_t>& v) {
    size_t len = v.size();
    if (len <= UINT8_MAX) {
        writeByte(marker::BYTES_8);
        writeByte(static_cast<uint8_t>(len));
    } else if (len <= UINT16_MAX) {
        writeByte(marker::BYTES_16);
        uint16_t ul = static_cast<uint16_t>(len);
        writeByte(static_cast<uint8_t>((ul >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(ul & 0xFF));
    } else {
        writeByte(marker::BYTES_32);
        uint32_t ul = static_cast<uint32_t>(len);
        writeByte(static_cast<uint8_t>((ul >> 24) & 0xFF));
        writeByte(static_cast<uint8_t>((ul >> 16) & 0xFF));
        writeByte(static_cast<uint8_t>((ul >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(ul & 0xFF));
    }
    buf_.insert(buf_.end(), v.begin(), v.end());
}

void Encoder::writeListHeader(size_t count) {
    if (count <= marker::TINY_LIST_MAX_LEN) {
        writeByte(marker::TINY_LIST_BASE | static_cast<uint8_t>(count));
    } else if (count <= UINT8_MAX) {
        writeByte(marker::LIST_8);
        writeByte(static_cast<uint8_t>(count));
    } else if (count <= UINT16_MAX) {
        writeByte(marker::LIST_16);
        uint16_t uc = static_cast<uint16_t>(count);
        writeByte(static_cast<uint8_t>((uc >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(uc & 0xFF));
    } else {
        writeByte(marker::LIST_32);
        uint32_t uc = static_cast<uint32_t>(count);
        writeByte(static_cast<uint8_t>((uc >> 24) & 0xFF));
        writeByte(static_cast<uint8_t>((uc >> 16) & 0xFF));
        writeByte(static_cast<uint8_t>((uc >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(uc & 0xFF));
    }
}

void Encoder::writeDictHeader(size_t count) {
    if (count <= marker::TINY_DICT_MAX_LEN) {
        writeByte(marker::TINY_DICT_BASE | static_cast<uint8_t>(count));
    } else if (count <= UINT8_MAX) {
        writeByte(marker::DICT_8);
        writeByte(static_cast<uint8_t>(count));
    } else if (count <= UINT16_MAX) {
        writeByte(marker::DICT_16);
        uint16_t uc = static_cast<uint16_t>(count);
        writeByte(static_cast<uint8_t>((uc >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(uc & 0xFF));
    } else {
        writeByte(marker::DICT_32);
        uint32_t uc = static_cast<uint32_t>(count);
        writeByte(static_cast<uint8_t>((uc >> 24) & 0xFF));
        writeByte(static_cast<uint8_t>((uc >> 16) & 0xFF));
        writeByte(static_cast<uint8_t>((uc >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(uc & 0xFF));
    }
}

void Encoder::writeStructHeader(uint8_t tag, size_t field_count) {
    if (field_count <= marker::TINY_STRUCT_MAX_LEN) {
        writeByte(marker::TINY_STRUCT_BASE | static_cast<uint8_t>(field_count));
    } else if (field_count <= UINT8_MAX) {
        writeByte(marker::STRUCT_8);
        writeByte(static_cast<uint8_t>(field_count));
    } else if (field_count <= UINT16_MAX) {
        writeByte(marker::STRUCT_16);
        uint16_t uc = static_cast<uint16_t>(field_count);
        writeByte(static_cast<uint8_t>((uc >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(uc & 0xFF));
    } else {
        writeByte(marker::STRUCT_32);
        uint32_t uc = static_cast<uint32_t>(field_count);
        writeByte(static_cast<uint8_t>((uc >> 24) & 0xFF));
        writeByte(static_cast<uint8_t>((uc >> 16) & 0xFF));
        writeByte(static_cast<uint8_t>((uc >> 8) & 0xFF));
        writeByte(static_cast<uint8_t>(uc & 0xFF));
    }
    writeByte(tag);
}

void Encoder::writeValue(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) {
        writeNull();
    } else if (std::holds_alternative<bool>(v)) {
        writeBoolean(std::get<bool>(v));
    } else if (std::holds_alternative<int64_t>(v)) {
        writeInt(std::get<int64_t>(v));
    } else if (std::holds_alternative<double>(v)) {
        writeFloat(std::get<double>(v));
    } else if (std::holds_alternative<std::string>(v)) {
        writeString(std::get<std::string>(v));
    } else if (std::holds_alternative<std::vector<uint8_t>>(v)) {
        writeBytes(std::get<std::vector<uint8_t>>(v));
    } else if (std::holds_alternative<std::vector<PackStreamValueStorage>>(v)) {
        auto& list = std::get<std::vector<PackStreamValueStorage>>(v);
        writeListHeader(list.size());
        for (auto& elem : list)
            writeValue(elem.value);
    } else if (std::holds_alternative<std::unordered_map<std::string, PackStreamValueStorage>>(v)) {
        auto& dict = std::get<std::unordered_map<std::string, PackStreamValueStorage>>(v);
        writeDictHeader(dict.size());
        for (auto& [key, val] : dict) {
            writeString(key);
            writeValue(val.value);
        }
    } else if (std::holds_alternative<PackStreamStruct>(v)) {
        auto& s = std::get<PackStreamStruct>(v);
        writeStructHeader(s.tag, s.fields.size());
        for (auto& field : s.fields)
            writeValue(field.value);
    }
}

} // namespace packstream
} // namespace bolt
} // namespace eugraph
