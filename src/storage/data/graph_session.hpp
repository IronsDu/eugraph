#pragma once

// `GraphSession`：一次**租约**的独占事务作用域（1 个 WtSession + 可变事务 + cursor provider）✓
//
// 设计依据：docs/storage/session-cursor-pool-design.md §3 / §4 / §5。
//  - 分配者 ≠ 持有者：`SessionPool` 拥有并复用本类型 ✓；本类型只负责**事务状态与收尾顺序** ✓
//  - **直接持有 `WtSession`** ✓：不做半截 backend 抽象（WT 类型本来就在存储层公开；
//    以后若真要换引擎，再在更外层做抽象 ✓）
//  - close()：关全部 cursor → 未提交（或毒化后事务仍开着）⇒ 回滚 ✓（绝不静默丢数据）
//  - **失败即禁止提交**：数据 op 失败经 `fail*()` 粘住 ⇒ `commit()` 直接拒绝并回滚 ✓
//  - 只有不可重试错误才毒化会话 ✓（Busy 可重试，见 `TxnErrorClass`）
//  - 纯事务状态机在 `TxnController`/`StickyError` ⇒ 可脱离 WT 单测 ✓

#include "common/types/graph_types.hpp"
#include "storage/data/cursor_provider.hpp"
#include "storage/data/session_types.hpp"
#include "storage/data/txn_controller.hpp"
#include "storage/wt_error.hpp"
#include "storage/wt_session.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace eugraph {

class SyncGraphDataStore;

class GraphSession {
public:
    /// `cursors == nullptr` ⇒ 默认的每会话缓存 provider ✓（测试可注入假 provider ✓）
    explicit GraphSession(WtSession session, std::shared_ptr<CursorProvider> cursors = nullptr)
        : session_(std::move(session)),
          cursors_(cursors ? std::move(cursors) : std::make_shared<SessionCursorProvider>(session_.get())) {}
    GraphSession(const GraphSession&) = delete;
    GraphSession& operator=(const GraphSession&) = delete;
    ~GraphSession() {
        close();
    } // 兜底：关 cursor + 回滚未提交（不负责归还 ✓）

    bool begin_read() {
        return begin(TxnKind::Read);
    }
    bool begin_write() {
        return begin(TxnKind::Write);
    }

    /// 显式提交（唯一成功路径 ✓）
    /// 先检查粘性错误：本事务出现过任何失败 ⇒ **拒绝提交**并回滚 ✓（绝不把部分写入当成功 ✗）
    TxnOutcome commit() {
        if (err_.has()) {
            const auto& first = err_.outcome();
            TxnOutcome refused{TxnState::Failed, first.wt_error,
                               "commit refused after earlier failure: " + first.message, err_.error_class()};
            rollback();
            return refused;
        }
        auto request = txn_.commit_request();
        if (!request) {
            TxnOutcome out{TxnState::Failed, 0, "commit without active transaction"};
            err_.set(out);
            return out;
        }

        TxnOutcome out;
        if (!session_) {
            out = TxnOutcome{TxnState::Failed, EINVAL, "commit: no WT session", TxnErrorClass::Unknown};
        } else {
            const int ret = session_.get()->commit_transaction(session_.get(), nullptr);
            if (ret == 0)
                out = TxnOutcome{TxnState::Committed, 0, {}, TxnErrorClass::None};
            else
                out = TxnOutcome{TxnState::Failed, ret, describeWtError(ret), classifyWtError(ret)};
        }
        txn_.on_commit_result(out);
        err_.set(out);
        return out;
    }

    /// 回滚（幂等 ✓）；不洗白毒化 ✗
    /// 回滚失败 ⇒ 会话不可信：毒化并粘住错误，池会销毁它（绝不带着未收尾事务回池 ✗）。
    void rollback() noexcept {
        if (txn_.transaction_open() && session_) {
            const int ret = session_.get()->rollback_transaction(session_.get(), nullptr);
            if (ret != 0) {
                spdlog::error("[GraphSession] rollback_transaction failed: {}", describeWtError(ret));
                txn_.poison();
                err_.set(TxnOutcome{TxnState::Failed, ret,
                                    std::string("rollback_transaction failed: ") + describeWtError(ret),
                                    classifyWtError(ret)});
                return; // 保留 transaction_open：由池销毁/会话析构收尾 ✓
            }
        }
        txn_.on_rollback();
    }

    /// 收尾：关全部 cursor → 后端事务仍开着 ⇒ 回滚 ✓（归还由池负责 ✓）
    void close() noexcept {
        if (closed_)
            return;
        closed_ = true;
        // cursor 必须先于事务回滚/会话复用关闭：WtCursor 绑定 WT session ✓
        if (cursors_)
            cursors_->clear();
        if (txn_.transaction_open()) {
            if (session_) {
                const int ret = session_.get()->rollback_transaction(session_.get(), nullptr);
                if (ret != 0) {
                    spdlog::error("[GraphSession] close rollback failed: {}", describeWtError(ret));
                    txn_.poison();
                    err_.set(TxnOutcome{TxnState::Failed, ret,
                                        std::string("close rollback failed: ") + describeWtError(ret),
                                        classifyWtError(ret)});
                    return; // 毒化 ⇒ 池销毁；不谎报“已回滚” ✓
                }
            }
            txn_.on_rollback();
            spdlog::warn("[GraphSession] 未提交就收尾 ⇒ 已回滚（调用方应显式 commit/rollback）");
        }
    }

