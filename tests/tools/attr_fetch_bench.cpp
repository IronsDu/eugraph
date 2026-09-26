// 逐层量属性取法的单位成本（直连 WT，绕开算子/协程）：
//   A 单属性点查  getVertexProperty
//   B 全属性前缀扫描 getVertexProperties
//   C 逐顶点扫描全部标签表（模拟"每顶点多次取"的上限）
// 用法: attr_fetch_bench <graph_data_dir> [sample]
#include "storage/data/sync_graph_data_store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace eugraph;
using Clock = std::chrono::steady_clock;
static double us(Clock::time_point a, Clock::time_point b, size_t n) {
    return std::chrono::duration<double, std::micro>(b - a).count() / (n ? n : 1);
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "/home/dodo/code/fuck/eugraph-sf0.1-fresh/graph_0/data";
    const size_t sample = argc > 2 ? std::stoul(argv[2]) : 20000;
    SyncGraphDataStore store;
    if (!store.open(dir)) { std::printf("open failed: %s\n", dir.c_str()); return 1; }

    // 确定性定位 Message：行数 ≈ 286744 **且** prop=1 有命中（creationDate）
    LabelId msg = INVALID_LABEL_ID;
    for (LabelId lid = 1; lid <= 32; ++lid) {
        std::vector<VertexId> probe; 
        store.scanVerticesByLabel(INVALID_GRAPH_TXN, lid, [&](VertexId v) { if (probe.size() < 50) probe.push_back(v); return probe.size() < 50; });
        if (probe.empty()) continue;
        size_t n = 0;
        store.scanVerticesByLabel(INVALID_GRAPH_TXN, lid, [&](VertexId) { ++n; return true; });
        if (n < 100000) continue;
        size_t hits = 0;
        for (VertexId v : probe) if (store.getVertexProperty(INVALID_GRAPH_TXN, v, lid, 1).has_value()) ++hits;
        std::printf("  candidate label=%u rows=%zu prop1_hits=%zu/%zu\n", lid, n, hits, probe.size());
        if (hits > 0 && msg == INVALID_LABEL_ID) msg = lid;
    }
    if (msg == INVALID_LABEL_ID) { std::printf("Message label not found\n"); return 1; }
    std::printf("=> Message label = %u\n", msg);

    std::vector<VertexId> vids; vids.reserve(sample);
    size_t scanned = 0;
    auto t0 = Clock::now();
    store.scanVerticesByLabel(INVALID_GRAPH_TXN, msg, [&](VertexId v) {
        ++scanned; if (vids.size() < sample) vids.push_back(v); return true;
    });
    auto t1 = Clock::now();
    std::printf("  [baseline] 标签扫描        %zu 行  %.3f us/行\n", scanned, us(t0, t1, scanned));

    // A 单属性点查（prop 1 若命中；再试 0..4 找命中的）
    for (uint16_t pid = 0; pid < 5; ++pid) {
        size_t hits = 0;
        t0 = Clock::now();
        for (VertexId v : vids) if (store.getVertexProperty(INVALID_GRAPH_TXN, v, msg, pid).has_value()) ++hits;
        t1 = Clock::now();
        std::printf("  [A] 点查 prop=%u          hits=%zu/%zu  %.3f us/次\n", pid, hits, vids.size(), us(t0, t1, vids.size()));
    }

    // B 全属性前缀扫描（每顶点一次）
    size_t got = 0;
    t0 = Clock::now();
    for (VertexId v : vids) { auto p = store.getVertexProperties(INVALID_GRAPH_TXN, v, msg); if (p) { ++got; (void)p->size(); } }
    t1 = Clock::now();
    std::printf("  [B] 全属性前缀扫描         命中=%zu/%zu  %.3f us/次\n", got, vids.size(), us(t0, t1, vids.size()));

    // C 批量全属性（一次 dispatch 内做 N 次前缀扫描）
    t0 = Clock::now();
    auto batch = store.getVertexPropertiesBatch(INVALID_GRAPH_TXN, msg, vids);
    t1 = Clock::now();
    size_t nb = std::count_if(batch.begin(), batch.end(), [](const auto& o) { return o.has_value(); });
    std::printf("  [C] 批量前缀扫描           命中=%zu/%zu  %.3f us/次\n", nb, batch.size(), us(t0, t1, batch.size()));

    store.close();
    return 0;
}
