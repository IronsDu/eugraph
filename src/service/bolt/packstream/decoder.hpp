#pragma once

#include "service/bolt/packstream/types.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace service {
namespace bolt {
namespace packstream {

class DecodeError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Decoder {
public:
    explicit Decoder(const uint8_t* data, size_t len) : data_(data), pos_(0), end_pos_(len) {}

    size_t remaining() const {
        return end_pos_ - pos_;
    }

    // Decode the next value from the buffer.
    Value decode();

    // Decode a structure, returning its tag and field count (fields must be decoded separately).
    // Returns {tag, field_count}.
    std::pair<uint8_t, size_t> decodeStructHeader();

    // Skip one value.
    void skip();

private:
    uint8_t readByte();
    const uint8_t* data_;
    size_t pos_;
    size_t end_pos_;
};

} // namespace packstream
} // namespace bolt
} // namespace service
} // namespace eugraph
