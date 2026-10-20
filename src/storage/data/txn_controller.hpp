#pragma once

// 事务状态机的**纯逻辑**部分（不含任何 WiredTiger 调用 ⇒ 可单测 ✓）
//
// 设计依据：docs/storage/session-cursor-pool-design.md §3 / §5。
//  - 同一会话同一时刻**只能有一个**活动事务（Active 时再 begin ⇒ 拒绝 ✗）
//  - `transaction_open()` 表示**后端事务仍开着**，与逻辑状态解耦：
//    数据 op 失败毒化后仍需 rollback 收尾 ✓（毒化不等于事务已结束 ✗）
//  - 提交失败按类别处理：可重试（Busy）⇒ 事务结束但**不毒化** ✓；其余 ⇒ 毒化 ✗
//  - 未提交就收尾 ⇒ rollback ✓；Failed 状态**不可被 rollback 洗白** ✗

#include "storage/data/session_types.hpp"

#include <optional>

namespace eugraph {

class TxnController {
public:
    /// 开始新事务；当前已有活动事务 / 后端事务未收尾 / 已被毒化 ⇒ false ✓
    bool begin(TxnKind kind) noexcept {
        if (state_ == TxnState::Active || state_ == TxnState::Failed || txn_open_)
            return false;
        kind_ = kind;
        state_ = TxnState::Active;
        txn_open_ = true;
        return true;
    }

    /// 会话在真正调用 WT commit 之前询问：应提交什么类型的事务？
    /// 没有活动事务 / 后端事务已收尾 ⇒ nullopt（调用方不得盲目提交 ✗）
    [[nodiscard]] std::optional<TxnKind> commit_request() const noexcept {
        if (state_ != TxnState::Active || !txn_open_)
            return std::nullopt;
        return kind_;
    }

    /// 记录提交结果：Committed ⇒ 成功 ✓；可重试失败 ⇒ RolledBack（会话可复用 ✓）；
    /// 其余失败 ⇒ Failed（毒化 ✗）
    void on_commit_result(const TxnOutcome& outcome) noexcept {
        if (!txn_open_) {
            state_ = TxnState::Failed;
            kind_ = TxnKind::None;
            return;
        }
        txn_open_ = false;
        kind_ = TxnKind::None;
        if (outcome.committed()) {
            state_ = TxnState::Committed;
        } else if (outcome.retryable()) {
            state_ = TxnState::RolledBack; // 事务已失效，但会话仍可信 ✓
        } else {
            state_ = TxnState::Failed;
        }
    }

    /// 回滚：关闭后端事务；Active ⇒ RolledBack ✓；Failed **保持 Failed**（不可洗白 ✗）
    void on_rollback() noexcept {
        txn_open_ = false;
        if (state_ == TxnState::Active)
            state_ = TxnState::RolledBack;
        kind_ = TxnKind::None;
    }

    /// 数据 op 出现不可恢复错误时毒化（**不**关闭后端事务：留给 rollback/close 收尾 ✓）
    void poison() noexcept {
        if (state_ != TxnState::Failed)
            state_ = TxnState::Failed;
        kind_ = TxnKind::None;
    }

    [[nodiscard]] TxnState state() const noexcept {
        return state_;
    }
    [[nodiscard]] TxnKind kind() const noexcept {
        return kind_;
    }
    [[nodiscard]] bool active() const noexcept {
        return state_ == TxnState::Active;
    }
    /// 后端事务是否仍开着（毒化后仍可能为 true ⇒ 必须先 rollback ✓）
    [[nodiscard]] bool transaction_open() const noexcept {
        return txn_open_;
    }
    [[nodiscard]] bool poisoned() const noexcept {
        return state_ == TxnState::Failed;
    }

    /// 会话复用前复位（池在 `GraphSession::reset_for_reuse()` 里调用 ✓）：
    /// 已提交/已回滚 ⇒ 回到 None；**毒化保持 Failed** ✗（毒化会话必须销毁，不复用 ✗）
    void reset() noexcept {
        if (state_ != TxnState::Failed) {
            state_ = TxnState::None;
            kind_ = TxnKind::None;
        }
        txn_open_ = false;
    }

private:
    TxnState state_ = TxnState::None;
    TxnKind kind_ = TxnKind::None;
    bool txn_open_ = false; // 后端是否仍开着事务（与逻辑状态解耦 ✓）
};

} // namespace eugraph
