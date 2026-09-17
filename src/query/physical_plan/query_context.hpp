#pragma once

#include <atomic>
#include <memory>
#include <utility>

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
class QueryContext {
public:
    explicit QueryContext(QueryCancel cancel = nullptr) : cancel_(std::move(cancel)) {}

    /// True once the caller cancelled this statement (for Bolt: the client went
    /// away). Operators read it as they consume upstream chunks; a cancelled
    /// operator co_returns and the pull-based tree unwinds.
    bool cancelled() const {
        return cancel_ && cancel_->load(std::memory_order_relaxed);
    }

private:
    QueryCancel cancel_;
};

} // namespace compute
} // namespace eugraph
