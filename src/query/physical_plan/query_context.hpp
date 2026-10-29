#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

/// Cooperative cancellation flag for one statement. Armed once -- typically when
/// the client goes away -- and checked by the operators as they consume each batch
/// of upstream chunks. A cancelled operator stops producing, and because the
/// execution model is pull-based the rest of the tree unwinds on its own; the
/// stream teardown then rolls the transaction back.
///
/// Deliberately a plain flag rather than a folly cancellation token: the check
/// points are chosen by the operators, and this needs no coroutine plumbing.
using QueryCancel = std::shared_ptr<const std::atomic<bool>>;

/// Per-statement execution state, shared by every operator of one physical plan.
///
/// It exists so that statement-scoped state has a statement-scoped home: a token
/// here can only ever belong to one statement, whereas the store it used to live on
/// (IAsyncGraphDataStore) is shared by every query of a graph.
///
/// Held through std::shared_ptr (see PhysicalOperator::setQueryContext), so it is
/// destroyed together with the last operator of the tree, whatever order the tree
/// happens to be torn down in.
///
/// The destructor deliberately does NO work: ending the statement's transaction
/// belongs to the teardown paths that destroy the stream first
/// (BoltSession::abandonStream, StreamAbandonRollback). Rolling back from here
/// would free the WT session while operator destructors are still closing the
/// cursors that live in it.
struct OpTraceRecord {
    std::string name;
    uint64_t chunks = 0;
    uint64_t rows = 0;
    uint64_t wall_ns = 0;
    uint64_t cpu_ns = 0;
    OpTraceRecord* parent = nullptr;
    uint32_t depth = 0;
};

/// Parent record of the operator currently being constructed. The traced
/// wrapper installs it while executeChunk() runs, so a child can record which
/// subtree it belongs to. Execution is single-threaded per statement.
inline thread_local OpTraceRecord* tls_current_op_trace = nullptr;

class QueryContext {
public:
    explicit QueryContext(QueryCancel cancel = nullptr)
        : cancel_(std::move(cancel)), op_trace_(enabledFromEnv("EUGRAPH_OP_TRACE")) {}

    /// Operator-level trace is opt-in: disabled means executeChunkTraced() returns
    /// the operator's generator directly, with no wrapper coroutine.
    bool opTraceEnabled() const noexcept {
        return op_trace_;
    }

    /// Register one operator execution record. Execution is pull-based and
    /// single-threaded, so no synchronization is needed here.
    std::shared_ptr<OpTraceRecord> addOpTrace(std::string name, OpTraceRecord* parent) {
        auto record = std::make_shared<OpTraceRecord>();
        record->name = std::move(name);
        record->parent = parent;
        record->depth = parent ? parent->depth + 1 : 0;
        op_trace_records_.push_back(record);
        return record;
    }

    const std::vector<std::shared_ptr<OpTraceRecord>>& opTraceRecords() const noexcept {
        return op_trace_records_;
    }

    /// True once the caller cancelled this statement (for Bolt: the client went
    /// away). Operators read it as they consume upstream chunks; a cancelled
    /// operator co_returns and the pull-based tree unwinds.
    bool cancelled() const {
        return cancel_ && cancel_->load(std::memory_order_relaxed);
    }

private:
    static bool enabledFromEnv(const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr && *value != '\0';
    }

    QueryCancel cancel_;
    bool op_trace_ = false;
    std::vector<std::shared_ptr<OpTraceRecord>> op_trace_records_;
};

} // namespace compute
} // namespace eugraph
