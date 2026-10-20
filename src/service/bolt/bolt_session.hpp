#pragma once

#include "service/bolt/bolt_messages.hpp"
#include "service/bolt/bolt_value_mapping.hpp"
#include "service/bolt/packstream/decoder.hpp"
#include "service/bolt/packstream/encoder.hpp"
#include "service/graph_service.hpp"

#include <folly/coro/Task.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace service {
namespace bolt {

class BoltConnection;

/// Per-connection Bolt protocol session state machine.
/// Handles Bolt v5.1 message dispatching and state transitions.
class BoltSession {
public:
    friend class BoltConnection;

    explicit BoltSession(service::GraphService& service, std::function<uint64_t()> next_bookmark = {})
        : service_(service), next_bookmark_fn_(std::move(next_bookmark)) {}

    void setBookmarkGenerator(std::function<uint64_t()> fn) {
        next_bookmark_fn_ = std::move(fn);
    }
    void setBoltPort(uint16_t port) {
        bolt_port_ = port;
    }

    /// Lets the owning connection tell a running PULL that the client is gone, so
    /// it stops pulling instead of draining a result nobody will read. Checked
    /// between batches, so it cannot interrupt a single long gen.next().
    void setConnectionClosedFlag(std::shared_ptr<const std::atomic<bool>> flag) {
        connection_closed_ = std::move(flag);
    }

    /// Ends the current stream, dropping any partially consumed chunk with it.
    /// Every path that ends a stream goes through here so that stale rows can
    /// never be served to a later query.
    ///
    /// This is the low-level primitive: it does NOT end the stream's transaction.
    /// Use abandonStream() unless the caller has just committed or rolled back.
    void endStream() {
        pending_chunk_.reset();
        pending_row_ = 0;
        stream_ctx_.reset();
    }

    /// Ends the current stream AND ends the transaction it still owns.
    ///
    /// Every path that abandons an unfinished stream comes through here (Bolt
    /// teardown, DISCARD, RESET, GOODBYE, a new RUN, and the error path).
    ///
    /// `rollback_explicit` also rolls back the explicit transaction: a dead
    /// connection, GOODBYE or RESET must not leave it behind, while DISCARD and a
    /// new RUN must keep it for the client's later COMMIT.
    ///
    /// Order matters: the operator tree (and the cursors it still holds) is
    /// destroyed before the session is ended, never after.
    void abandonStream(bool rollback_explicit) {
        std::shared_ptr<compute::StreamContext> ctx = std::move(stream_ctx_);
        endStream();

        if (ctx) {
            // 算子树/游标先销毁 ✓，再结束本语句的事务（自动提交语句 ✓）
            ctx->gen = folly::coro::AsyncGenerator<DataChunk>{};
            ctx->phys_op.reset();
            if (ctx->session && ctx->should_commit)
                ctx->rollback();
            ctx.reset();
        }
        if (rollback_explicit && explicit_txn_) {
            explicit_txn_->session->rollback();
            explicit_txn_.reset();
        }
        if (rollback_explicit)
            in_transaction_ = false;
    }

    /// True when the result being streamed must not be completed: either the client
    /// is gone, or the running statement observed the cancel flag and stopped
    /// producing. Both mean the same thing here -- stop pulling and roll back
    /// instead of commit.
    bool streamCancelled() const {
        if (connection_closed_ && connection_closed_->load(std::memory_order_relaxed))
            return true;
        return stream_ctx_ && stream_ctx_->query_context && stream_ctx_->query_context->cancelled();
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
    folly::coro::Task<std::vector<uint8_t>> handleRoute(const RouteMessage& msg);
    folly::coro::Task<std::vector<uint8_t>> handleReset();
    folly::coro::Task<std::vector<uint8_t>> handleGoodbye();

    // Response builders
    std::vector<uint8_t> makeSuccess(const std::unordered_map<std::string, packstream::Value>& fields);
    std::vector<uint8_t> makeFailure(const std::string& code, const std::string& message);
    /// FAILURE 响应：把异常翻译成 Neo4j 状态码。QueryException 自带分类；
    /// 其它异常按消息里的分类 token 判定，认不出则归为执行失败。
    std::vector<uint8_t> makeFailureFor(const std::exception& e);
    std::vector<uint8_t> makeIgnored();
    std::vector<uint8_t> makeRecord(const std::vector<packstream::Value>& fields);

    // Serialize a packstream Value to bytes
    std::vector<uint8_t> serialize(const packstream::Value& v);

    service::GraphService& service_;
    SessionState state_ = SessionState::CONNECTING;
    uint32_t negotiated_version_ = 0;

    /// Steady-clock timestamp when the current RUN was received. Used to
    /// report Bolt t_first/t_last timing metadata.
    std::chrono::steady_clock::time_point query_start_;

    // Current query execution context (set by RUN, consumed by PULL)
    std::shared_ptr<compute::StreamContext> stream_ctx_;
    /// Rows of a chunk that a page boundary cut short. Kept so the next PULL
    /// resumes mid-chunk instead of dropping them.
    std::optional<DataChunk> pending_chunk_;
    size_t pending_row_ = 0;
    /// Shared with the owning connection; true once the socket is gone.
    std::shared_ptr<const std::atomic<bool>> connection_closed_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs_;

    // Explicit transaction state
    bool in_transaction_ = false;

    // Explicit transaction (BEGIN…COMMIT): 一条语句一个会话租约，事务跨语句存活 ✓
    std::optional<GraphService::SessionScope> explicit_txn_;

    // Current database name (from HELLO db field or RUN extra metadata)
    std::string current_database_ = "default";

    // Authentication state
    std::string auth_scheme_;
    std::string auth_principal_;

    // Bookmark generation callback
    std::function<uint64_t()> next_bookmark_fn_;
    std::vector<std::string> received_bookmarks_;

    // Bolt server port (used for ROUTE response)
    uint16_t bolt_port_ = 7687;
};

} // namespace bolt
} // namespace service
} // namespace eugraph
