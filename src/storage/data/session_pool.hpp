#pragma once

// `SessionPool`：每图一个、**拥有**全部 `GraphSession` 的有界池 ✓
// `SessionLease`：**非拥有**的 move-only 租约，析构即归还 ✓
//
// 设计依据：docs/storage/session-cursor-pool-design.md §3 / §4。
//  - R1 创建者 ≠ 持有者：池拥有会话 ✓，租约只借用 ✓
//  - **生命周期安全**：租约与池共享同一份 `SessionPoolCore`（shared_ptr）✓
//    ⇒ 池先销毁、租约后释放也不会悬空 ✓
//  - **clear() 只关闭空闲会话** ✓：已借出的会话由持有它的租约在 `put()` 时关闭/销毁 ✓
//  - 归还的唯一入口是 `SessionPoolCore::put()` ✓：`GraphSession::close()`（关 cursor + 未提交回滚 ✓）；
//    毒化会话 ⇒ **销毁** ✗ 不回池 ✓；否则 `reset_for_reuse()` + 归位 ✓
//  - 有界；耗尽 ⇒ nullopt（不阻塞 ✗）
//  - **不再假装 NUMA**：原 `acquire(node)` 的 8 个桶实际全传 0；参数已删除，
//    等真正接入 current-node 再加 ✓
//  - 池必须在 WT connection 关闭**之前** `clear()` ✓（GraphInstance 成员声明顺序保证 ✓）

#include "storage/data/graph_session.hpp"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eugraph {

/// 池的**共享核心**：租约持 shared_ptr ⇒ 池对象可以先销毁 ✓
/// 只做会话的创建/复用/销毁与借出记账；不碰业务、不开事务 ✓
class SessionPoolCore {
public:
    /// 会话工厂：返回一个**未绑定事务**的 WtSession ✓（生产 = WtConnectionStore::make_session ✓）
    using SessionFactory = std::function<WtSession()>;

    SessionPoolCore(std::size_t max_total, SessionFactory factory)
        : max_total_(max_total), factory_(std::move(factory)) {}
    ~SessionPoolCore() {
        shutdown();
        for (auto& [_, session] : all_)
            if (session)
                session->close();
        all_.clear();
    }

    SessionPoolCore(const SessionPoolCore&) = delete;
    SessionPoolCore& operator=(const SessionPoolCore&) = delete;

    /// 借出一个会话（池耗尽 ⇒ nullopt ✓；不阻塞 ✗）
    [[nodiscard]] std::optional<GraphSession*> acquire() {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_)
            return std::nullopt;

        GraphSession* picked = nullptr;
        if (!idle_.empty()) {
            picked = idle_.back();
            idle_.pop_back();
        } else if (all_.size() < max_total_) {
            WtSession wt_session = factory_ ? factory_() : WtSession{};
            if (!wt_session)
                return std::nullopt; // 工厂失败不占容量 ✓
            auto session = std::make_unique<GraphSession>(std::move(wt_session));
            picked = session.get();
            all_.emplace(picked, std::move(session));
        } else {
            return std::nullopt; // 有界：耗尽 ✓
        }
        leased_.insert(picked);
        return picked;
    }

    /// 归还的唯一入口 ✓（锁外做 close ✓：它可能触发 WT IO ✓）
    void put(GraphSession& session) noexcept {
        // 防御：只接受当前确实借出的会话（重复归还/错配 ⇒ 记日志后忽略 ✓）
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (leased_.erase(&session) == 0) {
                spdlog::error("[SessionPool] put() 收到未借出的会话 ⇒ 忽略（重复归还？）");
                return;
            }
        }

        session.close(); // ★ 关 cursor + **未提交 ⇒ 回滚** ✓（回滚归属在这里 ✓）

        std::unique_ptr<GraphSession> doomed;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (closed_ || session.poisoned()) {
                // 毒化 ⇒ 销毁不回池 ✗；池已关闭 ⇒ 只销毁，不再碰空闲表 ✓
                auto it = all_.find(&session);
                if (it != all_.end()) {
                    doomed = std::move(it->second);
                    all_.erase(it);
                }
                if (session.poisoned())
                    spdlog::error("[SessionPool] 毒化会话 ⇒ 销毁（不回池）");
            } else {
                session.reset_for_reuse();
                idle_.push_back(&session);
            }
        }
        // doomed 在锁外析构（~WtSession 可能触发 IO ✓）
    }

    /// ★ 必须在 connection 关闭前调用 ✓
    /// 只关闭**空闲**会话；借出的会话由对应租约在 `put()` 时关闭 ✓（不会跨线程关别人的 session ✗）
    void shutdown() noexcept {
        std::vector<std::unique_ptr<GraphSession>> doomed;
        std::size_t outstanding = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (closed_)
                return;
            closed_ = true;
            for (auto* session : idle_) {
                auto it = all_.find(session);
                if (it != all_.end()) {
                    doomed.push_back(std::move(it->second));
                    all_.erase(it);
                }
            }
            idle_.clear();
            outstanding = leased_.size();
        }
        for (auto& session : doomed)
            if (session)
                session->close(); // 关 cursor + 未提交回滚 ✓
        if (outstanding > 0)
            spdlog::warn("[SessionPool] 关闭时仍有 {} 个借出的会话；它们由各自租约释放时关闭 ✓", outstanding);
    }

    [[nodiscard]] bool closed() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return closed_;
    }
    [[nodiscard]] std::size_t live() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return all_.size();
    }
    [[nodiscard]] std::size_t idle_count() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return idle_.size();
    }
    /// 当前借出的会话数（未归还）✓
    [[nodiscard]] std::size_t outstanding() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return leased_.size();
    }

