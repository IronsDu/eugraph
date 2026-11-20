#pragma once
/// 每图一份的索引运行时权威（**线程安全，无 WT 依赖**）。
///
/// 设计依据：docs/storage/online-index-build-design.md §5 / §10 / §12。
///
/// 这里放"有状态"的东西：
///   * `IndexRuntime`：单个索引的 phase/lifecycle/准入闸门/在飞写者/读者/进度；
///   * `IndexWriteGuard`：写路径的准入凭证（RAII，绑定到**用户事务**的生命周期）；
///   * `IndexReadHandle`：查询执行期的读凭证（RAII，保护数据不被 DROP 删除）；
///   * `IndexRuntimeRegistry`：index_id → IndexRuntime 的映射（每图一个，不跨图）。
///
/// 约定：
///   * **准入是两阶段**：`inflight++` 之后**复查**闸门仍开，才算准入（设计 I5）；
///   * 关闭闸门后**永不**自动重开；失败/取消路径必须显式 `reopenAdmission()`（I8）；
///   * `tryEnter` 失败**不代表**可以直写索引表——调用方应返回 `RetryLater`，等待后重试（I3）；
///   * 守卫与句柄都是 move-only；禁止复制，避免 inflight/readers 重复计数。

#include "common/types/index_runtime.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace folly::coro {
class Baton;
template <typename T> class Task;
} // namespace folly::coro

namespace eugraph {

class IndexRuntime;

// ==================== 准入凭证 ====================

/// 写路径的准入凭证：析构自动 `inflight--`（覆盖异常/取消/提前返回所有路径）。
class IndexWriteGuard {
public:
    IndexWriteGuard() = default;
    explicit IndexWriteGuard(std::shared_ptr<IndexRuntime> runtime) : runtime_(std::move(runtime)) {}
    ~IndexWriteGuard();
    IndexWriteGuard(IndexWriteGuard&&) noexcept = default;
    IndexWriteGuard& operator=(IndexWriteGuard&&) noexcept = default;
    IndexWriteGuard(const IndexWriteGuard&) = delete;
    IndexWriteGuard& operator=(const IndexWriteGuard&) = delete;

    explicit operator bool() const noexcept {
        return runtime_ != nullptr;
    }
    void reset();
    /// 仅用于观测/日志；已释放后返回 0。
    uint32_t indexId() const;

private:
    std::shared_ptr<IndexRuntime> runtime_;
};

/// 查询执行期的读凭证：持有期间 DROP 不会物理删表；析构自动 `readers--`。
class IndexReadHandle {
public:
    IndexReadHandle() = default;
    explicit IndexReadHandle(std::shared_ptr<IndexRuntime> runtime) : runtime_(std::move(runtime)) {}
    ~IndexReadHandle();
    IndexReadHandle(IndexReadHandle&&) noexcept = default;
    IndexReadHandle& operator=(IndexReadHandle&&) noexcept = default;
    IndexReadHandle(const IndexReadHandle&) = delete;
    IndexReadHandle& operator=(const IndexReadHandle&) = delete;

    explicit operator bool() const noexcept {
        return runtime_ != nullptr;
    }
    void reset();
    uint32_t indexId() const;

private:
    std::shared_ptr<IndexRuntime> runtime_;
};

// ==================== 单索引运行时 ====================

class IndexRuntime : public std::enable_shared_from_this<IndexRuntime> {
public:
    /// 创建参数（不可变；表名只在这里解析一次，避免"表名/ID 各算一遍"的错配）。
    struct Descriptor {
        uint32_t index_id = 0;
        uint32_t label_id = 0; ///< 顶点/边标签 id
        bool is_edge = false;
        bool unique = false;
        std::string index_table; ///< vidx_<id> / eidx_...
        std::string delta_table; ///< idx_delta_<id>
        IndexPhase initial_phase = IndexPhase::kScan;
        IndexLifecycle initial_lifecycle = IndexLifecycle::kActive;
        uint64_t public_generation = 0;
    };

    explicit IndexRuntime(Descriptor desc)
        : desc_(std::move(desc)), phase_(desc_.initial_phase), lifecycle_(desc_.initial_lifecycle),
          public_generation_(desc_.public_generation), started_at_(std::chrono::steady_clock::now()),
          started_at_unix_ms_(
              std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                  .count()) {}

    IndexRuntime(const IndexRuntime&) = delete;
    IndexRuntime& operator=(const IndexRuntime&) = delete;

