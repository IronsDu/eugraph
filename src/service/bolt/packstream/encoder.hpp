#pragma once

#include "service/bolt/packstream/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eugraph {
namespace service {
namespace bolt {
namespace packstream {

class Encoder {
public:
    Encoder() = default;

    void reset();

    const std::vector<uint8_t>& buffer() const {
        return buf_;
    }
    std::vector<uint8_t> release();

    void writeNull();
    void writeBoolean(bool v);
    void writeInt(int64_t v);
    void writeFloat(double v);
    void writeString(const std::string& v);
    void writeBytes(const std::vector<uint8_t>& v);
    void writeListHeader(size_t count);
    void writeDictHeader(size_t count);
    void writeStructHeader(uint8_t tag, size_t field_count);

    // Convenience: encode a Value recursively
    void writeValue(const Value& v);

private:
    void writeByte(uint8_t b);

    std::vector<uint8_t> buf_;
};

} // namespace packstream
} // namespace bolt
} // namespace service
} // namespace eugraph
