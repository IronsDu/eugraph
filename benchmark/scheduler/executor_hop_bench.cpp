// Micro-benchmark for the cost of a two-executor hop under concurrency.
//
// Shape:
//   starts on compute (1 worker)
//     -> IoScheduler::dispatch -> IO executor (1 worker)
//     -> returns to compute
//
// This deliberately uses one worker per pool: it isolates the fixed
// enqueue/wakeup/context-switch cost from intra-pool parallelism. With N
// requests in flight, the two single-thread pools can still pipeline, but
// there is no second worker to hide a per-task cost.
//
// Run:
//   ./executor_hop_bench
//   ./executor_hop_bench --concurrency 1,2,4,8,16,32,64,128 --hops 2000
//   ./executor_hop_bench --batch 16 --compute-work 2000 --io-work 2000
//
// Columns:
//   conc        number of in-flight requests
//   hops        completed compute->IO->compute round trips
//   hops/s      round trips per second
//   ops/s       useful IO units per second (hops * batch / wall)
//   p50/p99     per-round-trip latency in microseconds (includes queueing)
//
// Note: the latency sample is one full round trip (compute pre-work + IO work +
// compute post-work), not just the enqueue call.

#include "common/thread/numa_thread_factory.hpp"
#include "storage/io_scheduler.hpp"

#include <folly/Try.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/Executor.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using eugraph::IoScheduler;

inline void spin(uint64_t iterations) {
    uint64_t x = iterations + 0x9e3779b97f4a7c15ULL;
    for (uint64_t i = 0; i < iterations; ++i) {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    }
    // Keep the loop observable.
    asm volatile("" : "+r"(x) : : "memory");
}

struct HopSamples {
    std::vector<uint64_t> latency_ns;
    uint64_t ops = 0;
};

folly::coro::Task<HopSamples> runRequest(IoScheduler& io, int hops, int batch, uint64_t compute_work,
                                         uint64_t io_work) {
    HopSamples samples;
    samples.latency_ns.reserve(static_cast<size_t>(hops));
    samples.ops = 0;

    for (int i = 0; i < hops; ++i) {
        const auto start = Clock::now();
        spin(compute_work);
        co_await io.dispatch([batch, io_work]() {
            for (int b = 0; b < batch; ++b) {
                spin(io_work);
            }
            return 0;
        });
        spin(compute_work);
        const auto end = Clock::now();
        samples.latency_ns.push_back(
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        samples.ops += static_cast<uint64_t>(batch);
    }
    co_return samples;
}

struct LevelResult {
    int concurrency = 0;
    uint64_t hops = 0;
    uint64_t ops = 0;
    double wall_ms = 0;
    double hop_per_s = 0;
    double ops_per_s = 0;
    double p50_us = 0;
    double p99_us = 0;
};

LevelResult runLevel(IoScheduler& io, folly::CPUThreadPoolExecutor& compute, int concurrency, int hops, int batch,
                     uint64_t compute_work, uint64_t io_work) {
    std::vector<HopSamples> results(static_cast<size_t>(concurrency));
    std::atomic<int> remaining{concurrency};
    std::promise<void> done;
    auto done_future = done.get_future();

    const auto wall_start = Clock::now();
    for (int i = 0; i < concurrency; ++i) {
        auto task = runRequest(io, hops, batch, compute_work, io_work);
        folly::coro::co_withExecutor(folly::getKeepAliveToken(compute), std::move(task))
            .start([&, i](folly::Try<HopSamples>&& result) {
                if (result.hasValue()) {
                    results[static_cast<size_t>(i)] = std::move(*result);
                } else {
                    std::fprintf(stderr, "request %d failed: %s\n", i, result.exception().what().c_str());
                    std::abort();
                }
                if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    done.set_value();
                }
            });
    }
    done_future.wait();
    const auto wall_end = Clock::now();

    LevelResult level;
    level.concurrency = concurrency;
    level.wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    level.hops = static_cast<uint64_t>(concurrency) * static_cast<uint64_t>(hops);

    std::vector<uint64_t> latencies;
    latencies.reserve(static_cast<size_t>(level.hops));
    for (auto& result : results) {
        level.ops += result.ops;
        latencies.insert(latencies.end(), result.latency_ns.begin(), result.latency_ns.end());
    }
    std::sort(latencies.begin(), latencies.end());
    if (!latencies.empty()) {
        const size_t p50 = latencies.size() / 2;
        const size_t p99 = std::min(latencies.size() - 1, latencies.size() * 99 / 100);
        level.p50_us = static_cast<double>(latencies[p50]) / 1000.0;
        level.p99_us = static_cast<double>(latencies[p99]) / 1000.0;
    }
    level.hop_per_s = static_cast<double>(level.hops) / (level.wall_ms / 1000.0);
    level.ops_per_s = static_cast<double>(level.ops) / (level.wall_ms / 1000.0);
    return level;
}