    // ---- 不可变描述 ----
    const Descriptor& descriptor() const noexcept {
        return desc_;
    }
    uint32_t indexId() const noexcept {
        return desc_.index_id;
    }
    uint32_t labelId() const noexcept {
        return desc_.label_id;
    }
    bool isEdge() const noexcept {
        return desc_.is_edge;
    }
    bool unique() const noexcept {
        return desc_.unique;
    }
    const std::string& indexTable() const noexcept {
        return desc_.index_table;
    }
    const std::string& deltaTable() const noexcept {
        return desc_.delta_table;
    }

    // ---- phase / lifecycle（原子量，不持锁读） ----
    IndexPhase phase() const noexcept {
        return phase_.load(std::memory_order_acquire);
    }
    IndexLifecycle lifecycle() const noexcept {
        return lifecycle_.load(std::memory_order_acquire);
    }
    /// 合法迁移才生效；返回是否发生迁移（同态/非法返回 false）。
    bool transitionPhase(IndexPhase next) noexcept;
    bool transitionLifecycle(IndexLifecycle next) noexcept;

    // ---- 准入（写路径） ----
    bool admissionOpen() const noexcept {
        return admission_open_.load(std::memory_order_acquire);
    }
    /// 两阶段准入：先 ++ 再复查。成功返回凭证；失败返回 nullopt（调用方应 RetryLater）。
    std::optional<IndexWriteGuard> tryEnter();
    /// `inflight--`（由 Guard 析构调用；业务代码不要直接调）。
    void releaseAdmission() noexcept;
    /// 关闸（不等待）。返回关闸前是否开着。
    bool closeAdmission();
    /// 关闸并等在飞写者归零；超时返回 false（调用方应回退 CATCHUP，见设计 §8.3）。
    bool closeAndWait(uint32_t timeout_ms);
    /// 重新开闸（收尾失败/取消回退时必须调用，I8）。
    void reopenAdmission();
    /// 异步等待闸门重新打开。只在调用方当前**不持有任何事务级准入**时使用；
    /// 醒来后必须重新 route，不能假设自己已获准。
    folly::coro::Task<void> waitForAdmission();
    bool waitDrained(uint32_t timeout_ms);
    /// 等读句柄归零（DROP 清理用）；超时返回 false（调用方转 PENDING_PURGE）。
    bool waitReadersZero(uint32_t timeout_ms);
    uint32_t inflight() const noexcept {
        return inflight_.load(std::memory_order_acquire);
    }

    // ---- 读凭证（查询执行） ----
    /// 只有 PUBLIC 且数据仍保留（ACTIVE/DROPPING/PENDING_PURGE）才允许；PURGING 之后拒绝。
    std::optional<IndexReadHandle> acquireRead();
    void releaseRead() noexcept;
    uint32_t readers() const noexcept {
        return readers_.load(std::memory_order_acquire);
    }

    // ---- 可观测 ----
    uint64_t publicGeneration() const noexcept {
        return public_generation_.load(std::memory_order_acquire);
    }
    void setPublicGeneration(uint64_t generation) noexcept {
        public_generation_.store(generation, std::memory_order_release);
    }
    double progress() const noexcept {
        return progress_.load(std::memory_order_acquire);
    }
    void setProgress(double percent) noexcept {
        progress_.store(percent, std::memory_order_release);
    }
    uint32_t deltaBacklog() const noexcept {
        return delta_backlog_.load(std::memory_order_acquire);
    }
    void setDeltaBacklog(uint32_t rows) noexcept {
        delta_backlog_.store(rows, std::memory_order_release);
    }
    /// 增量维护（写入 +1、重放 -applied），结果钳到 [0, UINT32_MAX]。
    void adjustDeltaBacklog(int64_t delta) noexcept {
        uint32_t current = delta_backlog_.load(std::memory_order_acquire);
        while (true) {
            const int64_t next_signed = static_cast<int64_t>(current) + delta;
            const uint32_t next = next_signed <= 0 ? 0u : static_cast<uint32_t>(next_signed);
            if (delta_backlog_.compare_exchange_weak(current, next, std::memory_order_acq_rel))
                return;
        }
    }
    std::string lastError() const;
    void setLastError(std::string error);
    std::chrono::steady_clock::time_point startedAt() const noexcept {
        return started_at_;
    }
    int64_t startedAtUnixMs() const noexcept {
        return started_at_unix_ms_;
    }

private:
    Descriptor desc_;

