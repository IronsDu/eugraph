// 存储层微基准（C2 / known-defects §9）：量"每属性一次 B-tree 查找"的真实单位成本。
//
// 直接调 SyncGraphDataStore，绕开算子层与协程层：
//   1) 顺序标签扫描一行要多久（基准，对应 RETURN count(m)）；
//   2) 按 (vid, prop_id) 逐个 getVertexProperty 要多久（怀疑是主成本）。
// label_id 与 prop_id 自动识别（按行数找 Message，按"值为字符串"找 creationDate），避免猜。
//
// 它不在 CMake 目标里（tests/ 下的测试是显式登记的），手工编译即可：
//   L=build/release/CMakeFiles/data_chunk_tests.dir/link.txt
//   LIBS=$(tr ' ' '\n' < $L | grep '\.a$' | grep -v libeugraph | sed 's|^|build/release/|')
//   g++ -std=c++20 -O2 -I src -I build/release/vcpkg_installed/x64-linux/include \
//       -I build/release/wiredtiger-install/include tests/tools/prop_lookup_bench.cpp \
//       -o /tmp/propbench -Wl,--start-group build/release/libeugraph_{query_engine,lib,metadata,compute}.a \
//       -Wl,--end-group $LIBS -ldl -lpthread -lgcc_s
//
// ⚠️ 两个前提：① 服务端必须先停（同一 WT 库不能两个进程同时打开）；
//    ② 数据根目录要指向 **<graph>/data**（label_fwd_*/vprop_* 在 `graph_0/data` 这个独立
//    WT 库里，`graph_0` 本身只有 label_reverse/edge_index/vertex_existence 三个全局表）。
#include "storage/data/sync_graph_data_store.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace eugraph;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "/home/dodo/code/fuck/eugraph-sf0.1-fresh/graph_0";
    SyncGraphDataStore store;
    if (!store.open(dir)) {
        std::printf("open failed: %s\n", dir.c_str());
        return 1;
    }

    // 先确认 label 5 是否真有属性：取前 3 个 vid 的全部属性并打印
    {
        std::vector<VertexId> probe;
        store.scanVerticesByLabel(INVALID_GRAPH_TXN, 5, [&](VertexId v) {
            probe.push_back(v);
            return probe.size() < 3;
        });
        for (VertexId v : probe) {
            auto props = store.getVertexProperties(INVALID_GRAPH_TXN, v, 5);
            std::printf("  vid=%llu -> props %s", (unsigned long long)v,
                        props.has_value() ? "present" : "ABSENT");
            if (props.has_value()) {
                std::printf(", size=%zu, set idx:", props->size());
                for (size_t i = 0; i < props->size(); ++i)
                    if ((*props)[i].has_value())
                        std::printf(" %zu", i);
            }
            std::printf("\n");
        }
    }

    // 找 Message 标签：行数最接近 286744 的那个
    LabelId label = INVALID_LABEL_ID;
    size_t best_delta = SIZE_MAX;
    for (LabelId lid = 1; lid <= 24; ++lid) {
        size_t n = 0;
        store.scanVerticesByLabel(INVALID_GRAPH_TXN, lid, [&](VertexId) {
            ++n;
            return true;
        });
        if (n > 100000 && (n > 286744 ? n - 286744 : 286744 - n) < best_delta) {
            best_delta = (n > 286744 ? n - 286744 : 286744 - n);
            label = lid;
        }
    }
    if (label == INVALID_LABEL_ID) {
        std::printf("Message label not found (scanned 1..24)\n");
        return 1;
    }

    std::vector<VertexId> vids;
    vids.reserve(20000);
    auto t0 = Clock::now();
    size_t scanned = 0;
    store.scanVerticesByLabel(INVALID_GRAPH_TXN, label, [&](VertexId vid) {
        ++scanned;
        if (vids.size() < 20000)
            vids.push_back(vid);
        return true;
    });
    double scan_ms = ms(t0);
    std::printf("label %u : %zu rows, scan %.1f ms, %.3f us/row\n", label, scanned, scan_ms,
                scanned ? scan_ms * 1000.0 / scanned : 0.0);

    // 逐属性探测：找"值是字符串且非空"的属性（creationDate 是 LONG，这里先扫出各属性命中数）
    for (uint16_t pid = 0; pid < 10; ++pid) {
        t0 = Clock::now();
        size_t hits = 0;
        for (VertexId v : vids) {
            if (store.getVertexProperty(INVALID_GRAPH_TXN, v, label, pid).has_value())
                ++hits;
        }
        double el = ms(t0);
        std::printf("  prop %u : %zu/%zu hits, %.1f ms, %.3f us/call\n", pid, hits, vids.size(), el,
                    el * 1000.0 / vids.size());
    }
    store.close();
    return 0;
}