    /// 记录本事务的一次失败（数据 op / 表原语）✓
    ///  - 任何失败都会阻止后续 commit ✓
    ///  - 不可重试类错误同时毒化会话 ✓（池销毁，不回池）
    void fail(TxnErrorClass error_class, int wt_error, std::string message) {
        err_.set(TxnOutcome{TxnState::Failed, wt_error, std::move(message), error_class});
        if (poisons_session(error_class))
            txn_.poison();
    }
    /// 数据 op 返回 WT 错误码时的统一入口 ✓
    void fail_from_wt(int wt_error, std::string message) {
        fail(classifyWtError(wt_error), wt_error, std::move(message));
    }

    /// 会话级 cursor 缓存（§7 ✓）：同表复用 ✓；close()/end_statement() 统一关闭 ✓
    /// 打开失败 ⇒ `valid()==false` + `error()` ✓（调用方必须 `fail_from_wt()` ✓）
    [[nodiscard]] CursorHandle acquire_cursor(const std::string& table) {
        return cursors_->acquire(table);
    }
    /// 扫描用**独立** cursor（不复用缓存 ✓；可同表并发 ✓）
    [[nodiscard]] CursorHandle acquire_scan_cursor(const std::string& table) {
        return cursors_->acquire_scan(table);
    }
    /// 语句边界（显式事务跨语句/测试）：只清 cursor 缓存，不结束事务 ✓
    void end_statement() noexcept {
        if (cursors_)
            cursors_->clear();
    }
    void reset_for_reuse() noexcept {
        // 非致命失败后事务可能仍开着（例如 Busy 未提交）⇒ 必须先回滚再复位 ✓
        if (txn_.transaction_open())
            rollback();
        if (cursors_)
            cursors_->clear();
        txn_.reset();
        err_.clear();
        closed_ = false; // close() 会置真 ✗ ⇒ 复用前必须复位 ✓
    }

    [[nodiscard]] bool active() const noexcept {
        return txn_.active();
    }
    [[nodiscard]] TxnState state() const noexcept {
        return txn_.state();
    }
    [[nodiscard]] TxnKind kind() const noexcept {
        return txn_.kind();
    }
    [[nodiscard]] bool poisoned() const noexcept {
        return txn_.poisoned();
    }
    /// 本事务是否已出现失败（⇒ 不允许提交 ✓）
    [[nodiscard]] bool transaction_failed() const noexcept {
        return err_.has();
    }
    [[nodiscard]] const StickyError& error() const noexcept {
        return err_;
    }
    [[nodiscard]] bool closed() const noexcept {
        return closed_;
    }

private:
    friend class SyncGraphDataStore;

    /// 仅**存储层**使用（§3 ✓；业务层不得直接读写 WT）
    [[nodiscard]] WT_SESSION* wt_session() const noexcept {
        return session_.get();
    }

    bool begin(TxnKind kind) {
        if (closed_ || txn_.poisoned())
            return false;
        // 先确认新事务真的能开始；失败时**不得**清掉上一个事务的粘性错误 ✗
        // （否则重复 begin 会洗掉失败标记，后续 commit 会把部分写入当成功）
        if (!txn_.begin(kind))
            return false;
        // 新事务成功开始后，才清掉上一个事务留下的非致命错误 ✓
        if (err_.has())
            err_.clear();
        if (!session_) {
            txn_.on_rollback();
            err_.set(TxnOutcome{TxnState::Failed, EINVAL, "begin: no WT session", TxnErrorClass::Unknown});
            return false;
        }
        const int ret = session_.get()->begin_transaction(session_.get(), "isolation=snapshot");
        if (ret != 0) {
            txn_.on_rollback();
            const TxnErrorClass cls = classifyWtError(ret);
            err_.set(TxnOutcome{TxnState::Failed, ret, "begin_transaction failed", cls});
            // begin 失败通常可重试（事务根本没开）；只有引擎 panic 才毒化 ✓
            if (cls == TxnErrorClass::Panic)
                txn_.poison();
            return false;
        }
        return true;
    }

    WtSession session_;                       // 独占 1 个 WT session ✓
    std::shared_ptr<CursorProvider> cursors_; // cursor 所有权边界 ✓（默认每会话缓存）
    TxnController txn_{};
    StickyError err_{};
    bool closed_ = false;
};

/// 可选的非拥有会话引用 ✓（显式事务跨语句传递用；无裸指针 ✓）
using SessionRef = std::optional<std::reference_wrapper<GraphSession>>;

} // namespace eugraph