    std::atomic<IndexPhase> phase_;
    std::atomic<IndexLifecycle> lifecycle_;
    std::atomic<bool> admission_open_{true};
    mutable std::mutex gate_mu_;
    std::shared_ptr<folly::coro::Baton> admission_baton_;
    std::atomic<uint32_t> inflight_{0};
    std::atomic<uint32_t> readers_{0};
    std::atomic<uint64_t> public_generation_;

    std::atomic<double> progress_{0.0};
    std::atomic<uint32_t> delta_backlog_{0};
    std::chrono::steady_clock::time_point started_at_;
    int64_t started_at_unix_ms_ = 0;

    mutable std::mutex wait_mu_;
    std::condition_variable wait_cv_;

    mutable std::mutex error_mu_;
    std::string last_error_;
};

// ==================== 每图注册表 ====================

class IndexRuntimeRegistry {
public:
    IndexRuntimeRegistry() = default;
    IndexRuntimeRegistry(const IndexRuntimeRegistry&) = delete;
    IndexRuntimeRegistry& operator=(const IndexRuntimeRegistry&) = delete;

    /// 幂等创建：已存在则返回现有 runtime（不覆盖）。`index_id == 0` 拒绝（返回 nullptr）。
    std::shared_ptr<IndexRuntime> createOrUpdate(IndexRuntime::Descriptor desc);
    std::shared_ptr<IndexRuntime> find(uint32_t index_id) const;
    /// 写路径用：某标签下所有索引（顶点/边分开）。
    std::vector<std::shared_ptr<IndexRuntime>> forLabel(uint32_t label_id, bool is_edge) const;
    /// 仅当 lifecycle 已到 PURGED 时允许移除。
    bool erase(uint32_t index_id);
    size_t size() const;

    /// 规划器（新计划）是否可选；`txn_start_generation` 来自事务的 SchemaPin。
    bool plannerSelectable(uint32_t index_id, uint64_t txn_start_generation) const;
    /// 写路径准入（转发到 runtime 的两阶段协议）；不存在/已关闸返回 nullopt。
    std::optional<IndexWriteGuard> tryEnter(uint32_t index_id);
    /// 读凭证；不存在/已 PURGING 返回 nullopt。
    std::optional<IndexReadHandle> acquireRead(uint32_t index_id);

