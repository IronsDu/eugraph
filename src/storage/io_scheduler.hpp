#pragma once

#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Task.h>
#include <folly/executors/IOThreadPoolExecutor.h>

#include <functional>
#include <memory>
#include <type_traits>

namespace eugraph {

/// IoScheduler wraps folly::IOThreadPoolExecutor and provides a coroutine-friendly
/// dispatch mechanism: synchronous storage calls are scheduled on IO threads,
/// and the calling coroutine suspends until the result is ready.
class IoScheduler {
public:
    explicit IoScheduler(size_t num_threads = 4)
        : io_pool_(std::make_shared<folly::IOThreadPoolExecutor>(num_threads)), io_ka_(io_pool_.get()) {}

    /// Get the underlying folly executor.
    folly::Executor* executor() const {
        return io_pool_.get();
    }

    /// Dispatch a callable returning non-void to the IO thread pool.
    template <typename F> folly::coro::Task<std::invoke_result_t<F>> dispatch(F func) {
        using R = std::invoke_result_t<F>;
        auto ioTask = folly::coro::co_invoke([f = std::move(func)]() -> folly::coro::Task<R> { co_return f(); });
        auto result = co_await folly::coro::co_viaIfAsync(io_ka_, std::move(ioTask));
        co_return std::move(result);
    }

    /// Dispatch a void-returning callable to the IO thread pool.
    template <typename F> folly::coro::Task<void> dispatchVoid(F func) {
        auto ioTask = folly::coro::co_invoke([f = std::move(func)]() -> folly::coro::Task<void> {
            f();
            co_return;
        });
        co_await folly::coro::co_viaIfAsync(io_ka_, std::move(ioTask));
    }

private:
    std::shared_ptr<folly::IOThreadPoolExecutor> io_pool_;
    folly::Executor::KeepAlive<> io_ka_;
};

} // namespace eugraph
