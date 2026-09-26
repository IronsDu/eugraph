// 复刻 ProjectionExtract 的 coalesce 路径每行链条，定位 2.3 µs/属性 的去向。
// 链条：ref_vids 收集 → 每属性 batchGetVertexProperties(ids, lid, {pid}) → 取 pid → push_back
#include "storage/data/sync_graph_data_store.hpp"

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
    const LabelId msg = argc > 2 ? static_cast<LabelId>(std::stoul(argv[2])) : 2;   // Message
    SyncGraphDataStore store;
    if (!store.open(dir)) { std::printf("open failed\n"); return 1; }

    std::vector<VertexId> vids; vids.reserve(20000);
    store.scanVerticesByLabel(INVALID_GRAPH_TXN, msg, [&](VertexId v) { vids.push_back(v); return vids.size() < 20000; });
    std::printf("label %u, sample %zu\n", msg, vids.size());

    // ① 复刻 coalesce：每属性一次 batch(投影) + 收集到 vector
    {
        auto t0 = Clock::now();
        for (uint16_t pid : {1, 5, 3}) {                  // creationDate, length, browserUsed
            std::vector<std::optional<Properties>> out;
            out.reserve(vids.size());
            for (VertexId v : vids) {
                Properties props;
                props.resize(pid + 1);
                auto pv = store.getVertexProperty(INVALID_GRAPH_TXN, v, msg, pid);
                if (pv) props[pid] = std::move(*pv);
                out.emplace_back(std::move(props));
            }
            (void)out;
        }
        auto t1 = Clock::now();
        std::printf("  [1] 复刻 coalesce（3 属性，含 Properties 分配） %.3f us/行/属性\n", us(t0, t1, vids.size() * 3));
    }
    // ② 只做点查，不分配 Properties
    {
        auto t0 = Clock::now();
        size_t hits = 0;
        for (uint16_t pid : {1, 5, 3})
            for (VertexId v : vids) if (store.getVertexProperty(INVALID_GRAPH_TXN, v, msg, pid)) ++hits;
        auto t1 = Clock::now();
        std::printf("  [2] 仅点查（无 Properties 分配）           %.3f us/行/属性 (hits=%zu)\n", us(t0, t1, vids.size() * 3), hits);
    }
    // ③ 一次性取回全部属性（scan-all 语义），再切片 3 个
    {
        auto t0 = Clock::now();
        for (VertexId v : vids) {
            auto p = store.getVertexProperties(INVALID_GRAPH_TXN, v, msg);
            if (p) { volatile size_t s = p->size(); (void)s; }
        }
        auto t1 = Clock::now();
        std::printf("  [3] scan-all 一次取回全部属性             %.3f us/行\n", us(t0, t1, vids.size()));
    }
    // ④ 批量 scan-all（一次 dispatch 语义）
    {
        auto t0 = Clock::now();
        auto b = store.getVertexPropertiesBatch(INVALID_GRAPH_TXN, msg, vids);
        auto t1 = Clock::now();
        std::printf("  [4] 批量 scan-all                        %.3f us/行\n", us(t0, t1, vids.size()));
    }
    store.close();
    return 0;
}
