#include "query/physical_plan/physical_operator_base.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <thread>

namespace eugraph {
namespace compute {

namespace {

uint64_t threadCpuNs() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

/// Column kind implied by a runtime Value. `nullptr` means "no opinion" (NULL or a
/// kind this helper does not classify), which callers treat as "keep looking".
const binder::BoundTypeKind* kindOf(const Value& v) {
    static const binder::BoundTypeKind kBool = binder::BoundTypeKind::BOOL;
    static const binder::BoundTypeKind kInt = binder::BoundTypeKind::INT64;
    static const binder::BoundTypeKind kDouble = binder::BoundTypeKind::DOUBLE;
    static const binder::BoundTypeKind kString = binder::BoundTypeKind::STRING;
    static const binder::BoundTypeKind kVertexRef = binder::BoundTypeKind::VERTEX_REF;
    static const binder::BoundTypeKind kEdgeKey = binder::BoundTypeKind::EDGE_KEY;
    static const binder::BoundTypeKind kPathTopology = binder::BoundTypeKind::PATH_TOPOLOGY;
    static const binder::BoundTypeKind kVertex = binder::BoundTypeKind::VERTEX;
    static const binder::BoundTypeKind kEdge = binder::BoundTypeKind::EDGE;
    static const binder::BoundTypeKind kPath = binder::BoundTypeKind::PATH;
    static const binder::BoundTypeKind kList = binder::BoundTypeKind::LIST;
    static const binder::BoundTypeKind kMap = binder::BoundTypeKind::MAP;
    static const binder::BoundTypeKind kDateTime = binder::BoundTypeKind::DATETIME;
    static const binder::BoundTypeKind kTime = binder::BoundTypeKind::TIME;
    static const binder::BoundTypeKind kDuration = binder::BoundTypeKind::DURATION;

    switch (v.index()) {
    case 1:
        return &kBool;
    case 2:
        return &kInt;
    case 3:
        return &kDouble;
    case 4:
        return &kString;
    case 5:
        return &kVertexRef;
    case 6:
        return &kEdgeKey;
    case 7:
        return &kPathTopology;
    case 8:
        return &kVertex;
    case 9:
        return &kEdge;
    case 10:
        return &kPath;
    case 11:
        return &kDateTime;
    case 12:
        return &kTime;
    case 13:
        return &kDuration;
    case 14:
        return &kList;
    case 15:
        return &kMap;
    default:
        return nullptr; // monostate / BytesValue: not classified here
    }
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> wrapRowsToChunkGenerator(std::vector<Row> rows) {
    if (rows.empty())
        co_return;

    size_t num_cols = 0;
    for (const auto& row : rows)
        num_cols = std::max(num_cols, row.size());
    if (num_cols == 0)
        co_return;

    // One column kind per position: first non-null cell wins. A position with no
    // typed cell anywhere stays ANY, matching the old RowBatch bridge.
    std::vector<binder::BoundTypeKind> kinds(num_cols, binder::BoundTypeKind::ANY);
    std::vector<bool> decided(num_cols, false);
    for (const auto& row : rows) {
        for (size_t c = 0; c < row.size(); ++c) {
            if (decided[c])
                continue;
            if (const auto* kind = kindOf(row[c])) {
                kinds[c] = *kind;
                decided[c] = true;
            }
        }
    }

    DataChunk chunk;
    for (size_t c = 0; c < num_cols; ++c)
        chunk.columns.push_back(Column::flat(kinds[c], rows.size()));

    for (size_t r = 0; r < rows.size(); ++r) {
        for (size_t c = 0; c < rows[r].size(); ++c)
            chunk.columns[c].setValue(r, rows[r][c]);
    }
    chunk.count = rows.size();
    chunk.sel = SelectionVector::identity(chunk.count);

    co_yield std::move(chunk);
}

folly::coro::AsyncGenerator<DataChunk> PhysicalOperator::executeChunkTraced() {
    if (!query_ctx_ || !query_ctx_->opTraceEnabled())
        return executeChunk();
    return executeChunkTracedImpl();
}

folly::coro::AsyncGenerator<DataChunk> PhysicalOperator::executeChunkTracedImpl() {
    // The parent record comes from the statement context, not a thread-local:
    // production builds attach the whole operator tree eagerly
    // (setQueryContext -> attachQueryContext), so this only matters for a record
    // created lazily, and either way a statement-scoped field cannot leak a
    // destroyed record across threads (#310).
    if (!trace_record_)
        trace_record_ = query_ctx_->addOpTrace(toString(), query_ctx_->currentOpTrace());
    std::shared_ptr<StorageOpCounters> const node_counters = trace_record_->counters;
    OpTraceRecord* const self = trace_record_.get();
    StorageProfileState* const state = query_ctx_->profileState();

    // What the caller had installed, captured once: this operator restores it
    // whenever it suspends so a sibling/caller's storage calls are not charged
    // to us. The state is per statement and a statement runs one coroutine at a
    // time, so plain LIFO save/restore is correct (no thread migration issue).
    std::shared_ptr<StorageOpCounters> const caller_counters = state != nullptr ? state->current : nullptr;
    OpTraceRecord* const caller_trace = query_ctx_->currentOpTrace();
    auto install = [&] {
        if (state != nullptr && node_counters)
            state->current = node_counters;
        query_ctx_->setCurrentOpTrace(self);
    };
    auto restore = [&] {
        if (state != nullptr && node_counters)
            state->current = caller_counters;
        query_ctx_->setCurrentOpTrace(caller_trace);
    };
    // A cancelled query can drop the operator tree while the generator is
    // suspended with its context installed; restore then.
    struct RestoreOnDestroy {
        std::function<void()> restore;
        ~RestoreOnDestroy() {
            restore();
        }
    } restore_on_destroy{restore};

    auto start_wall = std::chrono::steady_clock::now();
    auto start_thread = std::this_thread::get_id();
    auto start_cpu = threadCpuNs();
    auto child_gen = executeChunk();
    install();
    while (true) {
        auto chunk = co_await child_gen.next();
        install();
        const auto now_wall = std::chrono::steady_clock::now();
        trace_record_->wall_ns +=
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now_wall - start_wall).count());
        // CLOCK_THREAD_CPUTIME_ID is per-thread: after a cross-thread resume the
        // difference can be negative, which used to wrap the uint64 counter into
        // an absurd inclusive time. Only account CPU when the coroutine resumed on
        // the thread that started the interval.
        const auto now_thread = std::this_thread::get_id();
        if (now_thread == start_thread) {
            const auto now_cpu = threadCpuNs();
            if (now_cpu >= start_cpu)
                trace_record_->cpu_ns += now_cpu - start_cpu;
        }
        start_wall = now_wall;
        start_thread = now_thread;
        start_cpu = threadCpuNs();
        if (!chunk.has_value())
            break;
        ++trace_record_->chunks;
        trace_record_->rows += chunk->count;
        // The caller (and any sibling it pulls) runs while this generator is
        // suspended. Restore the caller's record before yielding, otherwise a
        // sibling's record is attached to this operator -- which made LeftJoin's
        // right sub-plan show up under its left child and inflated the join's
        // "self" time by the whole right side.
        restore();
        co_yield std::move(*chunk);
        install();
        // Do not charge time spent by downstream operators after this yield to
        // this operator: reset wall/CPU/thread when the generator is resumed.
        start_wall = std::chrono::steady_clock::now();
        start_thread = std::this_thread::get_id();
        start_cpu = threadCpuNs();
    }
    restore();
}

} // namespace compute
} // namespace eugraph
