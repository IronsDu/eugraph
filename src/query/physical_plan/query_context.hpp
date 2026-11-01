#pragma once

#include "query/profile/query_profile.hpp"
#include "storage/storage_op_metrics.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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
    std::string details;
    std::vector<std::string> identifiers;
    uint64_t chunks = 0;
    uint64_t rows = 0;
    uint64_t wall_ns = 0;
    uint64_t cpu_ns = 0;
    OpTraceRecord* parent = nullptr;
    uint32_t depth = 0;
    /// Per-operator storage counters. Only allocated when PROFILE is enabled;
    /// aggregate points at the statement-level counters.
    std::shared_ptr<StorageOpCounters> counters;
};

/// Parent record of the operator currently being constructed. The traced
/// wrapper installs it while executeChunk() runs, so a child can record which
/// subtree it belongs to. Execution is single-threaded per statement.
inline thread_local OpTraceRecord* tls_current_op_trace = nullptr;

class QueryContext {
public:
    explicit QueryContext(QueryCancel cancel = nullptr, bool profile = false)
        : cancel_(std::move(cancel)), op_trace_(enabledFromEnv("EUGRAPH_OP_TRACE")), profile_(profile),
          op_trace_log_(op_trace_) {
        if (profile_)
            statement_counters_ = std::make_shared<StorageOpCounters>();
    }

    /// Operator-level trace is opt-in: disabled means executeChunkTraced() returns
    /// the operator's generator directly, with no wrapper coroutine.
    bool opTraceEnabled() const noexcept {
        return op_trace_ || profile_;
    }

    bool profileEnabled() const noexcept {
        return profile_;
    }

    /// Debug logging (EUGRAPH_OP_TRACE) is separate from PROFILE collection:
    /// a PROFILE statement must not spam the server log with op-trace lines.
    bool opTraceLogEnabled() const noexcept {
        return op_trace_log_;
    }

    const std::shared_ptr<StorageOpCounters>& statementCounters() const noexcept {
        return statement_counters_;
    }

    /// Register one operator execution record. Execution is pull-based and
    /// single-threaded, so no synchronization is needed here.
    std::shared_ptr<OpTraceRecord> addOpTrace(std::string name, OpTraceRecord* parent,
                                              std::vector<std::string> identifiers = {}) {
        auto record = std::make_shared<OpTraceRecord>();
        record->name = std::move(name);
        record->parent = parent;
        record->depth = parent ? parent->depth + 1 : 0;
        record->identifiers = std::move(identifiers);
        if (profile_) {
            record->counters = std::make_shared<StorageOpCounters>();
            record->counters->aggregate = statement_counters_.get();
        }
        op_trace_records_.push_back(record);
        return record;
    }

    /// Materialize the tree/metrics when the stream is fully consumed.
    QueryProfile buildProfile(uint64_t planning_ns, uint64_t execution_ns, uint64_t commit_ns) const;

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
    bool profile_ = false;
    bool op_trace_log_ = false;
    std::shared_ptr<StorageOpCounters> statement_counters_;
    std::vector<std::shared_ptr<OpTraceRecord>> op_trace_records_;
};

inline QueryProfile QueryContext::buildProfile(uint64_t planning_ns, uint64_t execution_ns, uint64_t commit_ns) const {
    QueryProfile profile;
    profile.planning_ns = planning_ns;
    profile.execution_ns = execution_ns;
    profile.commit_ns = commit_ns;

    std::unordered_map<const OpTraceRecord*, std::shared_ptr<ProfileNode>> nodes;
    nodes.reserve(op_trace_records_.size());
    for (const auto& record : op_trace_records_) {
        auto node = std::make_shared<ProfileNode>();
        const auto open = record->name.find('(');
        node->operator_type = open == std::string::npos ? record->name : record->name.substr(0, open);
        node->details = record->name;
        node->identifiers = record->identifiers;
        node->rows = record->rows;
        node->chunks = record->chunks;
        node->inclusive_wall_ns = record->wall_ns;
        node->inclusive_cpu_ns = record->cpu_ns;
        if (record->counters) {
            node->wt_search = record->counters->wt_search.load(std::memory_order_relaxed);
            node->wt_search_near = record->counters->wt_search_near.load(std::memory_order_relaxed);
            node->scan_rows = record->counters->wt_scan_rows.load(std::memory_order_relaxed);
            node->io_dispatches = record->counters->io_dispatches.load(std::memory_order_relaxed);
            node->prefetch_hits = record->counters->prefetch_hits.load(std::memory_order_relaxed);
            node->prefetch_misses = record->counters->prefetch_misses.load(std::memory_order_relaxed);
            node->db_hits = node->wt_search + node->wt_search_near + node->scan_rows;
        }
        nodes.emplace(record.get(), node);
        if (record->parent) {
            auto parent_it = nodes.find(record->parent);
            if (parent_it != nodes.end())
                parent_it->second->children.push_back(node);
            else
                profile.roots.push_back(node);
        } else {
            profile.roots.push_back(node);
        }
    }

    std::function<void(ProfileNode&)> finalize = [&](ProfileNode& node) {
        uint64_t child_wall = 0;
        uint64_t child_cpu = 0;
        for (auto& child : node.children) {
            finalize(*child);
            child_wall += child->inclusive_wall_ns;
            child_cpu += child->inclusive_cpu_ns;
        }
        node.self_wall_ns = node.inclusive_wall_ns > child_wall ? node.inclusive_wall_ns - child_wall : 0;
        node.self_cpu_ns = node.inclusive_cpu_ns > child_cpu ? node.inclusive_cpu_ns - child_cpu : 0;
    };
    for (auto& root : profile.roots)
        finalize(*root);

    if (!profile.roots.empty()) {
        profile.rows = profile.roots.front()->rows;
        profile.chunks = profile.roots.front()->chunks;
    }
    if (statement_counters_) {
        profile.db_hits = statement_counters_->wt_search.load(std::memory_order_relaxed) +
                          statement_counters_->wt_search_near.load(std::memory_order_relaxed) +
                          statement_counters_->wt_scan_rows.load(std::memory_order_relaxed);
        profile.wt_search = statement_counters_->wt_search.load(std::memory_order_relaxed);
        profile.wt_search_near = statement_counters_->wt_search_near.load(std::memory_order_relaxed);
        profile.scan_rows = statement_counters_->wt_scan_rows.load(std::memory_order_relaxed);
        profile.io_dispatches = statement_counters_->io_dispatches.load(std::memory_order_relaxed);
        profile.prefetch_hits = statement_counters_->prefetch_hits.load(std::memory_order_relaxed);
        profile.prefetch_misses = statement_counters_->prefetch_misses.load(std::memory_order_relaxed);
    } else {
        for (const auto& record : op_trace_records_) {
            if (record->counters) {
                profile.wt_search += record->counters->wt_search.load(std::memory_order_relaxed);
                profile.wt_search_near += record->counters->wt_search_near.load(std::memory_order_relaxed);
                profile.scan_rows += record->counters->wt_scan_rows.load(std::memory_order_relaxed);
                profile.io_dispatches += record->counters->io_dispatches.load(std::memory_order_relaxed);
                profile.prefetch_hits += record->counters->prefetch_hits.load(std::memory_order_relaxed);
                profile.prefetch_misses += record->counters->prefetch_misses.load(std::memory_order_relaxed);
            }
        }
        profile.db_hits = profile.wt_search + profile.wt_search_near + profile.scan_rows;
    }
    return profile;
}

} // namespace compute
} // namespace eugraph
