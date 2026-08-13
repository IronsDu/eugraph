#include "service/bolt/websocket.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <cstring>
#include <sstream>
#include <string>

namespace eugraph {
namespace service {
namespace bolt {
namespace websocket {

// ==================== SHA1 (minimal, self-contained) ====================

namespace {

uint32_t rotl32(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}

struct SHA1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t total_bytes = 0;
    uint8_t buf[64] = {};
    size_t buf_len = 0;

    void update(const uint8_t* data, size_t len) {
        total_bytes += len;
        while (len > 0) {
            size_t n = 64 - buf_len;
            if (n > len)
                n = len;
            std::memcpy(buf + buf_len, data, n);
            buf_len += n;
            data += n;
            len -= n;
            if (buf_len == 64) {
                processBlock(buf);
                buf_len = 0;
            }
        }
    }

    void finalize(uint8_t digest[20]) {
        uint64_t bits = total_bytes * 8;
        buf[buf_len++] = 0x80;
        if (buf_len > 56) {
            std::memset(buf + buf_len, 0, 64 - buf_len);
            processBlock(buf);
            buf_len = 0;
        }
        std::memset(buf + buf_len, 0, 56 - buf_len);
        for (int i = 0; i < 8; ++i)
            buf[56 + i] = static_cast<uint8_t>(bits >> (56 - i * 8));
        processBlock(buf);

        for (int i = 0; i < 5; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>(h[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(h[i]);
        }
    }

private:
    void processBlock(const uint8_t block[64]) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
                   (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
        for (int i = 16; i < 80; ++i)
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t t = rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
};

std::vector<uint8_t> sha1(const uint8_t* data, size_t len) {
    SHA1 ctx;
    ctx.update(data, len);
    uint8_t digest[20];
    ctx.finalize(digest);
    return {digest, digest + 20};
}

const char kBase64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < len)
            v |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < len)
            v |= uint32_t(data[i + 2]);
        out.push_back(kBase64Table[(v >> 18) & 0x3F]);
        out.push_back(kBase64Table[(v >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kBase64Table[(v >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kBase64Table[v & 0x3F] : '=');
    }
    return out;
}

constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

} // anonymous namespace

// ==================== Frame parsing ====================

bool tryParseFrameHeader(const uint8_t* data, size_t len, FrameHeader& hdr) {
    if (len < 2)
        return false;

    hdr.fin = (data[0] & 0x80) != 0;
    hdr.opcode = static_cast<Opcode>(data[0] & 0x0F);
    hdr.mask = (data[1] & 0x80) != 0;
    uint8_t len7 = data[1] & 0x7F;

    size_t pos = 2;

    if (len7 <= 125) {
        hdr.payload_len = len7;
    } else if (len7 == 126) {
        if (len < 4)
            return false;
        hdr.payload_len = (uint64_t(data[2]) << 8) | data[3];
        pos = 4;
    } else { // len7 == 127
        if (len < 10)
            return false;
        hdr.payload_len = 0;
        for (int i = 0; i < 8; ++i)
            hdr.payload_len = (hdr.payload_len << 8) | data[2 + i];
        pos = 10;
    }

    if (hdr.mask) {
        if (len < pos + 4)
            return false;
        std::memcpy(hdr.mask_key, data + pos, 4);
        pos += 4;
    }

    hdr.header_size = pos;
    return true;
}

void unmaskPayload(uint8_t* data, size_t len, const uint8_t mask_key[4]) {
    for (size_t i = 0; i < len; ++i)
        data[i] ^= mask_key[i % 4];
}

// ==================== Handshake ====================

size_t findHeaderEnd(const uint8_t* data, size_t len) {
    if (len < 4)
        return 0;
    for (size_t i = 0; i <= len - 4; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
            return i + 4;
    }
    return 0;
}

std::string parseHandshakeKey(const uint8_t* data, size_t len) {
    std::string request(reinterpret_cast<const char*>(data), len);

    // Must be GET with Upgrade: websocket
    if (request.find("GET ") != 0)
        return {};

    // HTTP header names and values are case-insensitive. Browser WebSocket
    // implementations (including Node's built-in WebSocket) may send headers
    // in lowercase, so do a case-insensitive scan instead of exact matching.
    auto containsCI = [](const std::string& haystack, const std::string& needle) {
        if (needle.empty())
            return haystack.find(needle);
        for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                char a = haystack[i + j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b - 'A' + 'a');
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match)
                return i;
        }
        return std::string::npos;
    };

    if (containsCI(request, "upgrade: websocket") == std::string::npos)
        return {};

    // Extract Sec-WebSocket-Key
    const std::string key_hdr = "sec-websocket-key:";
    size_t pos = containsCI(request, key_hdr);
    if (pos == std::string::npos)
        return {};

    pos += key_hdr.size();
    // Skip whitespace
    while (pos < request.size() && (request[pos] == ' ' || request[pos] == '\t'))
        ++pos;
    size_t end = request.find('\r', pos);
    if (end == std::string::npos)
        return {};
    return request.substr(pos, end - pos);
}

std::string buildHandshakeResponse(const std::string& ws_key) {
    std::string accept_input = ws_key + std::string(kWebSocketGuid);
    auto hash = sha1(reinterpret_cast<const uint8_t*>(accept_input.data()), accept_input.size());
    std::string accept_key = base64Encode(hash.data(), hash.size());

    std::ostringstream oss;
    oss << "HTTP/1.1 101 Switching Protocols\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
        << "\r\n";
    return oss.str();
}

// ==================== Frame building ====================

static void appendLength(uint64_t len, std::vector<uint8_t>& out) {
    if (len <= 125) {
        out.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        out.push_back(126);
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        out.push_back(127);
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
    }
}

std::vector<uint8_t> buildFrame(const uint8_t* payload, size_t len, Opcode opcode) {
    std::vector<uint8_t> out;
    out.reserve(2 + 8 + len);                           // header + max extended len + payload
    out.push_back(0x80 | static_cast<uint8_t>(opcode)); // FIN + opcode
    // Server -> client: no mask
    appendLength(len, out);
    out.insert(out.end(), payload, payload + len);
    return out;
}

std::vector<uint8_t> buildCloseFrame(uint16_t code) {
    uint8_t payload[2] = {static_cast<uint8_t>(code >> 8), static_cast<uint8_t>(code & 0xFF)};
    return buildFrame(payload, 2, Opcode::CLOSE);
}

std::vector<uint8_t> buildPongFrame(const uint8_t* data, size_t len) {
    return buildFrame(data, len, Opcode::PONG);
}

} // namespace websocket
} // namespace bolt
} // namespace service
} // namespace eugraph
