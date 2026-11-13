// Unit tests for IoScheduler: where does the dispatched callable run, and where
// does the calling coroutine resume?
//
// These tests pin the contract the storage layer relies on:
//   1. the storage callable must execute on the Storage IO executor (never on
//      the caller's thread), and
//   2. the calling coroutine must resume on its own executor afterwards.
//
// "IoSchedulerTest.Dispatch*" exercise the production dispatch path; the A/B
// test checks it against a reference implementation built directly on
// co_withExecutor (the folly primitive that actually schedules work).

#include "storage/io_scheduler.hpp"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>

#include <gtest/gtest.h>

#include <thread>

namespace {

using ThreadId = std::thread::id;

struct ThreadTrace {
    ThreadId caller;
    ThreadId work;
    ThreadId resume;
    int value = 0;
};

/// Production path: IoScheduler::dispatchVoid.
folly::coro::Task<void> runDispatchVoid(eugraph::IoScheduler& io, ThreadTrace& trace) {
    trace.caller = std::this_thread::get_id();
    co_await io.dispatchVoid([&trace] { trace.work = std::this_thread::get_id(); });
    trace.resume = std::this_thread::get_id();
}

/// Production path: IoScheduler::dispatch (non-void).
folly::coro::Task<void> runDispatchValue(eugraph::IoScheduler& io, ThreadTrace& trace) {
    trace.caller = std::this_thread::get_id();
    trace.value = co_await io.dispatch([&trace] {
        trace.work = std::this_thread::get_id();
        return 42;
    });
    trace.resume = std::this_thread::get_id();
}

/// Reference path: schedule the child with co_withExecutor (the folly
/// primitive that actually runs it on the IO executor) and await it.
folly::coro::Task<void> runReferenceDispatch(eugraph::IoScheduler& io, ThreadTrace& trace) {
    trace.caller = std::this_thread::get_id();
    auto child = folly::coro::co_invoke([&trace]() -> folly::coro::Task<int> {
        trace.work = std::this_thread::get_id();
        co_return 42;
    });
    trace.value = co_await folly::coro::co_withExecutor(folly::getKeepAliveToken(io.executor()), std::move(child));
    trace.resume = std::this_thread::get_id();
}

template <typename Task> void runOn(folly::Executor* executor, Task&& task) {
    folly::coro::blockingWait(folly::coro::co_withExecutor(folly::getKeepAliveToken(executor), std::move(task)));
}

} // namespace

/// Exceptions thrown by the storage callable must cross the executor hop and
/// surface in the caller, not terminate the IO worker.
TEST(IoSchedulerTest, DispatchPropagatesExceptionToCaller) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::Pool);
    folly::CPUThreadPoolExecutor compute(1);
    bool caught = false;
    std::string message;

    runOn(&compute, [&]() -> folly::coro::Task<void> {
        try {
            co_await io.dispatchVoid([] { throw std::runtime_error("storage boom"); });
        } catch (const std::runtime_error& e) {
            caught = true;
            message = e.what();
        }
    }());

    EXPECT_TRUE(caught);
    EXPECT_EQ(message, "storage boom");
}

/// The storage callable must not run on the caller's compute thread.
TEST(IoSchedulerTest, DispatchVoidRunsOnIoPoolNotCaller) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::Pool);
    folly::CPUThreadPoolExecutor compute(1);
    ThreadTrace trace;

    runOn(&compute, runDispatchVoid(io, trace));

    EXPECT_NE(trace.work, ThreadId{}) << "callable never ran";
    EXPECT_NE(trace.work, trace.caller)
        << "IoScheduler::dispatchVoid ran the storage callable on the caller (compute) thread: "
           "IO and compute are not separated";
    EXPECT_EQ(trace.resume, trace.caller) << "caller coroutine did not resume on its compute executor";
}

TEST(IoSchedulerTest, DispatchValueRunsOnIoPoolNotCaller) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::Pool);
    folly::CPUThreadPoolExecutor compute(1);
    ThreadTrace trace;

    runOn(&compute, runDispatchValue(io, trace));

    EXPECT_EQ(trace.value, 42);
    EXPECT_NE(trace.work, ThreadId{}) << "callable never ran";
    EXPECT_NE(trace.work, trace.caller)
        << "IoScheduler::dispatch ran the storage callable on the caller (compute) thread";
    EXPECT_EQ(trace.resume, trace.caller) << "caller coroutine did not resume on its compute executor";
}

