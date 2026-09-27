// 决定性隔离实验：定位存储层并发退化的共享资源。
//
// 背景（known-defects §9.6 / §9.7）：裸 getVertexProperty 在 1→8 线程下每次查找 +155%、
// 吞吐反而下降 60%；且**小到能驻留 cache 的工作集退化程度相同**，故排除缓存/内存带宽。
// 剩下的嫌疑是"每次查找都要获取的共享资源"——首要嫌疑是本项目 getVertexProperty 内部
// **每次调用都 openCursor / 析构**（对同一张表的共享 WT_DATA_HANDLE 加锁）。
//
// 本工具用 WT C API 直接对比两种形态（与项目实现无关，属独立判据）：
//   A: 每次查找 open_cursor → search → close_cursor（项目的做法）
//   B: 每线程 open_cursor 一次，循环内复用（cursor->reset + search）
// 多轮交错、每档取 min（本机 CPU 频率会漂移，顺序测会把漂移误判为退化）。
//
// 用法: wt_cursor_bench <graph_data_dir> <table_name> <per_thread> [rounds]
//   table_name 形如 table:vprop_2（可用 wt list 查看）
#include <wiredtiger.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

namespace {

// 生成第 i 个键：8 字节 BE vertex id + 2 字节 BE prop id（与项目 KV 编码一致）
void makeKey(uint64_t vid, uint16_t pid, std::vector<uint8_t>& out) {
    out.resize(10);
    for (int k = 0; k < 8; ++k)
        out[k] = static_cast<uint8_t>(vid >> (56 - 8 * k));
    out[8] = static_cast<uint8_t>(pid >> 8);
    out[9] = static_cast<uint8_t>(pid & 0xFF);
}

double run_mode(WT_CONNECTION* conn, const std::string& table, bool reuse_cursor, int threads, size_t per_thread,
                const std::vector<uint64_t>& vids, uint16_t pid) {
    std::atomic<size_t> found{0};
    auto t0 = Clock::now();
    std::vector<std::thread> th;
    th.reserve(threads);
    for (int t = 0; t < threads; ++t) {
        th.emplace_back([&, t]() {
            WT_SESSION* session = nullptr;
            if (conn->open_session(conn, nullptr, nullptr, &session) != 0)
                return;
            WT_CURSOR* cur = nullptr;
            if (reuse_cursor && session->open_cursor(session, table.c_str(), nullptr, nullptr, &cur) != 0)
                return;
            std::vector<uint8_t> key;
            size_t local = 0;
            for (size_t i = 0; i < per_thread; ++i) {
                uint64_t vid = vids[(i + static_cast<size_t>(t) * 977) % vids.size()];
                makeKey(vid, pid, key);
                if (!reuse_cursor) {
                    if (session->open_cursor(session, table.c_str(), nullptr, nullptr, &cur) != 0)
                        continue;
                } else {
                    cur->reset(cur);
                }
                cur->set_key(cur, key.data(), static_cast<int>(key.size()));
                int ret = cur->search(cur);
                if (ret == 0) {
                    WT_ITEM value;
                    if (cur->get_value(cur, &value) == 0 && value.size > 0)
                        ++local;
                }
                if (!reuse_cursor)
                    cur->close(cur);
            }
            if (reuse_cursor)
                cur->close(cur);
            session->close(session, nullptr);
            found.fetch_add(local);
        });
    }
    for (auto& x : th)
        x.join();
    auto t1 = Clock::now();
    double total = static_cast<double>(per_thread) * threads;
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / total;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "/home/dodo/code/fuck/eugraph-sf0.1-fresh/graph_0/data";
    const std::string table = argc > 2 ? argv[2] : "table:vprop_2";
    const size_t per_thread = argc > 3 ? std::stoul(argv[3]) : 20000;
    const int rounds = argc > 4 ? std::stoi(argv[4]) : 3;
    const uint16_t pid = 1;

    WT_CONNECTION* conn = nullptr;
    std::string cfg = "create,cache_size=256MB,eviction=(threads_max=4)";
    int ret = wiredtiger_open(dir.c_str(), nullptr, cfg.c_str(), &conn);
    if (ret != 0) {
        std::printf("wiredtiger_open failed: %d (%s)\n", ret, wiredtiger_strerror(ret));
        return 1;
    }

    std::vector<uint64_t> vids;
    vids.reserve(50000);
    for (uint64_t v = 0; v < 200000 && vids.size() < 50000; ++v)
        vids.push_back(v);
    std::printf("dir=%s table=%s keys=%zu\n", dir.c_str(), table.c_str(), vids.size());

    const std::vector<int> thread_counts = {1, 2, 4, 8};
    for (bool reuse : {false, true}) {
        std::printf("--- %s ---\n", reuse ? "B: 游标复用（每线程开一次）" : "A: 每次查找开关游标（项目做法）");
        std::vector<double> best(thread_counts.size(), 1e18);
        for (int r = 0; r < rounds; ++r) {
            for (size_t k = 0; k < thread_counts.size(); ++k) {
                double us = run_mode(conn, table, reuse, thread_counts[k], per_thread, vids, pid);
                best[k] = std::min(best[k], us);
            }
        }
        for (size_t k = 0; k < thread_counts.size(); ++k) {
            std::printf("    threads=%d  %7.3f us/次   %5.2f M/s", thread_counts[k], best[k], 1.0 / best[k]);
            if (k > 0)
                std::printf("   vs 1线程 %+.0f%%", (best[k] / best[0] - 1.0) * 100.0);
            std::printf("\n");
        }
    }
    conn->close(conn, nullptr);
    return 0;
}
