#pragma once

#include "query/dataset/data_chunk.hpp"

#include <folly/Executor.h>
#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>

#include <exception>
#include <optional>
#include <utility>

namespace eugraph {
namespace service {

/// Rebind an operator generator to `compute` for every batch.
///
/// The Thrift handler must keep its handler/stream loop on the network IO
/// thread: fbthrift's RocketServerConnection::WriteBatcher only flushes a
/// request/response reply promptly when it is produced on the connection's
/// EventBase thread (see docs/query/engine/execution-model.md §3).  Operator
/// execution, however, belongs on the compute pool.
///
/// `AsyncGenerator::next()`'s `viaIfAsync()` sets the generator promise's
/// executor *before* resuming it. The first synchronous segment still runs
/// inline on the consumer thread (folly's `NextAwaitable` does not schedule),
/// but the generator's first `co_await` (typically the storage dispatch of a
/// scan) resumes on `compute`; every subsequent batch starts on `compute`.
///
/// We deliberately do NOT wrap each `next()` in a Task started with
/// `co_withExecutor`: that would guarantee the first segment on `compute`, but
/// it detaches a coroutine frame from the stream; if fbthrift cancels/destroys
/// the stream while that task is in flight, the detached frame can outlive the
/// generator and touch freed state. `viaIfAsync` keeps the whole chain
/// structured and was the shape validated by the probe.
///
/// The wrapper itself is consumed by the fbthrift stream loop on the network
/// IO thread; after each batch the consumer resumes on its own executor.
inline folly::coro::AsyncGenerator<DataChunk> hopGeneratorTo(folly::coro::AsyncGenerator<DataChunk> inner,
                                                             std::optional<DataChunk> prefetched_first,
                                                             bool inner_exhausted,
                                                             folly::Executor::KeepAlive<> compute) {
    if (prefetched_first)
        co_yield std::move(*prefetched_first);
    // Prefetching the first chunk may have observed end-of-stream (empty
    // result); the generator is already done and calling next() again trips
    // AsyncGenerator's !coro_.done() check.
    if (inner_exhausted)
        co_return;
    if (!compute) {
        while (auto chunk = co_await inner.next())
            co_yield std::move(*chunk);
        co_return;
    }
    while (auto chunk = co_await folly::coro::co_viaIfAsync(compute.get_alias(), inner.next())) {
        co_yield std::move(*chunk);
    }
}

/// A stream whose first pull rethrows `error`. Used when the first-batch
/// prefetch fails: the handler must still return a stream so the error is
/// delivered on the stream (runtime) instead of as a method-level exception,
/// matching the pre-prefetch error semantics (and TCK's phase classification).
inline folly::coro::AsyncGenerator<DataChunk> errorGenerator(std::exception_ptr error) {
    std::rethrow_exception(error);
    co_return;
}

} // namespace service
} // namespace eugraph
