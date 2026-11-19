#pragma once
/// 每图一个的**索引构建服务**：FIFO 队列 + 工作线程 + 取消 + 关图排空。
///
/// 设计依据：docs/storage/online-index-build-design.md §8/§12。
/// 职责边界：
///   * 只管"排队/线程/取消/退出"，**不知道**构建怎么做（Job 由调用方注入）；
///   * 同一 index_id 幂等（已在排队/运行 ⇒ submit 返回 false）；
///   * `cancel()` 只置标记：排队中的任务被直接丢弃，运行中的任务在**阶段边界**自行检查；
///   * `shutdown()`：置停止 → 丢弃队列 → join 所有线程（在飞任务必须自行收敛）。
///
/// 线程模型：固定 N 个 `std::thread`（默认 1）。Job 内部通常 `blockingWait` 一个协程，
/// 协程自己从 SessionPool 借会话 ⇒ 与查询路径隔离。

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace eugraph {

class IndexBuildService {
public:
    using Job = std::function<void()>;

    struct Options {
        size_t threads = 1; ///< 并发构建数（1 = 串行）
    };

    IndexBuildService() : IndexBuildService(Options{}) {}
    explicit IndexBuildService(Options options);
    ~IndexBuildService();

    IndexBuildService(const IndexBuildService&) = delete;
    IndexBuildService& operator=(const IndexBuildService&) = delete;

    /// 提交任务。同一 index_id 已在排队/运行 ⇒ 幂等返回 false。
    bool submit(std::string name, Job job);

    /// 请求取消（排队中直接丢弃；运行中由 Job 在阶段边界检查）。
    void cancel(const std::string& name);
    bool isCancelled(const std::string& name) const;

    /// 观测。
    size_t running() const;
    size_t queued() const;

    /// 等待队列清空且无运行任务（测试/关图用；超时返回 false）。
    bool waitUntilIdle(uint32_t timeout_ms);

    /// 周期性回调（如 PENDING_PURGE 清扫）：独立线程，异常吞掉；shutdown 时先停它。
    void startPeriodic(uint32_t interval_ms, std::function<void()> tick);
    void stopPeriodic();

    /// 停止服务：丢弃排队任务，join 所有线程。幂等。
    void shutdown();

private:
    struct Entry {
        std::string name;
        Job job;
    };

    void workerLoop();

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Entry> queue_;
    std::unordered_set<std::string> known_;     ///< 排队或运行中
    std::unordered_set<std::string> cancelled_; ///< 取消标记（任务结束时清除）
    size_t running_ = 0;
    bool stop_ = false;
    size_t thread_count_ = 1;
    std::vector<std::thread> workers_;

    std::thread ticker_;
    std::atomic<bool> ticker_stop_{false};
    std::mutex ticker_mu_;
    std::condition_variable ticker_cv_;
};

} // namespace eugraph
