#pragma once

#include "common/thread/numa_thread_factory.hpp"
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Task.h>

#include <folly/executors/IOThreadPoolExecutor.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <type_traits>

namespace eugraph {

/// IoScheduler wraps folly::IOThreadPoolExecutor and provides a coroutine-friendly
/// dispatch mechanism: synchronous storage calls are scheduled on IO threads,
/// and the calling coroutine suspends until the result is ready.
///
/// NOTE: use co_withExecutor(), not co_viaIfAsync(), for a Task argument.
/// co_viaIfAsync(exec, Task) only binds the child's executor and resumes it
/// inline on the caller thread (see folly/coro/Task.h); it does not schedule.
/// co_withExecutor() returns TaskWithExecutor, whose awaiter actually posts the
/// child to the IO executor. The awaiting coroutine resumes on its own executor
/// afterwards (Task::await_transform adds the return hop).
class IoScheduler {
public:
    explicit IoScheduler(size_t num_threads = 4)
        : io_pool_(std::make_shared<folly::IOThreadPoolExecutor>(num_threads,
                                                                 eugraph::common::makeThreadFactory("eugraph-io"))),
          io_ka_(io_pool_.get()) {}

    /// Get the underlying folly executor.
    folly::Executor* executor() const {
        return io_pool_.get();
    }

    /// Dispatch a callable returning non-void to the IO thread pool.
    template <typename F> folly::coro::Task<std::invoke_result_t<F>> dispatch(F func) {
        using R = std::invoke_result_t<F>;
        auto ioTask =
            folly::coro::co_invoke([f = std::move(func)]() mutable -> folly::coro::Task<R> { co_return f(); });
        // A coroutine without an executor has nowhere to resume after the IO
        // hop; keep the historical inline behavior instead of dereferencing an
        // empty KeepAlive in the return hop. All production entry points are
        // executor-bound (Bolt / Thrift handler / blockingWait driver).
        if (!(co_await folly::coro::co_current_executor))
            co_return co_await std::move(ioTask);
        auto result = co_await folly::coro::co_withExecutor(io_ka_, std::move(ioTask));
        co_return std::move(result);
    }

    /// Dispatch a void-returning callable to the IO thread pool.
    template <typename F> folly::coro::Task<void> dispatchVoid(F func) {
        auto ioTask = folly::coro::co_invoke([f = std::move(func)]() mutable -> folly::coro::Task<void> {
            f();
            co_return;
        });
        if (!(co_await folly::coro::co_current_executor)) {
            co_await std::move(ioTask);
            co_return;
        }
        co_await folly::coro::co_withExecutor(io_ka_, std::move(ioTask));
    }

private:
    std::shared_ptr<folly::IOThreadPoolExecutor> io_pool_;
    folly::Executor::KeepAlive<> io_ka_;
};

} // namespace eugraph