void runWarmup(IoScheduler& io, folly::CPUThreadPoolExecutor& compute, int concurrency, int hops, int batch,
               uint64_t compute_work, uint64_t io_work) {
    std::atomic<int> remaining{concurrency};
    std::promise<void> done;
    auto done_future = done.get_future();
    for (int i = 0; i < concurrency; ++i) {
        auto task = runRequest(io, hops, batch, compute_work, io_work);
        folly::coro::co_withExecutor(folly::getKeepAliveToken(compute), std::move(task))
            .start([&](folly::Try<HopSamples>&& result) {
                if (!result.hasValue()) {
                    std::fprintf(stderr, "warmup failed: %s\n", result.exception().what().c_str());
                    std::abort();
                }
                if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    done.set_value();
                }
            });
    }
    done_future.wait();
}

std::vector<int> parseList(const char* text) {
    std::vector<int> values;
    const char* p = text;
    while (*p != '\0') {
        char* end = nullptr;
        long value = std::strtol(p, &end, 10);
        if (end == p || value <= 0 || value > 4096) {
            std::fprintf(stderr, "invalid concurrency list: %s\n", text);
            std::exit(2);
        }
        values.push_back(static_cast<int>(value));
        p = end;
        if (*p == ',') {
            ++p;
        } else if (*p != '\0') {
            std::fprintf(stderr, "invalid concurrency list: %s\n", text);
            std::exit(2);
        }
    }
    if (values.empty()) {
        std::fprintf(stderr, "empty concurrency list\n");
        std::exit(2);
    }
    return values;
}

bool takeArg(int argc, char** argv, int& i, const char* name, const char*& value) {
    if (std::strcmp(argv[i], name) != 0) {
        return false;
    }
    if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", name);
        std::exit(2);
    }
    value = argv[++i];
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const char* concurrency_arg = "1,2,4,8,16,32,64,128";
    int hops = 2000;
    int batch = 1;
    int warmup_hops = 200;
    uint64_t compute_work = 2000;
    uint64_t io_work = 2000;

    for (int i = 1; i < argc; ++i) {
        const char* value = nullptr;
        if (takeArg(argc, argv, i, "--concurrency", value)) {
            concurrency_arg = value;
        } else if (takeArg(argc, argv, i, "--hops", value)) {
            hops = std::atoi(value);
        } else if (takeArg(argc, argv, i, "--batch", value)) {
            batch = std::atoi(value);
        } else if (takeArg(argc, argv, i, "--warmup-hops", value)) {
            warmup_hops = std::atoi(value);
        } else if (takeArg(argc, argv, i, "--compute-work", value)) {
            compute_work = static_cast<uint64_t>(std::strtoull(value, nullptr, 10));
        } else if (takeArg(argc, argv, i, "--io-work", value)) {
            io_work = static_cast<uint64_t>(std::strtoull(value, nullptr, 10));
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: %s [--concurrency 1,2,4,...] [--hops N] [--batch N] "
                        "[--compute-work N] [--io-work N] [--warmup-hops N]\n",
                        argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }
    if (hops <= 0 || batch <= 0 || warmup_hops < 0) {
        std::fprintf(stderr, "hops/batch must be positive; warmup-hops >= 0\n");
        return 2;
    }

    // Exactly one worker on each executor.
    eugraph::IoScheduler io(1);
    folly::CPUThreadPoolExecutor compute(1, eugraph::common::makeThreadFactory("bench-compute"));
    const auto levels = parseList(concurrency_arg);

    runWarmup(io, compute, 1, warmup_hops, batch, compute_work, io_work);
    runWarmup(io, compute, std::min(8, levels.back()), warmup_hops, batch, compute_work, io_work);

    std::printf("compute_workers=1 io_workers=1 hops_per_request=%d batch=%d "
                "compute_work=%llu io_work=%llu\n",
                hops, batch, static_cast<unsigned long long>(compute_work),
                static_cast<unsigned long long>(io_work));
    std::printf("%6s %10s %12s %12s %10s %10s\n", "conc", "hops", "hops/s", "ops/s", "p50_us", "p99_us");
    for (int concurrency : levels) {
        const auto result = runLevel(io, compute, concurrency, hops, batch, compute_work, io_work);
        std::printf("%6d %10llu %12.0f %12.0f %10.1f %10.1f\n", result.concurrency,
                    static_cast<unsigned long long>(result.hops), result.hop_per_s, result.ops_per_s, result.p50_us,
                    result.p99_us);
    }
    return 0;
}
