#pragma once

#include "bolt/bolt_messages.hpp"
#include "bolt/bolt_value_mapping.hpp"
#include "bolt/packstream/decoder.hpp"
#include "bolt/packstream/encoder.hpp"
#include "server/graph_service.hpp"

#include <folly/coro/Task.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace bolt {

class BoltConnection;

/// Per-connection Bolt protocol session state machine.
/// Handles Bolt v5.1 message dispatching and state transitions.
class BoltSession {
public:
    friend class BoltConnection;

    explicit BoltSession(server::GraphService& service, std::function<uint64_t()> next_bookmark = {})
        : service_(service), next_bookmark_fn_(std::move(next_bookmark)) {}

    void setBookmarkGenerator(std::function<uint64_t()> fn) {
        next_bookmark_fn_ = std::move(fn);
    }

    SessionState state() const {
        return state_;
    }
    bool isClosed() const {
        return state_ == SessionState::CLOSED;
    }

    /// Process the Bolt handshake preamble (4 bytes magic + 4 * num_versions version bytes).
    /// Returns the negotiated version bytes (4 bytes) or empty if negotiation fails.
    std::vector<uint8_t> negotiateHandshake(const uint8_t* data, size_t len);

    /// Process a single incoming Bolt message. Returns encoded response bytes.
    /// Caller must ensure this is called from a coroutine context.
    folly::coro::Task<std::vector<uint8_t>> processMessage(const uint8_t* data, size_t len);

private:
    // Message handlers (return encoded response)
    folly::coro::Task<std::vector<uint8_t>>
    handleHello(const std::unordered_map<std::string, packstream::Value>& fields);
    folly::coro::Task<std::vector<uint8_t>>
    handleLogon(const std::unordered_map<std::string, packstream::Value>& fields);
    folly::coro::Task<std::vector<uint8_t>> handleLogoff();
    folly::coro::Task<std::vector<uint8_t>> handleRun(const RunMessage& msg);
    folly::coro::Task<std::vector<uint8_t>> handlePull(const PullMessage& msg);
    folly::coro::Task<std::vector<uint8_t>> handleDiscard(const DiscardMessage& msg);
    folly::coro::Task<std::vector<uint8_t>> handleBegin(const BeginMessage& msg);
    folly::coro::Task<std::vector<uint8_t>> handleCommit();
    folly::coro::Task<std::vector<uint8_t>> handleRollback();
    folly::coro::Task<std::vector<uint8_t>> handleReset();
    folly::coro::Task<std::vector<uint8_t>> handleGoodbye();

    // Response builders
    std::vector<uint8_t> makeSuccess(const std::unordered_map<std::string, packstream::Value>& fields);
    std::vector<uint8_t> makeFailure(const std::string& code, const std::string& message);
    std::vector<uint8_t> makeIgnored();
    std::vector<uint8_t> makeRecord(const std::vector<packstream::Value>& fields);

    // Serialize a packstream Value to bytes
    std::vector<uint8_t> serialize(const packstream::Value& v);

    server::GraphService& service_;
    SessionState state_ = SessionState::CONNECTING;
    uint32_t negotiated_version_ = 0;

    // Current query execution context (set by RUN, consumed by PULL)
    std::shared_ptr<compute::StreamContext> stream_ctx_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs_;

    // Explicit transaction state
    bool in_transaction_ = false;

    // Pending transaction handle for explicit transactions (saved before
    // stream_ctx_ is reset in PULL/DISCARD, committed/rolled back later).
    GraphTxnHandle pending_txn_ = INVALID_GRAPH_TXN;
    class IAsyncGraphDataStore* pending_store_ = nullptr;

    // Current database name (from HELLO db field or RUN extra metadata)
    std::string current_database_ = "default";

    // Authentication state
    std::string auth_scheme_;
    std::string auth_principal_;

    // Bookmark generation callback
    std::function<uint64_t()> next_bookmark_fn_;
    std::vector<std::string> received_bookmarks_;
};

} // namespace bolt
} // namespace eugraph
