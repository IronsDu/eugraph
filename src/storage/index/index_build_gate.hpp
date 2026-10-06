#pragma once
/// 索引构建闸门：**进程内、按 `index_id` 索引**的"闸门 + 在飞写者计数"。
///
/// 为什么需要它：DML 写入路径看到的是**每语句的模式副本**，因此"是否已进入收尾"无法放进 schema；
/// 而收尾阶段必须与写者建立一条 happens-before 边界，否则"写入变更表后、排空之前"的窗口会丢写
/// （实测：边索引构建期插入 6 条丢 2 条，见设计 §20.13）。
///
/// 协议（设计 §7.1 / §15.1 的落地形态）：
/// * 写者（索引处于 `BUILDING`）：`tryEnter()` 成功 ⇒ **写变更表**（构建器会排空它）；
///   失败（闸门已关）⇒ **直写索引**（此时回填已结束、索引已可用，不再有变更表可依赖）；
/// * 构建器收尾：`closeAndWait()`（CAS 关闸 + 等 `inflight == 0`）⇒ 追平变更表 ⇒ 翻 `PUBLIC` ⇒ `release()`。
///
/// 不变量：闸门关闭**之后**进入的写者一律直写索引 ⇒ 不存在"关闸后仍写进变更表"的写入 ⇒ 排空即终局。
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {

class IndexBuildGate : public std::enable_shared_from_this<IndexBuildGate> {
public:
    /// RAII 准入凭证：析构时自动 `inflight--`（覆盖异常/提前返回等所有路径）。
    class Guard {
    public:
        Guard() = default;
        Guard(std::shared_ptr<IndexBuildGate> gate) : gate_(std::move(gate)) {}
        Guard(Guard&& o) noexcept : gate_(std::move(o.gate_)) {}
        Guard& operator=(Guard&& o) noexcept {
            reset();
            gate_ = std::move(o.gate_);
            return *this;
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        ~Guard() {
            reset();
        }

        /// 是否成功准入（false ⇒ 调用方应直写索引）
        explicit operator bool() const {
            return gate_ != nullptr;
        }
        void reset() {
            if (gate_) {
                gate_->inflight_.fetch_sub(1, std::memory_order_acq_rel);
                gate_.reset();
            }
        }

    private:
        std::shared_ptr<IndexBuildGate> gate_;
    };

    /// 尝试准入：闸门已关 ⇒ 返回空凭证（写者应直写索引）
    Guard tryEnter() {
        if (closed_.load(std::memory_order_acquire))
            return Guard{};
        inflight_.fetch_add(1, std::memory_order_acq_rel);
        // **两阶段检查**（设计 §15.1）：先 ++ 再复查，确保"准入成功"与"关闸"线性化 ——
        // 否则会出现"读到未关闸 → 关闸并排空 → 才写入变更表"的丢写。
        if (closed_.load(std::memory_order_acquire)) {
            inflight_.fetch_sub(1, std::memory_order_acq_rel);
            return Guard{};
        }
        return Guard{std::static_pointer_cast<IndexBuildGate>(shared_from_this())};
    }

    /// 关闸并等待所有在飞写者退出；返回是否在超时内等到（超时后调用方仍可继续，窗口会变大）
    bool closeAndWait(uint32_t timeout_ms = 5000) {
        closed_.store(true, std::memory_order_release);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (inflight_.load(std::memory_order_acquire) != 0) {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    /// 收尾结束后释放（把闸门恢复为"关"状态即可：此后写者直写索引）
    void release() {
        closed_.store(true, std::memory_order_release);
    }
    bool closed() const {
        return closed_.load(std::memory_order_acquire);
    }
    uint32_t inflight() const {
        return inflight_.load(std::memory_order_acquire);
    }

private:
    friend class IndexBuildGateRegistry;
    IndexBuildGate() = default;
    std::atomic<bool> closed_{false};
    std::atomic<uint32_t> inflight_{0};
};

/// 进程级注册表：把 `index_id` 映射到闸门（进程内单实例即可：图数据库服务端为单进程）
class IndexBuildGateRegistry {
public:
    static IndexBuildGateRegistry& instance() {
        static IndexBuildGateRegistry reg;
        return reg;
    }

    /// 取得（必要时创建）某索引的闸门。索引删除后条目可保留（id 不复用，见设计 H15）。
    std::shared_ptr<IndexBuildGate> gate(uint32_t index_id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = gates_.find(index_id);
        if (it != gates_.end())
            return it->second;
        auto g = std::shared_ptr<IndexBuildGate>(new IndexBuildGate());
        gates_[index_id] = g;
        return g;
    }

private:
    std::mutex mu_;
    std::unordered_map<uint32_t, std::shared_ptr<IndexBuildGate>> gates_;
};

/// **把闸门守卫绑定到用户事务**：守卫在事务 **commit/rollback 时**才释放
/// （设计 §15.1 的实现契约：准入名额必须覆盖整个用户事务，而不是"一次写调用"）。
/// 为什么必须如此：写入变更表后、事务提交前，构建器的"排空"看不到该行 ⇒ 若此刻就放行，
/// 该行会在构建结束后永远留在变更表里 ⇒ **丢写**（实测边路径丢 1/6，设计 §20.19/H2）。
class IndexBuildTxnScope {
public:
    static void attach(uint64_t txn, IndexBuildGate::Guard guard) {
        if (!guard)
            return;
        std::lock_guard<std::mutex> lock(mu());
        guards()[txn].push_back(std::move(guard));
    }
    /// 事务结束时调用（commit/rollback 之后）：释放该事务登记的全部准入名额
    static void releaseAll(uint64_t txn) {
        std::vector<IndexBuildGate::Guard> to_release;
        {
            std::lock_guard<std::mutex> lock(mu());
            auto it = guards().find(txn);
            if (it == guards().end())
                return;
            to_release = std::move(it->second);
            guards().erase(it);
        }
        to_release.clear(); // Guard 析构 ⇒ inflight--
    }

private:
    static std::mutex& mu() {
        static std::mutex m;
        return m;
    }
    static std::unordered_map<uint64_t, std::vector<IndexBuildGate::Guard>>& guards() {
        static std::unordered_map<uint64_t, std::vector<IndexBuildGate::Guard>> g;
        return g;
    }
};

} // namespace eugraph
