#pragma once
/// 图内索引构建调度器：FIFO 队列 + **可配并发度** + 取消 + 可观测。
/// 设计依据：docs/storage/online-index-build-design.md §8（并发度模型与死锁自由性论证）、§12 测试 8。
///
/// 契约：
///  * **同时在跑数 ≤ `max_concurrent`**（默认 2）——它同时是 IO/内存/cache 压力的控制阀；
///  * **同索引幂等**：同一 `index_id` 已在排队或运行中时，重复提交返回 false（复用既有任务）；
///  * **取消**：排队中 ⇒ 直接丢弃、永不启动；运行中 ⇒ 置取消位，任务在**阶段边界**自行检查；
///  * **异常不阻塞队列**：任务抛异常也必须释放并发名额（否则队列永久卡死）；
///  * **无裸指针**：任务用 `std::function`，任务状态用 `std::shared_ptr`，线程由调度器持有并 join。
///
/// 可测试性：`Runner` 可注入。默认运行器在自己的 `std::thread` 上执行；测试注入"只记录、不执行"的
/// 运行器即可**确定性地**断言并发上限、FIFO 顺序与取消语义（无 sleep、无竞态）。
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace eugraph {

class IndexBuildScheduler {
public:
    struct Options {
        size_t max_concurrent = 2; ///< 图内同时在跑的构建数上限（1 = 串行）
    };
    using Job = std::function<void()>;
    using Runner = std::function<void(Job)>;

    IndexBuildScheduler() : IndexBuildScheduler(Options{}) {}

    explicit IndexBuildScheduler(Options opts, Runner runner = {})
        : opts_(opts), runner_(std::move(runner)), custom_runner_(static_cast<bool>(runner_)) {
        if (opts_.max_concurrent == 0)
            throw std::invalid_argument("IndexBuildScheduler: max_concurrent must be >= 1");
    }

    /// 析构时等所有任务结束（join），避免任务持有已销毁的调度器。
    ~IndexBuildScheduler() {
        drain();
    }

    IndexBuildScheduler(const IndexBuildScheduler&) = delete;
    IndexBuildScheduler& operator=(const IndexBuildScheduler&) = delete;

    /// 入队。返回 false 表示该索引已在排队或运行中（幂等，不重复提交）。
    bool submit(uint64_t index_id, std::string name, Job job) {
        if (!job)
            return false;
        std::unique_lock<std::mutex> lock(mu_);
        if (find(index_id) != nullptr)
            return false;
        auto state = std::make_shared<JobState>(JobState{index_id, std::move(name), std::move(job), false, false});
        queue_.push_back(state);
        pumpLocked(lock);
        return true;
    }

    /// 取消：排队中 ⇒ 移除且永不启动；运行中 ⇒ 置取消位（由任务自行在阶段边界检查）。返回是否命中。
    bool cancel(uint64_t index_id) {
        std::unique_lock<std::mutex> lock(mu_);
        auto it = std::find_if(queue_.begin(), queue_.end(),
                               [&](const std::shared_ptr<JobState>& s) { return s->index_id == index_id; });
        if (it != queue_.end()) {
            (*it)->cancelled = true;
            queue_.erase(it);
            return true;
        }
        for (auto& state : running_) {
            if (state->index_id == index_id) {
                state->cancelled = true; // 运行中：置取消位，由任务在阶段边界自行检查
                return true;
            }
        }
        return false;
    }

    bool isCancelled(uint64_t index_id) const {
        std::lock_guard<std::mutex> lock(mu_);
        const JobState* state = find(index_id);
        return state != nullptr && state->cancelled;
    }

    struct Entry {
        uint64_t index_id = 0;
        std::string name;
        bool running = false;
        bool cancelled = false;
    };

    std::vector<Entry> snapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<Entry> out;
        out.reserve(running_.size() + queue_.size());
        for (const auto& s : running_)
            out.push_back(Entry{s->index_id, s->name, true, s->cancelled});
        for (const auto& s : queue_)
            out.push_back(Entry{s->index_id, s->name, false, s->cancelled});
        return out;
    }

    size_t runningCount() const {
        std::lock_guard<std::mutex> lock(mu_);
        return running_.size();
    }
    size_t queuedCount() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queue_.size();
    }
    size_t maxConcurrent() const {
        return opts_.max_concurrent;
    }

    /// 等全部任务结束并回收线程（图关闭、测试与退出路径使用）。
    void drain() {
        std::vector<std::thread> threads;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return queue_.empty() && running_.empty(); });
            threads.swap(threads_);
        }
        for (auto& t : threads) {
            if (t.joinable())
                t.join();
        }
    }

private:
    struct JobState {
        uint64_t index_id;
        std::string name;
        Job job;
        bool running = false;
        bool cancelled = false;
    };

    const JobState* find(uint64_t index_id) const {
        const JobState* r = findLocked(index_id);
        if (r != nullptr)
            return r;
        for (const auto& s : queue_) {
            if (s->index_id == index_id)
                return s.get();
        }
        return nullptr;
    }
    const JobState* findLocked(uint64_t index_id) const {
        for (const auto& s : running_) {
            if (s->index_id == index_id)
                return s.get();
        }
        return nullptr;
    }

    /// 在持锁状态下启动任务（调用方必须持有 mu_）。调用 `runner_` 时**不持锁**：
    /// 注：注入"同步执行"的运行器（测试用）时，任务的 finish() 会重入 pumpLocked 继续启动下一个，
    /// 递归深度 = 队列长度；生产路径用默认的线程运行器，不存在该递归。
    /// 假运行器可能同步执行任务，而任务结束会回调 finish() ⇒ 必须避免自死锁。
    void pumpLocked(std::unique_lock<std::mutex>& lock) {
        while (running_.size() < opts_.max_concurrent && !queue_.empty()) {
            auto state = queue_.front();
            queue_.pop_front();
            state->running = true;
            running_.push_back(state);
            Job wrapped = [this, state] {
                try {
                    state->job();
                } catch (...) {
                    // 任务异常也必须释放并发名额，否则队列永久卡死
                }
                finish(state->index_id);
            };
            if (custom_runner_) {
                auto runner = runner_; // 拷贝一份，避免持锁调用外部代码
                lock.unlock();
                runner(std::move(wrapped));
                lock.lock();
            } else {
                lock.unlock();
                try {
                    std::thread worker(std::move(wrapped));
                    lock.lock();
                    threads_.push_back(std::move(worker));
                } catch (...) {
                    lock.lock();
                    state->cancelled = true;
                    state->running = false;
                    running_.erase(std::remove(running_.begin(), running_.end(), state), running_.end());
                    throw;
                }
            }
        }
    }

    void finish(uint64_t index_id) {
        std::unique_lock<std::mutex> lock(mu_);
        running_.erase(std::remove_if(running_.begin(), running_.end(),
                                      [&](const std::shared_ptr<JobState>& s) { return s->index_id == index_id; }),
                       running_.end());
        pumpLocked(lock); // 释放出的名额立刻给队首（FIFO）
        lock.unlock();
        cv_.notify_all();
    }

    Options opts_;
    Runner runner_;
    bool custom_runner_ = false;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<JobState>> queue_;
    std::vector<std::shared_ptr<JobState>> running_;
    std::vector<std::thread> threads_;
};

} // namespace eugraph
