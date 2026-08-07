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
constexpr uint8_t TELEMETRY = 0x54; // tag shared with TIME (different contexts)
constexpr uint8_t ROUTE = 0x66;

// Server → Client
constexpr uint8_t SUCCESS = 0x70;
constexpr uint8_t RECORD = 0x71;
constexpr uint8_t IGNORED = 0x7E;
constexpr uint8_t FAILURE = 0x7F;

// Result types
constexpr uint8_t NODE = 0x4E;
constexpr uint8_t RELATIONSHIP = 0x52;
constexpr uint8_t PATH = 0x50;

// Temporal types (v5.0+)
constexpr uint8_t DATE = 0x44;
constexpr uint8_t TIME = 0x54;
constexpr uint8_t LOCAL_TIME = 0x74;
constexpr uint8_t DATETIME = 0x49;
constexpr uint8_t DATETIME_ZONE_ID = 0x69;
constexpr uint8_t LOCAL_DATETIME = 0x64;
constexpr uint8_t DURATION = 0x45;

// Temporal types (v4.x legacy — DATETIME tags changed in v5.0)
constexpr uint8_t DATETIME_V4 = 0x46;
constexpr uint8_t DATETIME_ZONE_ID_V4 = 0x66;
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

struct RouteMessage {
    std::unordered_map<std::string, packstream::Value> routing;
    std::vector<std::string> bookmarks;
    std::unordered_map<std::string, packstream::Value> extra;
};

// RESET, COMMIT, ROLLBACK, GOODBYE, TELEMETRY have no fields

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

// Bolt v5.x maximum chunk payload size (top 2 bits of uint16 header reserved).
inline constexpr uint16_t BOLT_MAX_CHUNK_SIZE = 0x3FFF; // 16383
// Maximum assembled message size to prevent memory exhaustion.
inline constexpr size_t BOLT_MAX_MESSAGE_SIZE = 64ULL * 1024 * 1024; // 64 MiB

} // namespace bolt
} // namespace eugraph
