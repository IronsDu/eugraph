#include "bolt/packstream/decoder.hpp"

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace eugraph {
namespace bolt {
namespace packstream {

uint8_t Decoder::readByte() {
    if (pos_ >= end_pos_)
        throw DecodeError("unexpected end of data");
    return data_[pos_++];
}

static uint64_t readUintBigEndian(const uint8_t* data, size_t bytes) {
    uint64_t result = 0;
    for (size_t i = 0; i < bytes; i++) {
        result = (result << 8) | data[i];
    }
    return result;
}

Value Decoder::decode() {
    if (pos_ >= end_pos_)
        throw DecodeError("unexpected end of data");

    uint8_t marker = readByte();

    // Null
    if (marker == marker::NULL_VAL)
        return std::monostate{};

    // Boolean
    if (marker == marker::FALSE)
        return false;
    if (marker == marker::TRUE)
        return true;

    // Positive tiny int: 0x00-0x7F
    if (marker <= 0x7F)
        return static_cast<int64_t>(marker);

    // Negative tiny int: 0xF0-0xFF
    if (marker >= 0xF0)
        return static_cast<int64_t>(static_cast<int8_t>(marker));

    // INT_8
    if (marker == marker::INT_8)
        return static_cast<int64_t>(static_cast<int8_t>(readByte()));

    // INT_16
    if (marker == marker::INT_16) {
        uint8_t buf[2] = {readByte(), readByte()};
        return static_cast<int64_t>(static_cast<int16_t>(readUintBigEndian(buf, 2)));
    }

    // INT_32
    if (marker == marker::INT_32) {
        uint8_t buf[4];
        for (auto& b : buf)
            b = readByte();
        return static_cast<int64_t>(static_cast<int32_t>(readUintBigEndian(buf, 4)));
    }

    // INT_64
    if (marker == marker::INT_64) {
        uint8_t buf[8];
        for (auto& b : buf)
            b = readByte();
        uint64_t uv = readUintBigEndian(buf, 8);
        int64_t sv;
        std::memcpy(&sv, &uv, sizeof(sv));
        return sv;
    }

    // Float
    if (marker == marker::FLOAT_64) {
        uint8_t buf[8];
        for (auto& b : buf)
            b = readByte();
        uint64_t bits = readUintBigEndian(buf, 8);
        double d;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }

    // Tiny string
    if ((marker & 0xF0) == marker::TINY_STRING_BASE) {
        size_t len = marker & 0x0F;
        if (pos_ + len > end_pos_)
            throw DecodeError("string: unexpected end of data");
        std::string result(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return result;
    }

    // String 8/16/32
    if (marker == marker::STRING_8 || marker == marker::STRING_16 || marker == marker::STRING_32) {
        size_t len;
        if (marker == marker::STRING_8) {
            len = readByte();
        } else if (marker == marker::STRING_16) {
            uint8_t buf[2] = {readByte(), readByte()};
            len = readUintBigEndian(buf, 2);
        } else {
            uint8_t buf[4];
            for (auto& b : buf)
                b = readByte();
            len = readUintBigEndian(buf, 4);
        }
        if (pos_ + len > end_pos_)
            throw DecodeError("string: unexpected end of data");
        std::string result(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return result;
    }

    // Bytes
    if (marker == marker::BYTES_8 || marker == marker::BYTES_16 || marker == marker::BYTES_32) {
        size_t len;
        if (marker == marker::BYTES_8) {
            len = readByte();
        } else if (marker == marker::BYTES_16) {
            uint8_t buf[2] = {readByte(), readByte()};
            len = readUintBigEndian(buf, 2);
        } else {
            uint8_t buf[4];
            for (auto& b : buf)
                b = readByte();
            len = readUintBigEndian(buf, 4);
        }
        if (pos_ + len > end_pos_)
            throw DecodeError("bytes: unexpected end of data");
        std::vector<uint8_t> result(data_ + pos_, data_ + pos_ + len);
        pos_ += len;
        return result;
    }

    // List
    auto decodeList = [&](size_t len) -> Value {
        std::vector<PackStreamValueStorage> list;
        list.reserve(len);
        for (size_t i = 0; i < len; i++)
            list.push_back({decode()});
        return list;
    };

    if ((marker & 0xF0) == marker::TINY_LIST_BASE)
        return decodeList(marker & 0x0F);

    if (marker == marker::LIST_8)
        return decodeList(readByte());
    if (marker == marker::LIST_16) {
        uint8_t buf[2] = {readByte(), readByte()};
        return decodeList(readUintBigEndian(buf, 2));
    }
    if (marker == marker::LIST_32) {
        uint8_t buf[4];
        for (auto& b : buf)
            b = readByte();
        return decodeList(readUintBigEndian(buf, 4));
    }

    // Dictionary
    auto decodeDict = [&](size_t len) -> Value {
        std::unordered_map<std::string, PackStreamValueStorage> dict;
        for (size_t i = 0; i < len; i++) {
            auto key_val = decode();
            if (!std::holds_alternative<std::string>(key_val))
                throw DecodeError("dictionary key must be a string");
            auto key = std::move(std::get<std::string>(key_val));
            dict.emplace(std::move(key), PackStreamValueStorage{decode()});
        }
        return dict;
    };

    if ((marker & 0xF0) == marker::TINY_DICT_BASE)
        return decodeDict(marker & 0x0F);
    if (marker == marker::DICT_8)
        return decodeDict(readByte());
    if (marker == marker::DICT_16) {
        uint8_t buf[2] = {readByte(), readByte()};
        return decodeDict(readUintBigEndian(buf, 2));
    }
    if (marker == marker::DICT_32) {
        uint8_t buf[4];
        for (auto& b : buf)
            b = readByte();
        return decodeDict(readUintBigEndian(buf, 4));
    }

    // Structure
    size_t field_count;
    if ((marker & 0xF0) == marker::TINY_STRUCT_BASE) {
        field_count = marker & 0x0F;
    } else if (marker == marker::STRUCT_8) {
        field_count = readByte();
    } else if (marker == marker::STRUCT_16) {
        uint8_t buf[2] = {readByte(), readByte()};
        field_count = readUintBigEndian(buf, 2);
    } else {
        std::ostringstream oss;
        oss << "unknown packstream marker: 0x" << std::hex << static_cast<int>(marker);
        throw DecodeError(oss.str());
    }

    uint8_t tag = readByte();
    PackStreamStruct s;
    s.tag = tag;
    for (size_t i = 0; i < field_count; i++)
        s.fields.push_back({decode()});
    return s;
}

std::pair<uint8_t, size_t> Decoder::decodeStructHeader() {
    uint8_t marker = readByte();
    size_t field_count;

    if ((marker & 0xF0) == marker::TINY_STRUCT_BASE) {
        field_count = marker & 0x0F;
    } else if (marker == marker::STRUCT_8) {
        field_count = readByte();
    } else if (marker == marker::STRUCT_16) {
        uint8_t buf[2] = {readByte(), readByte()};
        field_count = readUintBigEndian(buf, 2);
    } else {
        std::ostringstream oss;
        oss << "expected struct marker, got 0x" << std::hex << static_cast<int>(marker);
        throw DecodeError(oss.str());
    }

    uint8_t tag = readByte();
    return {tag, field_count};
}

void Decoder::skip() {
    decode();
}

} // namespace packstream
} // namespace bolt
} // namespace eugraph
