#include "storage/io_scheduler.hpp"

#include <benchmark/benchmark.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <wiredtiger.h>

#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <random>
#include <string>
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

folly::coro::Task<void> dispatchLoop(eugraph::IoScheduler& io, size_t n) {
    for (size_t i = 0; i < n; ++i)
        co_await io.dispatchVoid([] {});
}

class DispatchBench : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State&) override {
        io_ = std::make_unique<eugraph::IoScheduler>(1);
    }
    void TearDown(const benchmark::State&) override {
        io_.reset();
    }

protected:
    std::unique_ptr<eugraph::IoScheduler> io_;
};

BENCHMARK_F(DispatchBench, EmptyDispatchVoid)(benchmark::State& state) {
    constexpr size_t kBatch = 64;
    for (auto _ : state)
        folly::coro::blockingWait(dispatchLoop(*io_, kBatch));
    state.SetItemsProcessed(state.iterations() * kBatch);
}

} // namespace
