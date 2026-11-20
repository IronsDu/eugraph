#include "storage/index/index_build_service.hpp"

namespace eugraph {

IndexBuildService::IndexBuildService(Options options) : thread_count_(options.threads == 0 ? 1 : options.threads) {
    workers_.reserve(thread_count_);
    for (size_t i = 0; i < thread_count_; ++i)
        workers_.emplace_back([this] { workerLoop(); });
}

IndexBuildService::~IndexBuildService() {
    shutdown();
}

bool IndexBuildService::submit(std::string name, Job job) {
    if (!job)
        return false;
    std::lock_guard<std::mutex> lock(mu_);
    if (stop_)
        return false;
    if (!known_.insert(name).second)
        return false; // 已在排队/运行
    queue_.push_back(Entry{std::move(name), std::move(job)});
    cv_.notify_one();
    return true;
}

void IndexBuildService::cancel(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    // 只有"确实有该索引的任务"才需要取消标记；否则（构建已结束）标记会污染后续同名构建。
    if (known_.count(name) != 0)
        cancelled_.insert(name);
    cv_.notify_all();
}

bool IndexBuildService::isCancelled(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mu_);
    return cancelled_.count(name) != 0;
}

size_t IndexBuildService::running() const {
    std::lock_guard<std::mutex> lock(mu_);
    return running_;
}

size_t IndexBuildService::queued() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

bool IndexBuildService::waitUntilIdle(uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mu_);
    const auto idle = [this] { return queue_.empty() && running_ == 0; };
    if (idle())
        return true;
    if (timeout_ms == 0)
        return false;
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), idle);
}

void IndexBuildService::startPeriodic(uint32_t interval_ms, std::function<void()> tick) {
    stopPeriodic();
    {
        std::lock_guard<std::mutex> lock(ticker_mu_);
        ticker_stop_.store(false, std::memory_order_release);
    }
    ticker_ = std::thread([this, interval_ms, tick = std::move(tick)]() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(ticker_mu_);
                if (ticker_cv_.wait_for(lock, std::chrono::milliseconds(interval_ms),
                                        [this] { return ticker_stop_.load(std::memory_order_acquire); }))
                    return;
            }
            try {
                tick();
            } catch (...) {
                // 周期任务失败不影响服务；下一轮再试。
            }
        }
    });
}

void IndexBuildService::stopPeriodic() {
    {
        std::lock_guard<std::mutex> lock(ticker_mu_);
        ticker_stop_.store(true, std::memory_order_release);
    }
    ticker_cv_.notify_all();
    if (ticker_.joinable())
        ticker_.join();
}

void IndexBuildService::shutdown() {
    stopPeriodic();
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stop_ && workers_.empty())
            return;
        stop_ = true;
        // 关图/删库时：在飞 Job 也要在**阶段边界**自行退出，否则 shutdown 会等到整次回填结束。
        for (const auto& name : known_)
            cancelled_.insert(name);
        queue_.clear();
        queue_.shrink_to_fit();
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
    workers_.clear();
    std::lock_guard<std::mutex> lock(mu_);
    known_.clear();
    cancelled_.clear();
}

void IndexBuildService::workerLoop() {
    while (true) {
        Entry entry;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_)
                return;
            entry = std::move(queue_.front());
            queue_.pop_front();
            // 排队期间被取消 ⇒ 直接丢弃，不执行 Job。
            if (cancelled_.count(entry.name) != 0) {
                known_.erase(entry.name);
                cancelled_.erase(entry.name);
                continue;
            }
            ++running_;
        }

        try {
            entry.job();
        } catch (...) {
            // Job 自己负责把失败写进索引状态；服务只保证线程不退出。
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            --running_;
            known_.erase(entry.name);
            cancelled_.erase(entry.name);
        }
        cv_.notify_all();
    }
}

} // namespace eugraph
