#pragma once

// 图的"使用闸门"：DROP DATABASE 必须先**关闸并等在飞使用者归零**，再关闭 WT 连接。
//
// 为什么必须这样做（实测，见 docs/tests/bolt-driver-integration-notes.md）：
// `GraphManager::dropGraph` 在锁外关闭三个 store；若有协程在此之前已解析到该图实例并仍在执行，
// 关闭就会与它们**并发使用同一个 WT 连接** ⇒ `__conn_close … failure during close`
// ⇒ `WT_PANIC` + `disabling further writes` ⇒ **此后进程内所有写入静默丢失**、后续
// `CREATE DATABASE` 打不开图实例（半成品图）⇒ 驱动测试"第一遍失败、第二遍通过"。
//
// 与在线索引构建用的 `IndexBuildGate` 是同一模式：**先关闸（拒绝新使用者）→ 等在飞归零 → 再销毁**。
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace eugraph {

class GraphUsageGate : public std::enable_shared_from_this<GraphUsageGate> {
public:
    /// RAII 使用凭证：析构时自动归还名额
    class Guard {
    public:
        Guard() = default;
        Guard(std::shared_ptr<GraphUsageGate> gate, bool valid) : gate_(std::move(gate)), valid_(valid) {}
        Guard(Guard&& other) noexcept : gate_(std::move(other.gate_)), valid_(other.valid_) {
            other.valid_ = false;
        }
        Guard& operator=(Guard&& other) noexcept {
            if (this != &other) {
                release();
                gate_ = std::move(other.gate_);
                valid_ = other.valid_;
                other.valid_ = false;
            }
            return *this;
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        ~Guard() {
            release();
        }

        explicit operator bool() const {
            return valid_;
        }
        void reset() {
            release();
        }

    private:
        void release() {
            if (valid_ && gate_) {
                gate_->leave();
                valid_ = false;
            }
            gate_.reset();
        }
        std::shared_ptr<GraphUsageGate> gate_;
        bool valid_ = false;
    };

    /// 两阶段准入：先占名额，再复查"是否已关闸"；已关闸则立刻归还并失败。
    /// 与关闸方的 `close()`（在同一把锁下置位）共同保证：**准入成功者一定在关闸之前**。
    Guard tryEnter() {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_)
            return Guard{};
        ++inflight_;
        return Guard{shared_from_this(), true};
    }

    /// 关闸：此后 `tryEnter()` 一律失败
    void close() {
        std::lock_guard<std::mutex> lock(mu_);
        closed_ = true;
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mu_);
        return closed_;
    }

    size_t inflight() const {
        std::lock_guard<std::mutex> lock(mu_);
        return inflight_;
    }

    /// 等在飞使用者归零（返回 true = 已排空；false = 超时仍有在飞）
    bool waitDrained(uint32_t timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (inflight_ > 0) {
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout && inflight_ > 0)
                return false;
        }
        return true;
    }

private:
    void leave() {
        std::lock_guard<std::mutex> lock(mu_);
        if (inflight_ > 0)
            --inflight_;
        if (inflight_ == 0)
            cv_.notify_all();
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool closed_ = false;
    size_t inflight_ = 0;
};

} // namespace eugraph
