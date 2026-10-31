#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eugraph {
namespace service {
namespace bolt {
namespace websocket {

/// Maximum WebSocket frame payload size (16 MB).
constexpr size_t kMaxFramePayload = 16 * 1024 * 1024;

/// Result of parsing a WebSocket frame header.
enum class Opcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA,
};

struct FrameHeader {
    bool fin = false;
    bool mask = false;
    Opcode opcode = Opcode::CONTINUATION;
    uint64_t payload_len = 0;
    uint8_t mask_key[4] = {};
    size_t header_size = 0; // Total bytes consumed by the header
};

/// Parse a WebSocket frame header. Returns false if more data is needed.
bool tryParseFrameHeader(const uint8_t* data, size_t len, FrameHeader& hdr);

/// Unmask payload data in-place using the given 4-byte mask key.
void unmaskPayload(uint8_t* data, size_t len, const uint8_t mask_key[4]);

// ---- Handshake ----

/// Parsed fields of a WebSocket upgrade request that the server cares about.
struct HandshakeRequest {
    std::string path;   ///< Request target, e.g. "/".
    std::string key;    ///< Sec-WebSocket-Key.
    std::string origin; ///< Origin header; empty when absent.
    bool has_origin = false;
};

/// Parse the WebSocket upgrade HTTP request.
/// Returns false if the request is not a valid upgrade (missing key/upgrade).
bool parseHandshakeRequest(const uint8_t* data, size_t len, HandshakeRequest& request);

/// Backwards-compatible helper: returns the Sec-WebSocket-Key or an empty string.
std::string parseHandshakeKey(const uint8_t* data, size_t len);

/// Build the 101 Switching Protocols handshake response.
std::string buildHandshakeResponse(const std::string& ws_key);

/// Build a 403 response for a rejected Origin. The caller closes after writing.
std::string buildForbiddenResponse();

/// Locate the end of HTTP headers (\r\n\r\n). Returns 0 if incomplete.
size_t findHeaderEnd(const uint8_t* data, size_t len);

// ---- Frame building (server -> client, unmasked) ----

/// Build a WebSocket data frame (binary or text, FIN=1, no mask).
std::vector<uint8_t> buildFrame(const uint8_t* payload, size_t len, Opcode opcode = Opcode::BINARY);

/// Build a WebSocket close frame.
std::vector<uint8_t> buildCloseFrame(uint16_t code = 1000);

/// Build a WebSocket pong frame.
std::vector<uint8_t> buildPongFrame(const uint8_t* data, size_t len);

} // namespace websocket
} // namespace bolt
} // namespace service
} // namespace eugraph
