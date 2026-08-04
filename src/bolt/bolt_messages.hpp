#pragma once

#include "bolt/packstream/types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace bolt {

// Bolt v5.1 structure tags
namespace tags {
// Client → Server
constexpr uint8_t HELLO = 0x01;
constexpr uint8_t GOODBYE = 0x02;
constexpr uint8_t LOGON = 0x6A;
constexpr uint8_t LOGOFF = 0x6B;
constexpr uint8_t RESET = 0x0F;
constexpr uint8_t RUN = 0x10;
constexpr uint8_t DISCARD = 0x2E;
constexpr uint8_t PULL = 0x3F;
constexpr uint8_t BEGIN = 0x11;
constexpr uint8_t COMMIT = 0x12;
constexpr uint8_t ROLLBACK = 0x13;

// Server → Client
constexpr uint8_t SUCCESS = 0x70;
constexpr uint8_t RECORD = 0x71;
constexpr uint8_t IGNORED = 0x7E;
constexpr uint8_t FAILURE = 0x7F;

// Result types
constexpr uint8_t NODE = 0x4E;
constexpr uint8_t RELATIONSHIP = 0x52;
constexpr uint8_t PATH = 0x50;
} // namespace tags

// ==================== Client → Server Messages ====================

struct HelloMessage {
    std::unordered_map<std::string, packstream::Value> fields;
};

struct RunMessage {
    std::string query;
    std::unordered_map<std::string, packstream::Value> parameters;
    std::unordered_map<std::string, packstream::Value> extra;
};

struct PullMessage {
    int64_t n = -1;   // -1 = all records
    int64_t qid = -1; // query id
};

struct DiscardMessage {
    int64_t n = -1;
    int64_t qid = -1;
};

struct BeginMessage {
    std::unordered_map<std::string, packstream::Value> extra;
};

// RESET, COMMIT, ROLLBACK, GOODBYE have no fields

// ==================== Server → Client Messages ====================

struct SuccessMessage {
    std::unordered_map<std::string, packstream::Value> fields;
};

struct FailureMessage {
    std::string code;
    std::string message;
};

struct RecordMessage {
    std::vector<packstream::Value> fields;
};

// IGNORED has no fields

// ==================== Session State ====================

enum class SessionState {
    CONNECTING,   // Waiting for HELLO
    READY,        // Ready to accept RUN/BEGIN
    STREAMING,    // Auto-commit transaction: RUN sent, waiting for PULL
    TX_READY,     // Explicit transaction: READY after BEGIN
    TX_STREAMING, // Explicit transaction: RUN sent, waiting for PULL
    FAILED,       // Error state, waiting for RESET
    CLOSED,       // Connection closed
};

// ==================== Bolt Handshake ====================

inline constexpr uint32_t BOLT_MAGIC = 0x6060B017;

// Supported versions (most preferred first)
inline constexpr uint32_t BOLT_VERSION_5_1 = 0x00000501;
inline constexpr uint32_t BOLT_VERSION_5_0 = 0x00000500;
inline constexpr uint32_t BOLT_VERSION_4_4 = 0x00000404;

// Version sent to client after negotiation.
// Each version proposal is 4 bytes (uint32), followed by 4 bytes of padding (0).
// We propose 3 versions → 12 bytes → fits in one chunk.
inline constexpr uint32_t BOLT_PROPOSED_VERSIONS[] = {
    BOLT_VERSION_5_1,
    BOLT_VERSION_5_0,
    BOLT_VERSION_4_4,
};

} // namespace bolt
} // namespace eugraph