/// Thrift-like setup: the caller runs on a separate IO pool (the handler
/// executor), which must still be distinct from the storage IO pool.
TEST(IoSchedulerTest, DispatchFromHandlerIoPoolRunsOnStorageIoPool) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::Pool);
    folly::IOThreadPoolExecutor handler_pool(1);
    ThreadTrace trace;

    runOn(&handler_pool, runDispatchVoid(io, trace));

    EXPECT_NE(trace.work, ThreadId{}) << "callable never ran";
    EXPECT_NE(trace.work, trace.caller)
        << "IoScheduler::dispatchVoid ran the storage callable on the handler (Thrift IO) thread";
    EXPECT_EQ(trace.resume, trace.caller) << "caller coroutine did not resume on its handler executor";
}

/// Legacy path: the caller is driven by blockingWait (no explicit executor).
/// The callable must still go to the IO pool and the continuation must come
/// back to the thread that entered blockingWait.
TEST(IoSchedulerTest, DispatchFromBlockingWaitStillReturnsToCaller) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::Pool);
    ThreadTrace trace;

    folly::coro::blockingWait(runDispatchVoid(io, trace));

    EXPECT_NE(trace.work, ThreadId{}) << "callable never ran";
    EXPECT_NE(trace.work, trace.caller) << "IoScheduler::dispatchVoid ran inline on the blockingWait thread";
    EXPECT_EQ(trace.resume, trace.caller) << "continuation did not return to the blockingWait thread";
}

/// A/B: production dispatch must have the same observable thread behaviour as
/// the co_withExecutor reference. Both dispatch to the same single IO worker.
TEST(IoSchedulerTest, AbProductionDispatchMatchesReference) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::Pool);
    folly::CPUThreadPoolExecutor compute(1);
    ThreadTrace production;
    ThreadTrace reference;

    runOn(&compute, [&]() -> folly::coro::Task<void> {
        co_await runDispatchVoid(io, production);
        co_await runReferenceDispatch(io, reference);
    }());

    // Reference sanity: this is what "IO/compute separation" looks like.
    EXPECT_NE(reference.work, reference.caller) << "reference co_withExecutor did not leave the caller thread";
    EXPECT_EQ(reference.resume, reference.caller) << "reference did not resume on the caller executor";

    // Production must match it.
    EXPECT_EQ(production.work, reference.work)
        << "production dispatch did not use the storage IO worker used by the reference";
    EXPECT_EQ(production.resume, production.caller) << "production did not resume on the caller executor";
    EXPECT_EQ(reference.value, 42);
}

/// Inline mode: the callable runs on the caller's own thread (no hop), which is
/// the whole point of the mode (cache locality), and the caller continues there.
TEST(IoSchedulerTest, InlineModeRunsCallableOnCallerThread) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::Inline);
    folly::CPUThreadPoolExecutor compute(1);
    ThreadTrace trace;

    runOn(&compute, runDispatchValue(io, trace));

    EXPECT_EQ(trace.value, 42);
    EXPECT_EQ(trace.work, trace.caller) << "inline mode must not hop to another thread";
    EXPECT_EQ(trace.resume, trace.caller);
}

/// Same-executor mode: the callable is posted to the caller's own executor, so
/// it runs on a worker of that pool (never on the caller's thread while the
/// caller is suspended), and the caller resumes on its executor.
TEST(IoSchedulerTest, SameExecutorModeRunsOnCallerPool) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::SameExecutor);
    folly::CPUThreadPoolExecutor compute(2);
    ThreadTrace trace;

    runOn(&compute, runDispatchValue(io, trace));

    EXPECT_EQ(trace.value, 42);
    EXPECT_NE(trace.work, ThreadId{}) << "callable never ran";
    EXPECT_NE(trace.work, trace.caller) << "same-executor mode blocked the caller instead of posting";
    EXPECT_EQ(trace.resume, trace.caller) << "caller did not resume on its own executor";
}

/// Session control must stay on the storage IO pool in every mode: a commit may
/// fsync and must never occupy a query slot, even when data calls run inline.
TEST(IoSchedulerTest, SessionControlStaysOnIoPoolInInlineMode) {
    eugraph::IoScheduler io(1, eugraph::IoScheduler::DispatchMode::Inline);
    folly::CPUThreadPoolExecutor compute(1);
    ThreadTrace data;
    ThreadTrace control;

    runOn(&compute, [&]() -> folly::coro::Task<void> {
        co_await runDispatchValue(io, data);
        control.caller = std::this_thread::get_id();
        co_await io.dispatchVoidOnIoPool([&control] { control.work = std::this_thread::get_id(); });
        control.resume = std::this_thread::get_id();
    }());

    EXPECT_EQ(data.work, data.caller) << "inline mode should run data calls on the caller";
    EXPECT_NE(control.work, control.caller)
        << "session control must not run on the query thread even when data calls are inline";
    EXPECT_EQ(control.resume, control.caller);
}
