#pragma once

#include "common/config/env_vars.hpp"
#include "common/thread/numa_thread_factory.hpp"
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Task.h>

#include <folly/executors/IOThreadPoolExecutor.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>

namespace eugraph {

/// IoScheduler dispatches synchronous storage calls from coroutines. The default
/// schedule is configurable (see DispatchMode): the historical dedicated IO pool,
/// synchronous inline execution on the caller, or the caller's own executor.
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

    /// Where a storage data call runs.
    ///
    ///   Pool         - dedicated storage IO pool (historical design);
    ///   Inline       - synchronously on the calling compute thread: fewest
    ///                  instructions and best cache locality, but the caller is
    ///                  blocked for the whole call;
    ///   SameExecutor - posted to the caller's own executor (the compute pool):
    ///                  the caller is freed while the call runs, and compute and
    ///                  storage share one capacity pool (the default).
    ///
    /// The two-executor split does not prioritise fast requests: when every IO
    /// worker is busy with a slow scan, a fast request's IO task still queues
    /// behind it. What the split buys is that a query waiting for IO does not
    /// occupy a compute thread -- SameExecutor keeps that property while
    /// removing the separate IO executor (SF1 c7 4 clients: qps 406 -> 559;
    /// driver complex-only 4 threads: 4.04 -> 6.78 op/s).
    enum class DispatchMode {
        Pool,
        Inline,
        SameExecutor
    };

    /// Compile-time default from EUGRAPH_STORAGE_DISPATCH_MODE (CMake).
    static constexpr DispatchMode compiledDispatchMode() {
#if defined(EUGRAPH_STORAGE_DISPATCH_MODE_INLINE)
        return DispatchMode::Inline;
#elif defined(EUGRAPH_STORAGE_DISPATCH_MODE_POOL)
        return DispatchMode::Pool;
#else
        return DispatchMode::SameExecutor;
#endif
    }

    /// Runtime override for A/B runs: EUGRAPH_IO_MODE=pool|inline|same.
    static DispatchMode storageDispatchMode() {
        static const DispatchMode mode = [] {
            const char* v = std::getenv(config::kEnvIoMode);
            if (v != nullptr && *v != '\0') {
                const std::string_view s(v);
                if (s == "pool")
                    return DispatchMode::Pool;
                if (s == "inline")
                    return DispatchMode::Inline;
                if (s == "same")
                    return DispatchMode::SameExecutor;
            }
            return compiledDispatchMode();
        }();
        return mode;
    }

    /// Dispatch a callable returning non-void to the storage executor (see
    /// storageDispatchMode()).
    template <typename F> folly::coro::Task<std::invoke_result_t<F>> dispatch(F func) {
        return dispatchImpl(std::move(func), /*force_io_pool=*/false);
    }

    /// Session control (BEGIN/COMMIT/ROLLBACK) must stay off the query slots: a
    /// commit can fsync, which must not block the compute pool.
    template <typename F> folly::coro::Task<std::invoke_result_t<F>> dispatchOnIoPool(F func) {
        return dispatchImpl(std::move(func), /*force_io_pool=*/true);
    }

    /// Dispatch a void-returning callable (see storageDispatchMode()).
    template <typename F> folly::coro::Task<void> dispatchVoid(F func) {
        return dispatchVoidImpl(std::move(func), /*force_io_pool=*/false);
    }

    template <typename F> folly::coro::Task<void> dispatchVoidOnIoPool(F func) {
        return dispatchVoidImpl(std::move(func), /*force_io_pool=*/true);
    }

private:
    template <typename F> folly::coro::Task<std::invoke_result_t<F>> dispatchImpl(F func, bool force_io_pool) {
        using R = std::invoke_result_t<F>;
        const DispatchMode mode = force_io_pool ? DispatchMode::Pool : storageDispatchMode();
        if (mode == DispatchMode::Inline)
            co_return func();
        auto ioTask =
            folly::coro::co_invoke([f = std::move(func)]() mutable -> folly::coro::Task<R> { co_return f(); });
        if (mode == DispatchMode::SameExecutor) {
            // Post back to the calling coroutine's own executor instead of the
            // storage pool: the caller is suspended (freeing its slot) while the
            // callable runs on a compute worker.
            auto* exec = co_await folly::coro::co_current_executor;
            if (exec != nullptr)
                co_return co_await folly::coro::co_withExecutor(folly::getKeepAliveToken(exec), std::move(ioTask));
        }
        // A coroutine without an executor has nowhere to resume after the hop;
        // keep the historical inline behaviour instead of dereferencing an
        // empty KeepAlive. All production entry points are executor-bound.
        if (!(co_await folly::coro::co_current_executor))
            co_return co_await std::move(ioTask);
        co_return co_await folly::coro::co_withExecutor(io_ka_, std::move(ioTask));
    }

    template <typename F> folly::coro::Task<void> dispatchVoidImpl(F func, bool force_io_pool) {
        const DispatchMode mode = force_io_pool ? DispatchMode::Pool : storageDispatchMode();
        if (mode == DispatchMode::Inline) {
            func();
            co_return;
        }
        auto ioTask = folly::coro::co_invoke([f = std::move(func)]() mutable -> folly::coro::Task<void> {
            f();
            co_return;
        });
        if (mode == DispatchMode::SameExecutor) {
            auto* exec = co_await folly::coro::co_current_executor;
            if (exec != nullptr) {
                co_await folly::coro::co_withExecutor(folly::getKeepAliveToken(exec), std::move(ioTask));
                co_return;
            }
        }
        if (!(co_await folly::coro::co_current_executor)) {
            co_await std::move(ioTask);
            co_return;
        }
        co_await folly::coro::co_withExecutor(io_ka_, std::move(ioTask));
    }

    std::shared_ptr<folly::IOThreadPoolExecutor> io_pool_;
    folly::Executor::KeepAlive<> io_ka_;
};

} // namespace eugraph
