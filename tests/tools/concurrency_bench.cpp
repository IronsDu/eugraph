// 并发膨胀定位（下沉到存储层）：裸 getVertexProperty 在 1..N 线程下的单位成本与吞吐。
// 绕开算子、协程、线程池与调度器 —— 若这里也膨胀，原因在存储层；否则在引擎/执行器层。
//
// 方法学要点（重要）：本机 CPU 频率会漂移（可达 2x），因此**必须交错多轮、每档取 min**；
// 顺序测 1→2→4→8 会把频率漂移误判成并发下降。
//
// 用法: concurrency_bench <graph_data_dir> <label> <prop> <per_thread> [rounds]
// 同时测两个工作集档位（小/大），用于区分：
//   「小工作集（可驻留 cache）仍下降」= WT 内部争用
//   「仅大工作集下降」                = 缓存/内存带宽
#include "storage/data/sync_graph_data_store.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

using namespace eugraph;
using Clock = std::chrono::steady_clock;

namespace {

struct Config {
    int threads;
    size_t working_set; // 访问前多少个 vid
};

/// 进程级 CPU 时间（所有线程的 utime + stime，秒）。用于把"墙钟变慢"与"CPU 变多"分开：
/// 纯阻塞锁只会让墙钟变慢；CPU 上升说明有线程在自旋或做了额外工作。
double procCpuSeconds() {
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0.0;
    return static_cast<double>(ru.ru_utime.tv_sec) + static_cast<double>(ru.ru_utime.tv_usec) / 1e6 +
           static_cast<double>(ru.ru_stime.tv_sec) + static_cast<double>(ru.ru_stime.tv_usec) / 1e6;
}

struct RunResult {
    double wall_us_per_lookup;
    double cpu_us_per_lookup;
};

RunResult run_once(SyncGraphDataStore& store, LabelId label, uint16_t prop, const std::vector<VertexId>& vids,
                   const Config& cfg, size_t per_thread, bool use_txn) {
    std::atomic<size_t> hits{0};
    double cpu0 = procCpuSeconds();
    auto t0 = Clock::now();
    std::vector<std::thread> th;
    th.reserve(cfg.threads);
    for (int t = 0; t < cfg.threads; ++t) {
        th.emplace_back([&, t]() {
            GraphTxnHandle txn = use_txn ? store.beginTransaction() : INVALID_GRAPH_TXN;
            size_t local = 0;
            for (size_t i = 0; i < per_thread; ++i) {
                VertexId v = vids[(i + static_cast<size_t>(t) * 977) % cfg.working_set];
                if (store.getVertexProperty(txn, v, label, prop).has_value())
                    ++local;
            }
            if (use_txn)
                store.commitTransaction(txn);
            hits.fetch_add(local);
        });
    }
    for (auto& x : th)
        x.join();
    auto t1 = Clock::now();
    double cpu1 = procCpuSeconds();
    double total = static_cast<double>(per_thread) * cfg.threads;
    double wall = std::chrono::duration<double, std::micro>(t1 - t0).count() / total;
    double cpu = (cpu1 - cpu0) * 1e6 / total;
    return RunResult{wall, cpu};
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "/home/dodo/code/fuck/eugraph-sf0.1-fresh/graph_0/data";
    const LabelId label = argc > 2 ? static_cast<LabelId>(std::stoul(argv[2])) : 2;
    const uint16_t prop = argc > 3 ? static_cast<uint16_t>(std::stoul(argv[3])) : 1;
    const size_t per_thread = argc > 4 ? std::stoul(argv[4]) : 20000;
    const int rounds = argc > 5 ? std::stoi(argv[5]) : 3;

    SyncGraphDataStore store;
    if (!store.open(dir)) {
        std::printf("open failed\n");
        return 1;
    }

    std::vector<VertexId> all;
    all.reserve(60000);
    store.scanVerticesByLabel(INVALID_GRAPH_TXN, label, [&](VertexId v) {
        all.push_back(v);
        return all.size() < 60000;
    });
    if (all.empty()) {
        std::printf("no vids\n");
        return 1;
    }

    // 工作集档位：小（1k）与大（全部样本），用于区分 cache 争用 vs WT 内部争用
    const std::vector<size_t> working_sets = {1000, std::min<size_t>(all.size(), 50000)};
    const std::vector<int> thread_counts = {1, 2, 4, 8};

    std::printf("=== 每线程独立事务(独立 session) ===\n");
    for (size_t ws : working_sets) {
        std::printf("  工作集 %zu 个顶点:\n", ws);
        // 交错：逐轮把所有配置各跑一次，每档取 min（min 最不受频率漂移影响）
        // 同时记录 CPU（取对应那轮的 CPU，用 wall 最小的那轮）
        std::vector<double> best(thread_counts.size(), 1e18);
        std::vector<double> best_cpu(thread_counts.size(), 0.0);
        for (int r = 0; r < rounds; ++r) {
            for (size_t k = 0; k < thread_counts.size(); ++k) {
                Config cfg{thread_counts[k], ws};
                RunResult rr = run_once(store, label, prop, all, cfg, per_thread, true);
                if (rr.wall_us_per_lookup < best[k]) {
                    best[k] = rr.wall_us_per_lookup;
                    best_cpu[k] = rr.cpu_us_per_lookup;
                }
            }
        }
        for (size_t k = 0; k < thread_counts.size(); ++k) {
            std::printf("    threads=%d  每次查找 墙钟 %6.3f us  CPU %6.3f us  吞吐 %5.2f M/s", thread_counts[k],
                        best[k], best_cpu[k], 1.0 / best[k]);
            if (k > 0)
                std::printf("   vs 1线程: 墙钟 %+.0f%%  CPU %+.0f%%", (best[k] / best[0] - 1.0) * 100.0,
                            (best_cpu[k] / best_cpu[0] - 1.0) * 100.0);
            std::printf("\n");
        }
    }
    store.close();
    return 0;
}