private:
    mutable std::mutex mu_;
    std::size_t max_total_;
    SessionFactory factory_;
    std::unordered_map<GraphSession*, std::unique_ptr<GraphSession>> all_; // ★ 池拥有 ✓
    std::vector<GraphSession*> idle_{};
    std::unordered_set<GraphSession*> leased_; // 借出记账（防重复归还 ✓ + outstanding ✓）
    bool closed_ = false;
};

/// 非拥有、move-only 的会话租约 ✓（析构即归还 ✓）
/// 持 `shared_ptr<SessionPoolCore>` ⇒ 池对象先销毁也安全 ✓
class SessionLease {
public:
    SessionLease() = default;
    SessionLease(std::shared_ptr<SessionPoolCore> core, GraphSession* session) noexcept
        : core_(std::move(core)), session_(session) {}

    SessionLease(SessionLease&& other) noexcept : core_(std::move(other.core_)), session_(other.session_) {
        other.session_ = nullptr;
    }
    SessionLease& operator=(SessionLease&& other) noexcept {
        if (this != &other) {
            release();
            core_ = std::move(other.core_);
            session_ = other.session_;
            other.session_ = nullptr;
        }
        return *this;
    }
    SessionLease(const SessionLease&) = delete;
    SessionLease& operator=(const SessionLease&) = delete;
    ~SessionLease() {
        release();
    }

    /// 归还（幂等 ✓）：直接交给池 ✓（池内 close ⇒ 关 cursor + 回滚未提交 ✓）
    void release() noexcept {
        if (session_ == nullptr)
            return;
        if (session_->active())
            spdlog::warn("[SessionLease] 释放时事务仍 active ⇒ 池将回滚 ✓（调用方应显式 commit/rollback）");
        auto core = std::move(core_);
        GraphSession* session = session_;
        session_ = nullptr;
        if (core)
            core->put(*session);
    }

    [[nodiscard]] GraphSession& operator*() const noexcept {
        return *session_;
    }
    [[nodiscard]] GraphSession* operator->() const noexcept {
        return session_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return session_ != nullptr;
    }

private:
    std::shared_ptr<SessionPoolCore> core_;
    GraphSession* session_ = nullptr;
};

/// 每图一个；**拥有**全部 `GraphSession` ✓；有界 ✓
class SessionPool {
public:
    using SessionFactory = SessionPoolCore::SessionFactory;

    SessionPool(std::size_t max_total, SessionFactory factory)
        : core_(std::make_shared<SessionPoolCore>(max_total, std::move(factory))) {}
    ~SessionPool() {
        if (core_)
            core_->shutdown();
    }

    SessionPool(const SessionPool&) = delete;
    SessionPool& operator=(const SessionPool&) = delete;

    /// 取租约（同步 ✓）：池耗尽/已关闭 ⇒ nullopt（上层转明确错误 ✓，不阻塞 ✗）
    [[nodiscard]] std::optional<SessionLease> acquire() {
        auto picked = core_->acquire();
        if (!picked)
            return std::nullopt;
        return SessionLease{core_, *picked};
    }

    /// 归还（通常由 `~SessionLease` 调用 ✓）
    void put(GraphSession& session) noexcept {
        core_->put(session);
    }

    /// ★ 必须在 connection 关闭前调用 ✓（只关空闲会话 ✓）
    void clear() noexcept {
        core_->shutdown();
    }

    [[nodiscard]] bool closed() const noexcept {
        return core_->closed();
    }
    [[nodiscard]] std::size_t live() const noexcept {
        return core_->live();
    }
    [[nodiscard]] std::size_t idle_count() const noexcept {
        return core_->idle_count();
    }
    /// 当前借出的会话数（★ 非零时不得关闭连接 ✓）
    [[nodiscard]] std::size_t outstanding() const noexcept {
        return core_->outstanding();
    }

private:
    std::shared_ptr<SessionPoolCore> core_;
};

} // namespace eugraph