    /// 观测/测试快照（共享所有权，不持锁回调）。
    std::vector<std::shared_ptr<IndexRuntime>> snapshot() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<uint32_t, std::shared_ptr<IndexRuntime>> runtimes_;
};

// ==================== inline 实现 ====================

inline IndexWriteGuard::~IndexWriteGuard() {
    reset();
}

inline void IndexWriteGuard::reset() {
    if (runtime_) {
        runtime_->releaseAdmission();
        runtime_.reset();
    }
}

inline uint32_t IndexWriteGuard::indexId() const {
    return runtime_ ? runtime_->indexId() : 0;
}

inline IndexReadHandle::~IndexReadHandle() {
    reset();
}

inline void IndexReadHandle::reset() {
    if (runtime_) {
        runtime_->releaseRead();
        runtime_.reset();
    }
}

inline uint32_t IndexReadHandle::indexId() const {
    return runtime_ ? runtime_->indexId() : 0;
}

inline bool IndexRuntime::transitionPhase(IndexPhase next) noexcept {
    IndexPhase current = phase_.load(std::memory_order_acquire);
    while (true) {
        if (!indexPhaseTransitionAllowed(current, next))
            return false;
        if (phase_.compare_exchange_weak(current, next, std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
        // current 已被刷新为最新值，重试判定。
    }
}

inline bool IndexRuntime::transitionLifecycle(IndexLifecycle next) noexcept {
    IndexLifecycle current = lifecycle_.load(std::memory_order_acquire);
    while (true) {
        if (!indexLifecycleTransitionAllowed(current, next))
            return false;
        if (lifecycle_.compare_exchange_weak(current, next, std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
    }
}

inline std::optional<IndexWriteGuard> IndexRuntime::tryEnter() {
    if (!admission_open_.load(std::memory_order_acquire))
        return std::nullopt;
    inflight_.fetch_add(1, std::memory_order_acq_rel);
    // 两阶段复查：关闸方 CAS 之后会以 acquire 轮询 inflight，
    // 因此"准入成功"与"关闸"线性化（设计 §15.1 / I5）。
    if (!admission_open_.load(std::memory_order_acquire)) {
        releaseAdmission();
        return std::nullopt;
    }
    return IndexWriteGuard(shared_from_this());
}

inline void IndexRuntime::releaseAdmission() noexcept {
    inflight_.fetch_sub(1, std::memory_order_acq_rel);
    { std::lock_guard<std::mutex> lock(wait_mu_); }
    wait_cv_.notify_all();
}

inline bool IndexRuntime::waitDrained(uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(wait_mu_);
    const auto drained = [this] { return inflight_.load(std::memory_order_acquire) == 0; };
    if (drained())
        return true;
    if (timeout_ms == 0)
        return false;
    return wait_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), drained);
}

inline bool IndexRuntime::closeAndWait(uint32_t timeout_ms) {
    closeAdmission();
    return waitDrained(timeout_ms);
}

inline std::optional<IndexReadHandle> IndexRuntime::acquireRead() {
    if (phase() != IndexPhase::kPublic || !indexDataRetainedForReaders(lifecycle()))
        return std::nullopt;
    readers_.fetch_add(1, std::memory_order_acq_rel);
    return IndexReadHandle(shared_from_this());
}

inline void IndexRuntime::releaseRead() noexcept {
    readers_.fetch_sub(1, std::memory_order_acq_rel);
    { std::lock_guard<std::mutex> lock(wait_mu_); }
    wait_cv_.notify_all();
}

inline bool IndexRuntime::waitReadersZero(uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(wait_mu_);
    const auto drained = [this] { return readers_.load(std::memory_order_acquire) == 0; };
    if (drained())
        return true;
    if (timeout_ms == 0)
        return false;
    return wait_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), drained);
}

inline std::string IndexRuntime::lastError() const {
    std::lock_guard<std::mutex> lock(error_mu_);
    return last_error_;
}

inline void IndexRuntime::setLastError(std::string error) {
    std::lock_guard<std::mutex> lock(error_mu_);
    last_error_ = std::move(error);
}

inline std::shared_ptr<IndexRuntime> IndexRuntimeRegistry::createOrUpdate(IndexRuntime::Descriptor desc) {
    if (desc.index_id == 0)
        return nullptr;
    std::lock_guard<std::mutex> lock(mu_);
    auto it = runtimes_.find(desc.index_id);
    if (it != runtimes_.end())
        return it->second;
    auto runtime = std::make_shared<IndexRuntime>(std::move(desc));
    runtimes_.emplace(runtime->indexId(), runtime);
    return runtime;
}

inline std::shared_ptr<IndexRuntime> IndexRuntimeRegistry::find(uint32_t index_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = runtimes_.find(index_id);
    return it == runtimes_.end() ? nullptr : it->second;
}

inline std::vector<std::shared_ptr<IndexRuntime>> IndexRuntimeRegistry::forLabel(uint32_t label_id,
                                                                                 bool is_edge) const {
    std::vector<std::shared_ptr<IndexRuntime>> out;
    std::lock_guard<std::mutex> lock(mu_);
    out.reserve(runtimes_.size());
    for (const auto& [id, runtime] : runtimes_) {
        if (runtime->labelId() == label_id && runtime->isEdge() == is_edge)
            out.push_back(runtime);
    }
    return out;
}

inline bool IndexRuntimeRegistry::erase(uint32_t index_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = runtimes_.find(index_id);
    if (it == runtimes_.end())
        return false;
    if (it->second->lifecycle() != IndexLifecycle::kPurged)
        return false;
    runtimes_.erase(it);
    return true;
}

inline size_t IndexRuntimeRegistry::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return runtimes_.size();
}

inline bool IndexRuntimeRegistry::plannerSelectable(uint32_t index_id, uint64_t txn_start_generation) const {
    auto runtime = find(index_id);
    if (!runtime)
        return false;
    return indexPlannerSelectable(runtime->phase(), runtime->lifecycle(), runtime->publicGeneration(),
                                  txn_start_generation);
}

inline std::optional<IndexWriteGuard> IndexRuntimeRegistry::tryEnter(uint32_t index_id) {
    auto runtime = find(index_id);
    if (!runtime)
        return std::nullopt;
    return runtime->tryEnter();
}

inline std::optional<IndexReadHandle> IndexRuntimeRegistry::acquireRead(uint32_t index_id) {
    auto runtime = find(index_id);
    if (!runtime)
        return std::nullopt;
    return runtime->acquireRead();
}

inline std::vector<std::shared_ptr<IndexRuntime>> IndexRuntimeRegistry::snapshot() const {
    std::vector<std::shared_ptr<IndexRuntime>> out;
    std::lock_guard<std::mutex> lock(mu_);
    out.reserve(runtimes_.size());
    for (const auto& [id, runtime] : runtimes_)
        out.push_back(runtime);
    return out;
}

} // namespace eugraph
