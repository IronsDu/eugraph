// POC-1 / POC-2（见 docs/storage/session-cursor-ownership.md §5）：
// 量"cursor 每次开关"在一次点查里占多少，以及"扫描期复用一把 cursor"能省多少。
//
// 对照（同一批顶点、同一属性、同为点查）：
//   A: N × getVertexProperty        —— 每次调用 openCursor + search + close（现状）
//   B: 1 × pocPointGetBatchReuseCursor —— 一次 openCursor + N 次 search（候选方案）
// 两者每次查找的 CPU 时间差 ≈ cursor 开关的开销（也是 POC-2 的收益上限）。
//
// 手工编译（不在 CMake 目标里）：
//   L=build/release/CMakeFiles/data_chunk_tests.dir/link.txt
//   LIBS=$(tr ' ' '\n' < $L | grep -E '\.a$' | grep -v libeugraph | sed 's|^|build/release/|')
//   g++ -std=c++20 -O2 -I src -I build/release/vcpkg_installed/x64-linux/include \
//       -I build/release/wiredtiger-install/include tests/tools/poc_cursor_reuse.cpp \
//       -o /tmp/poc_cursor -Wl,--start-group build/release/libeugraph_{query_engine,lib,metadata,compute}.a \
//       -Wl,--end-group $LIBS -ldl -lpthread -lgcc_s
//
// ⚠️ 服务端必须先停（同一 WT 库不能两个进程同时打开）；数据根目录指向 <graph>/data。
#include "storage/data/sync_graph_data_store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <sys/resource.h>
#include <vector>

using namespace eugraph;
using Clock = std::chrono::steady_clock;

namespace {
double procCpuSeconds() {
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0.0;
    return static_cast<double>(ru.ru_utime.tv_sec) + ru.ru_utime.tv_usec / 1e6 +
           static_cast<double>(ru.ru_stime.tv_sec) + ru.ru_stime.tv_usec / 1e6;
}
} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "/home/dodo/code/fuck/eugraph-sf0.1-fresh/graph_0/data";
    const LabelId label = argc > 2 ? static_cast<LabelId>(std::stoul(argv[2])) : 2;
    const uint16_t prop = argc > 3 ? static_cast<uint16_t>(std::stoul(argv[3])) : 1;
    const size_t n = argc > 4 ? std::stoul(argv[4]) : 20000;
    const int rounds = argc > 5 ? std::stoi(argv[5]) : 3;

    SyncGraphDataStore store;
    if (!store.open(dir)) {
        std::printf("open failed: %s\n", dir.c_str());
        return 1;
    }

    std::vector<VertexId> vids;
    vids.reserve(n);
    store.scanVerticesByLabel(INVALID_GRAPH_TXN, label, [&](VertexId v) {
        vids.push_back(v);
        return vids.size() < n;
    });
    if (vids.empty()) {
        std::printf("no vids\n");
        return 1;
    }
    std::printf("label=%u prop=%u vids=%zu rounds=%d\n", label, prop, vids.size(), rounds);

    // 交错多轮取 min（本机 CPU 频率会漂移，顺序测量会把漂移误判为差异）
    double best_a = 1e18, best_b = 1e18;
    size_t hits_a = 0, hits_b = 0;
    for (int r = 0; r < rounds; ++r) {
        {
            size_t hits = 0;
            const double c0 = procCpuSeconds();
            auto t0 = Clock::now();
            for (VertexId v : vids)
                if (store.getVertexProperty(INVALID_GRAPH_TXN, v, label, prop).has_value())
                    ++hits;
            auto t1 = Clock::now();
            const double cpu = (procCpuSeconds() - c0) * 1e6 / vids.size();
            const double wall = std::chrono::duration<double, std::micro>(t1 - t0).count() / vids.size();
            best_a = std::min(best_a, cpu);
            hits_a = hits;
            std::printf("  A 每次开关 cursor : wall %6.3f us  cpu %6.3f us  (hits=%zu)\n", wall, cpu, hits);
        }
        {
            const double c0 = procCpuSeconds();
            auto t0 = Clock::now();
            auto out = store.pocPointGetBatchReuseCursor(INVALID_GRAPH_TXN, label, prop, vids);
            auto t1 = Clock::now();
            const size_t hits = std::count_if(out.begin(), out.end(), [](const auto& o) { return o.has_value(); });
            const double cpu = (procCpuSeconds() - c0) * 1e6 / vids.size();
            const double wall = std::chrono::duration<double, std::micro>(t1 - t0).count() / vids.size();
            best_b = std::min(best_b, cpu);
            hits_b = hits;
            std::printf("  B 复用一把 cursor : wall %6.3f us  cpu %6.3f us  (hits=%zu)\n", wall, cpu, hits);
        }
    }

    std::printf("\n  min: A=%.3f us  B=%.3f us  => cursor 开关占单次查找的 %.1f%%（省 %.3f us/次）\n", best_a, best_b,
                best_a > 0 ? (best_a - best_b) / best_a * 100.0 : 0.0, best_a - best_b);
    if (hits_a != hits_b)
        std::printf("  ⚠️ 两者命中数不同（%zu vs %zu）——结果不可比，先修正确性\n", hits_a, hits_b);
    store.close();
    return 0;
}
