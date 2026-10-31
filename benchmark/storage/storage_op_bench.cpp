#include "storage/io_scheduler.hpp"
#include "storage/kv/index_key_codec.hpp"
#include "storage/kv/key_codec.hpp"

#include <benchmark/benchmark.h>
#include <folly/coro/BlockingWait.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/coro/Task.h>
#include <wiredtiger.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

void setItem(WT_CURSOR* cursor, const std::string& data) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = data.data();
    item.size = data.size();
    cursor->set_key(cursor, &item);
}

void setValue(WT_CURSOR* cursor, const std::string& data) {
    WT_ITEM item;
    std::memset(&item, 0, sizeof(item));
    item.data = data.data();
    item.size = data.size();
    cursor->set_value(cursor, &item);
}

std::string makeKey(uint64_t id, uint64_t seq) {
    std::string key(27, '\0');
    for (int i = 0; i < 8; ++i)
        key[i] = static_cast<char>((id >> ((7 - i) * 8)) & 0xFF);
    key[8] = 0; // OUT
    key[9] = 0;
    key[10] = 0;
    for (int i = 0; i < 8; ++i)
        key[11 + i] = static_cast<char>((seq >> ((7 - i) * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i)
        key[19 + i] = static_cast<char>((seq >> ((7 - i) * 8)) & 0xFF);
    return key;
}

class WtSeekBench : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State&) override {
        dir_ = std::filesystem::current_path() / ("wt-op-bench-" + std::to_string(getpid()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        if (wiredtiger_open(dir_.c_str(), nullptr, "create,cache_size=512MB", &conn_) != 0)
            throw std::runtime_error("wiredtiger_open failed");
        if (conn_->open_session(conn_, nullptr, nullptr, &session_) != 0)
            throw std::runtime_error("open_session failed");
        if (session_->create(session_, "table:bench", "key_format=u,value_format=u") != 0)
            throw std::runtime_error("create table failed");
        if (session_->open_cursor(session_, "table:bench", nullptr, nullptr, &cursor_) != 0)
            throw std::runtime_error("open_cursor failed");

        constexpr uint64_t kRows = 100000;
        for (uint64_t i = 0; i < kRows; ++i) {
            const auto key = makeKey(i / 10, i);
            setItem(cursor_, key);
            setValue(cursor_, std::string(8, '\0'));
            if (cursor_->insert(cursor_) != 0)
                throw std::runtime_error("insert failed");
        }
        cursor_->reset(cursor_);
        keys_.reserve(1024);
        for (uint64_t i = 0; i < 1024; ++i)
            keys_.push_back(makeKey((i * 997) % (kRows / 10), (i * 997) % kRows));
        prefix_ = keys_.front().substr(0, 8);
    }

    void TearDown(const benchmark::State&) override {
        if (cursor_)
            cursor_->close(cursor_);
        if (session_)
            session_->close(session_, nullptr);
        if (conn_)
            conn_->close(conn_, nullptr);
        std::filesystem::remove_all(dir_);
    }

protected:
    std::filesystem::path dir_;
    WT_CONNECTION* conn_ = nullptr;
    WT_SESSION* session_ = nullptr;
    WT_CURSOR* cursor_ = nullptr;
    std::vector<std::string> keys_;
    std::string prefix_;
};

BENCHMARK_F(WtSeekBench, SearchExact)(benchmark::State& state) {
    size_t i = 0;
    for (auto _ : state) {
        setItem(cursor_, keys_[i++ % keys_.size()]);
        int ret = cursor_->search(cursor_);
        benchmark::DoNotOptimize(ret);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(WtSeekBench, SearchNearPrefix)(benchmark::State& state) {
    for (auto _ : state) {
        setItem(cursor_, prefix_);
        int cmp = 0;
        int ret = cursor_->search_near(cursor_, &cmp);
        benchmark::DoNotOptimize(ret);
        benchmark::DoNotOptimize(cmp);
    }
    state.SetItemsProcessed(state.iterations());
}

folly::coro::Task<void> dispatchLoop(eugraph::IoScheduler& io, size_t n, std::atomic<uint64_t>& cross_thread,
                                     std::atomic<uint64_t>& return_thread) {
    const auto caller = std::this_thread::get_id();
    for (size_t i = 0; i < n; ++i) {
        co_await io.dispatchVoid([&cross_thread, caller] {
            if (std::this_thread::get_id() != caller)
                cross_thread.fetch_add(1, std::memory_order_relaxed);
        });
        if (std::this_thread::get_id() != caller)
            return_thread.fetch_add(1, std::memory_order_relaxed);
    }
}

/// The pre-2026-10-31 production shape: `co_viaIfAsync(executor, Task)` only
/// binds the child's executor and resumes it inline on the caller thread.
/// Kept for same-binary A/B; not the production path anymore.
folly::coro::Task<void> dispatchLegacyInlineLoop(eugraph::IoScheduler& io, size_t n,
                                                 std::atomic<uint64_t>& cross_thread,
                                                 std::atomic<uint64_t>& return_thread) {
    const auto caller = std::this_thread::get_id();
    for (size_t i = 0; i < n; ++i) {
        auto ioTask = folly::coro::co_invoke([&cross_thread, caller]() -> folly::coro::Task<void> {
            if (std::this_thread::get_id() != caller)
                cross_thread.fetch_add(1, std::memory_order_relaxed);
            co_return;
        });
        co_await folly::coro::co_viaIfAsync(folly::getKeepAliveToken(io.executor()), std::move(ioTask));
        if (std::this_thread::get_id() != caller)
            return_thread.fetch_add(1, std::memory_order_relaxed);
    }
}

/// Reference cross-thread dispatch: `co_withExecutor` binds the child task to
/// the IO executor and, when it completes, resumes the awaiting coroutine on
/// its original executor; no manual back-hop is needed.
folly::coro::Task<void> dispatchCrossLoop(eugraph::IoScheduler& io, size_t n, std::atomic<uint64_t>& cross_thread,
                                          std::atomic<uint64_t>& return_thread) {
    const auto caller = std::this_thread::get_id();
    for (size_t i = 0; i < n; ++i) {
        co_await folly::coro::co_withExecutor(
            folly::getKeepAliveToken(io.executor()),
            folly::coro::co_invoke([&cross_thread, caller]() -> folly::coro::Task<void> {
                if (std::this_thread::get_id() != caller)
                    cross_thread.fetch_add(1, std::memory_order_relaxed);
                co_return;
            }));
        if (std::this_thread::get_id() != caller)
            return_thread.fetch_add(1, std::memory_order_relaxed);
    }
}

class DispatchBench : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State&) override {
        io_ = std::make_unique<eugraph::IoScheduler>(1);
        cpu_ = std::make_unique<folly::CPUThreadPoolExecutor>(1);
    }
    void TearDown(const benchmark::State&) override {
        cpu_.reset();
        io_.reset();
    }

protected:
    std::unique_ptr<eugraph::IoScheduler> io_;
    std::unique_ptr<folly::CPUThreadPoolExecutor> cpu_;
};

BENCHMARK_F(DispatchBench, LegacyInlineDispatchVoid)(benchmark::State& state) {
    constexpr size_t kBatch = 64;
    std::atomic<uint64_t> cross_thread{0};
    std::atomic<uint64_t> return_thread{0};
    for (auto _ : state) {
        auto task = dispatchLegacyInlineLoop(*io_, kBatch, cross_thread, return_thread);
        folly::coro::blockingWait(folly::coro::co_withExecutor(cpu_.get(), std::move(task)));
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
    state.counters["cross_thread"] = static_cast<double>(cross_thread.load(std::memory_order_relaxed));
    state.counters["return_thread"] = static_cast<double>(return_thread.load(std::memory_order_relaxed));
}

BENCHMARK_F(DispatchBench, ProductionDispatchVoid)(benchmark::State& state) {
    constexpr size_t kBatch = 64;
    std::atomic<uint64_t> cross_thread{0};
    std::atomic<uint64_t> return_thread{0};
    for (auto _ : state) {
        auto task = dispatchLoop(*io_, kBatch, cross_thread, return_thread);
        folly::coro::blockingWait(folly::coro::co_withExecutor(cpu_.get(), std::move(task)));
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
    state.counters["cross_thread"] = static_cast<double>(cross_thread.load(std::memory_order_relaxed));
    state.counters["return_thread"] = static_cast<double>(return_thread.load(std::memory_order_relaxed));
}

BENCHMARK_F(DispatchBench, CrossThreadCoroutineDispatch)(benchmark::State& state) {
    constexpr size_t kBatch = 64;
    std::atomic<uint64_t> cross_thread{0};
    std::atomic<uint64_t> return_thread{0};
    const auto start = std::chrono::steady_clock::now();
    for (auto _ : state) {
        auto task = dispatchCrossLoop(*io_, kBatch, cross_thread, return_thread);
        folly::coro::blockingWait(folly::coro::co_withExecutor(cpu_.get(), std::move(task)));
    }
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    const auto ops = state.iterations() * kBatch;
    state.SetItemsProcessed(ops);
    state.counters["ns_per_dispatch"] = ops == 0 ? 0.0 : static_cast<double>(elapsed_ns) / static_cast<double>(ops);
    state.counters["cross_thread"] = static_cast<double>(cross_thread.load(std::memory_order_relaxed));
    state.counters["return_thread"] = static_cast<double>(return_thread.load(std::memory_order_relaxed));
}


// ==================== Key decode cost ====================

namespace {

std::string cycleKey(const std::vector<std::string>& keys, size_t& i) {
    return keys[i++ % keys.size()];
}

} // namespace

static void KeyDecode_LabelForward(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i)
        keys.push_back(eugraph::KeyCodec::encodeLabelForwardKey(0x100000000ULL + i));
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::KeyCodec::decodeLabelForwardKey(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

static void KeyDecode_VertexExistence(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i)
        keys.push_back(eugraph::KeyCodec::encodeVertexExistenceKey(0x200000000ULL + i));
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::KeyCodec::decodeVertexExistenceKey(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

static void KeyDecode_VertexDataLabel(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i)
        keys.push_back(eugraph::KeyCodec::encodeVertexDataLabelKey(0x300000000ULL + i, static_cast<eugraph::LabelId>(i)));
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::KeyCodec::decodeVertexDataLabelKey(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

static void KeyDecode_VertexDataProp(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i)
        keys.push_back(eugraph::KeyCodec::encodeVertexDataPropKey(
            0x400000000ULL + i, static_cast<eugraph::LabelId>(i % 8), static_cast<uint16_t>(i % 16)));
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::KeyCodec::decodeVertexDataPropKey(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

static void KeyDecode_VertexDataAdjEdge(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i) {
        eugraph::KeyCodec::VertexDataAdjEdgeKey k{0x500000000ULL + i, eugraph::Direction::OUT,
                                                  static_cast<eugraph::EdgeLabelId>(i % 8),
                                                  0x600000000ULL + i, i};
        keys.push_back(eugraph::KeyCodec::encodeVertexDataAdjEdgeKey(k));
    }
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::KeyCodec::decodeVertexDataAdjEdgeKey(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

static void KeyDecode_EdgeIndex(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i) {
        eugraph::KeyCodec::EdgeIndexKey k{0x700000000ULL + i, eugraph::Direction::IN,
                                          static_cast<eugraph::EdgeLabelId>(i % 8), 0x800000000ULL + i, i};
        keys.push_back(eugraph::KeyCodec::encodeEdgeIndexKey(k));
    }
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::KeyCodec::decodeEdgeIndexKey(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

static void KeyDecode_EdgeType(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i) {
        eugraph::KeyCodec::EdgeTypeIndexKey k{0x900000000ULL + i, 0xA00000000ULL + i, i};
        keys.push_back(eugraph::KeyCodec::encodeEdgeTypeIndexKey(k));
    }
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::KeyCodec::decodeEdgeTypeIndexKey(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

static void KeyDecode_EProp(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i)
        keys.push_back(eugraph::KeyCodec::encodeEPropKey(0xB00000000ULL + i, static_cast<uint16_t>(i % 16)));
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::KeyCodec::decodeEPropKey(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}

static void KeyDecode_IndexSuffix(benchmark::State& state) {
    std::vector<std::string> keys;
    for (uint64_t i = 0; i < 1024; ++i)
        keys.push_back(eugraph::IndexKeyCodec::encodeIndexKey(static_cast<int64_t>(i * 17), 0xC00000000ULL + i));
    size_t i = 0;
    for (auto _ : state) {
        auto v = eugraph::IndexKeyCodec::decodeEntityId(cycleKey(keys, i));
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}


BENCHMARK(KeyDecode_LabelForward);
BENCHMARK(KeyDecode_VertexExistence);
BENCHMARK(KeyDecode_VertexDataLabel);
BENCHMARK(KeyDecode_VertexDataProp);
BENCHMARK(KeyDecode_VertexDataAdjEdge);
BENCHMARK(KeyDecode_EdgeIndex);
BENCHMARK(KeyDecode_EdgeType);
BENCHMARK(KeyDecode_EProp);
BENCHMARK(KeyDecode_IndexSuffix);

} // namespace
