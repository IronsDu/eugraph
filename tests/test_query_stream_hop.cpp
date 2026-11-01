// Unit tests for the Thrift stream hop: the operator generator must run on the
// compute executor while the consumer (fbthrift stream loop, network IO thread)
// resumes on its own executor.

#include "service/query_stream_hop.hpp"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>

#include <gtest/gtest.h>

#include <thread>

namespace {

using eugraph::Column;
using eugraph::DataChunk;
using eugraph::Value;
using eugraph::binder::BoundTypeKind;
using ThreadId = std::thread::id;

struct Trace {
    ThreadId consumer;
    ThreadId inner_start;
    ThreadId inner_resume;
};

folly::coro::Task<void> recordThread(ThreadId& out) {
    out = std::this_thread::get_id();
    co_return;
}

/// The generator starts inline (wherever the consumer resumes it), then awaits
/// a task on a third executor. The generator promise's executor — set by the
/// stream hop to compute, or by the consumer's next() to the consumer's
/// executor — decides where it resumes after that await.
folly::coro::AsyncGenerator<DataChunk> makeProbeGenerator(Trace& trace, folly::Executor::KeepAlive<> other) {
    trace.inner_start = std::this_thread::get_id();
    co_await folly::coro::co_withExecutor(std::move(other), folly::coro::makeTask());
    trace.inner_resume = std::this_thread::get_id();

    DataChunk chunk;
    chunk.count = 1;
    chunk.columns.push_back(Column::flat(BoundTypeKind::INT64, 1));
    chunk.columns[0].setValue(0, Value(static_cast<int64_t>(1)));
    co_yield std::move(chunk);
}

folly::coro::Task<size_t> consume(folly::coro::AsyncGenerator<DataChunk> gen, Trace& trace) {
    trace.consumer = std::this_thread::get_id();
    size_t rows = 0;
    while (auto chunk = co_await gen.next())
        rows += chunk->numRows();
    co_return rows;
}

ThreadId threadOf(folly::Executor* executor) {
    ThreadId tid{};
    folly::coro::blockingWait(folly::coro::co_withExecutor(folly::getKeepAliveToken(executor), recordThread(tid)));
    return tid;
}

} // namespace

TEST(QueryStreamHopTest, WrappedGeneratorRunsOnComputeAndConsumerStaysOnNet) {
    folly::IOThreadPoolExecutor net(1);
    folly::CPUThreadPoolExecutor compute(1);
    folly::IOThreadPoolExecutor other(1);

    const ThreadId net_tid = threadOf(&net);
    const ThreadId compute_tid = threadOf(&compute);
    ASSERT_NE(net_tid, compute_tid);

    Trace trace;
    auto inner = makeProbeGenerator(trace, folly::getKeepAliveToken(&other));
    auto wrapped =
        eugraph::service::hopGeneratorTo(std::move(inner), std::nullopt, false, folly::getKeepAliveToken(&compute));

    size_t rows = 0;
    folly::coro::blockingWait(
        folly::coro::co_withExecutor(folly::getKeepAliveToken(&net), [&]() -> folly::coro::Task<void> {
            rows = co_await consume(std::move(wrapped), trace);
        }()));

    EXPECT_EQ(rows, 1u);
    EXPECT_EQ(trace.consumer, net_tid) << "consumer coroutine left the network IO executor";
    // The first synchronous segment is inline on the consumer (folly's
    // NextAwaitable does not schedule); after the generator's first await it
    // must run on compute.
    EXPECT_EQ(trace.inner_start, net_tid);
    EXPECT_EQ(trace.inner_resume, compute_tid)
        << "operator generator did not resume on the compute executor after its await";
}

TEST(QueryStreamHopTest, UnwrappedGeneratorFollowsConsumerExecutor) {
    folly::IOThreadPoolExecutor net(1);
    folly::CPUThreadPoolExecutor compute(1);
    folly::IOThreadPoolExecutor other(1);

    const ThreadId net_tid = threadOf(&net);
    const ThreadId compute_tid = threadOf(&compute);
    ASSERT_NE(net_tid, compute_tid);

    Trace trace;
    auto inner = makeProbeGenerator(trace, folly::getKeepAliveToken(&other));

    size_t rows = 0;
    folly::coro::blockingWait(
        folly::coro::co_withExecutor(folly::getKeepAliveToken(&net), [&]() -> folly::coro::Task<void> {
            rows = co_await consume(std::move(inner), trace);
        }()));

    EXPECT_EQ(rows, 1u);
    EXPECT_EQ(trace.consumer, net_tid);
    // Without the hop, next() binds the generator to the consumer's executor.
    EXPECT_EQ(trace.inner_resume, net_tid)
        << "test is not discriminating: generator resumed on compute without the hop";
    EXPECT_NE(trace.inner_resume, compute_tid);
}

TEST(QueryStreamHopTest, PrefetchedFirstChunkIsYieldedBeforeResumingInner) {
    folly::IOThreadPoolExecutor net(1);
    folly::CPUThreadPoolExecutor compute(1);
    folly::IOThreadPoolExecutor other(1);

    Trace trace;
    auto inner = makeProbeGenerator(trace, folly::getKeepAliveToken(&other));

    DataChunk head;
    head.count = 1;
    head.columns.push_back(Column::flat(BoundTypeKind::INT64, 1));
    head.columns[0].setValue(0, Value(static_cast<int64_t>(99)));

    auto wrapped =
        eugraph::service::hopGeneratorTo(std::move(inner), std::move(head), false, folly::getKeepAliveToken(&compute));
    int64_t first = 0;
    size_t rows = 0;
    folly::coro::blockingWait(
        folly::coro::co_withExecutor(folly::getKeepAliveToken(&net), [&]() -> folly::coro::Task<void> {
            auto chunk = co_await wrapped.next();
            if (chunk) {
                rows = chunk->numRows();
                first = std::get<int64_t>(chunk->getValue(0, 0));
            }
            co_return;
        }()));

    EXPECT_EQ(rows, 1u);
    EXPECT_EQ(first, 99);
    // The prefetched chunk is yielded without touching the inner generator.
    EXPECT_EQ(trace.inner_start, ThreadId{});
}

TEST(QueryStreamHopTest, PrefetchedEmptyStreamDoesNotResumeDoneGenerator) {
    folly::IOThreadPoolExecutor net(1);
    folly::CPUThreadPoolExecutor compute(1);

    auto empty = []() -> folly::coro::AsyncGenerator<DataChunk> { co_return; }();
    auto wrapped = eugraph::service::hopGeneratorTo(std::move(empty), std::nullopt, /*inner_exhausted=*/true,
                                                    folly::getKeepAliveToken(&compute));
    size_t rows = 0;
    folly::coro::blockingWait(
        folly::coro::co_withExecutor(folly::getKeepAliveToken(&net), [&]() -> folly::coro::Task<void> {
            while (auto chunk = co_await wrapped.next())
                rows += chunk->numRows();
            co_return;
        }()));
    EXPECT_EQ(rows, 0u);
}
