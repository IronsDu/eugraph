#pragma once

#include "common/config/env_vars.hpp"
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

    /// Where a storage callable runs (see the discussion in the header of
    /// docs/benchmark/ldbc-snb-concurrency.md §8):
    ///   Pool          - storage IO pool (the historical two-executor design);
    ///   Inline        - synchronously on the calling compute thread: no hop,
    ///                   no cross-core data handoff, but the call blocks that
    ///                   compute slot for its duration;
    ///   SameExecutor  - posted to the *caller's own* executor (the compute
    ///                   pool): frees the calling thread but the callable may
    ///                   land on a different compute thread, so the cross-core
    ///                   handoff (and its cache cost) remains.
    ///
    /// Compile-time default: EUGRAPH_INLINE_STORAGE -> Inline, otherwise Pool.
    /// EUGRAPH_IO_MODE=pool|inline|same overrides it per process for A/B runs.
    enum class DispatchMode {
        Pool,
        Inline,
        SameExecutor
    };

    static DispatchMode dispatchMode() {
        static const DispatchMode mode = [] {
            const char* v = std::getenv(config::kEnvIoMode);
            if (v != nullptr && *v != '\0') {
                const std::string_view s(v);
                if (s == "inline" || s == "1")
                    return DispatchMode::Inline;
                if (s == "same" || s == "2")
                    return DispatchMode::SameExecutor;
                return DispatchMode::Pool;
            }
#ifdef EUGRAPH_INLINE_STORAGE
            return DispatchMode::Inline;
#else
            return DispatchMode::Pool;
#endif
        }();
        return mode;
    }

    /// Dispatch a callable returning non-void to the IO thread pool.
    template <typename F> folly::coro::Task<std::invoke_result_t<F>> dispatch(F func) {
        using R = std::invoke_result_t<F>;
        const DispatchMode mode = dispatchMode();
        if (mode == DispatchMode::Inline)
            co_return func();
        auto ioTask =
            folly::coro::co_invoke([f = std::move(func)]() mutable -> folly::coro::Task<R> { co_return f(); });
        if (mode == DispatchMode::SameExecutor) {
            // Post back to the calling coroutine's own executor instead of the
            // storage pool: capacity is shared with compute, but the callable
            // usually runs on another thread (cache handoff stays).
            auto* exec = co_await folly::coro::co_current_executor;
            if (exec == nullptr)
                co_return co_await std::move(ioTask);
            co_return co_await folly::coro::co_withExecutor(folly::getKeepAliveToken(exec), std::move(ioTask));
        }
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
        const DispatchMode mode = dispatchMode();
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
            if (exec == nullptr) {
                co_await std::move(ioTask);
                co_return;
            }
            co_await folly::coro::co_withExecutor(folly::getKeepAliveToken(exec), std::move(ioTask));
            co_return;
        }
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
