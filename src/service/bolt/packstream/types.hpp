#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eugraph {
namespace service {
namespace bolt {
namespace packstream {

struct PackStreamValueStorage;

struct PackStreamStruct {
    uint8_t tag;
    std::vector<PackStreamValueStorage> fields;
};

struct PackStreamValueStorage {
    std::variant<std::monostate,                                          // Null
                 bool,                                                    // Boolean
                 int64_t,                                                 // Integer
                 double,                                                  // Float
                 std::string,                                             // String
                 std::vector<uint8_t>,                                    // Bytes
                 std::vector<PackStreamValueStorage>,                     // List
                 std::unordered_map<std::string, PackStreamValueStorage>, // Dictionary
                 PackStreamStruct>                                        // Structure
        value;
};

using Value = decltype(PackStreamValueStorage::value);

// Bolt protocol marker bytes
namespace marker {
constexpr uint8_t NULL_VAL = 0xC0;
constexpr uint8_t FALSE = 0xC2;
constexpr uint8_t TRUE = 0xC3;

// Integer ranges
constexpr int64_t TINY_INT_MIN = -16;
constexpr int64_t TINY_INT_NEG_MAX = -1;
constexpr int64_t TINY_INT_POS_MIN = 0;
constexpr int64_t TINY_INT_POS_MAX = 127;

constexpr uint8_t INT_8 = 0xC8;
constexpr uint8_t INT_16 = 0xC9;
constexpr uint8_t INT_32 = 0xCA;
constexpr uint8_t INT_64 = 0xCB;

constexpr uint8_t FLOAT_64 = 0xC1;

// String ranges
constexpr uint8_t TINY_STRING_BASE = 0x80;
constexpr uint8_t TINY_STRING_MAX_LEN = 15;
constexpr uint8_t STRING_8 = 0xD0;
constexpr uint8_t STRING_16 = 0xD1;
constexpr uint8_t STRING_32 = 0xD2;

// Bytes
constexpr uint8_t BYTES_8 = 0xCC;
constexpr uint8_t BYTES_16 = 0xCD;
constexpr uint8_t BYTES_32 = 0xCE;

// List
constexpr uint8_t TINY_LIST_BASE = 0x90;
constexpr uint8_t TINY_LIST_MAX_LEN = 15;
constexpr uint8_t LIST_8 = 0xD4;
constexpr uint8_t LIST_16 = 0xD5;
constexpr uint8_t LIST_32 = 0xD6;

// Dictionary
constexpr uint8_t TINY_DICT_BASE = 0xA0;
constexpr uint8_t TINY_DICT_MAX_LEN = 15;
constexpr uint8_t DICT_8 = 0xD8;
constexpr uint8_t DICT_16 = 0xD9;
constexpr uint8_t DICT_32 = 0xDA;

// Structure
constexpr uint8_t TINY_STRUCT_BASE = 0xB0;
constexpr uint8_t TINY_STRUCT_MAX_LEN = 15;
constexpr uint8_t STRUCT_8 = 0xDC;
constexpr uint8_t STRUCT_16 = 0xDD;
constexpr uint8_t STRUCT_32 = 0xDF;
} // namespace marker

} // namespace packstream
} // namespace bolt
} // namespace service
} // namespace eugraph
