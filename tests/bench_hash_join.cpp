// HashJoin A/B benchmark: same binary, same data, interleaved runs.
//   EUGRAPH_BENCH_N=1000 ./build/release/bench_hash_join
// Not part of ctest; run manually before perf-sensitive changes.

#include "query/executor/query_executor.hpp"
#include "test_chunk_helpers.hpp"
#include "test_index_env.hpp"

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace eugraph;
using namespace eugraph::test;

namespace {

struct ExecResult {
    std::string error;
    std::vector<std::vector<Value>> rows;
};

ExecResult execSync(compute::QueryExecutor& executor, const std::string& query) {
    auto ctx = folly::coro::blockingWait(executor.prepareStream(query));
    ExecResult result;
    if (!ctx->error.empty()) {
        result.error = std::move(ctx->error);
        return result;
    }
    auto gen = std::move(ctx->gen);
    folly::coro::blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        try {
            while (auto chunk = co_await gen.next()) {
                auto rows = chunkToRows(*chunk);
                for (auto& row : rows)
                    result.rows.push_back(std::move(row));
            }
            if (ctx->should_commit)
                co_await ctx->commitAsync();
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        co_return;
    }));
    return result;
}

void createLabel(TestEnv& env, const std::string& name) {
    auto lid = folly::coro::blockingWait(
        env.async_meta->createLabel(name, {{0, "name", PropertyType::STRING, false, std::nullopt}}));
    if (lid == INVALID_LABEL_ID)
        throw std::runtime_error("createLabel failed: " + name);
    if (!folly::coro::blockingWait(env.async_data->createLabel(lid)))
        throw std::runtime_error("createLabel data table failed: " + name);
}

void insertVertices(TestEnv& env, LabelId lid, int start_id, int n, const std::string& var) {
    auto lease = env.session_pool->acquire();
    if (!lease)
        throw std::runtime_error("no session");
    if (!(**lease).begin_write())
        throw std::runtime_error("begin_write failed");
    auto store = env.async_data->for_session(**lease);
    for (int i = 0; i < n; ++i) {
        std::vector<std::pair<LabelId, Properties>> label_props;
        label_props.emplace_back(lid, Properties{PropertyValue(var + "_" + std::to_string(i))});
        if (!folly::coro::blockingWait(store.insertVertex(
                static_cast<VertexId>(start_id + i), std::span<const std::pair<LabelId, Properties>>{label_props}))) {
            throw std::runtime_error("insertVertex failed");
        }
    }
    if ((**lease).commit().state != TxnState::Committed)
        throw std::runtime_error("commit failed");
    lease.reset();
}

double percentile(std::vector<double> v, double p) {
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
    return v[idx];
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    const int n = std::getenv("EUGRAPH_BENCH_N") ? std::atoi(std::getenv("EUGRAPH_BENCH_N")) : 1000;
    if (n <= 0) {
        std::cerr << "EUGRAPH_BENCH_N must be positive\n";
        return 2;
    }

    TestEnv env;
    env.init("hash_join_bench");
    createLabel(env, "Person");
    createLabel(env, "City");
    // Label ids are allocated in creation order (1, 2); resolve via schema snapshot.
    const auto schema = env.async_meta->schemaSnapshot();
    LabelId person = INVALID_LABEL_ID;
    LabelId city = INVALID_LABEL_ID;
    for (const auto& [lid, def] : schema.labels) {
        if (def.name == "Person")
            person = lid;
        if (def.name == "City")
            city = lid;
    }
    if (person == INVALID_LABEL_ID || city == INVALID_LABEL_ID) {
        std::cerr << "label lookup failed\n";
        return 2;
    }

    insertVertices(env, person, 1, n, "same");
    insertVertices(env, city, n + 1, n, "same");

    compute::QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setStatsStore(env.stats_store.get());

    const std::string query = "MATCH (p:Person), (c:City) WHERE p.name = c.name RETURN count(*) AS c";
    auto run = [&](const char* max_build_rows) -> double {
        ScopedEnvVar env_var("EUGRAPH_HASH_JOIN_MAX_BUILD_ROWS", max_build_rows);
        auto explain = execSync(executor, "EXPLAIN " + query);
        std::string plan;
        for (const auto& row : explain.rows) {
            if (!row.empty() && std::holds_alternative<std::string>(row[0]))
                plan += std::get<std::string>(row[0]);
        }
        if (max_build_rows[0] == '0' && plan.find("HashJoin") != std::string::npos)
            throw std::runtime_error("expected CrossProduct fallback, got HashJoin");
        if (max_build_rows[0] != '0' && plan.find("HashJoin") == std::string::npos)
            throw std::runtime_error("expected HashJoin, got plan: " + plan);
        auto t0 = std::chrono::steady_clock::now();
        auto result = execSync(executor, query);
        auto t1 = std::chrono::steady_clock::now();
        if (!result.error.empty())
            throw std::runtime_error(result.error);
        if (result.rows.size() != 1 || std::get<int64_t>(result.rows[0][0]) != n)
            throw std::runtime_error("unexpected result");
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    // Warmup both paths.
    run("1000000");
    run("0");
    std::vector<double> hash_ms;
    std::vector<double> cross_ms;
    constexpr int kRuns = 7;
    for (int i = 0; i < kRuns; ++i) {
        hash_ms.push_back(run("1000000"));
        cross_ms.push_back(run("0"));
    }
    auto report = [](const char* name, const std::vector<double>& v) {
        std::cout << name << " min=" << percentile(v, 0.0) << "ms p50=" << percentile(v, 0.5)
                  << "ms max=" << percentile(v, 1.0) << "ms\n";
    };
    std::cout << "n=" << n << " runs=" << kRuns << "\n";
    report("hashjoin", hash_ms);
    report("cross", cross_ms);
    env.shutdown();
    return 0;
}
