#pragma once

// 会话/事务的**纯逻辑类型**（不依赖 WiredTiger ⇒ 可单测 ✓）
//
// 设计依据：docs/storage/session-cursor-pool-design.md §3、§5。
//  - 提交必须显式；未提交 ⇒ 回滚；提交失败 ⇒ 明确错误（绝不静默 ✗）
//  - 首个失败被"粘住"：事务期间任何失败都**禁止提交** ✓（避免部分写入被当成成功 ✗）
//  - 只有**不可重试**类错误才毒化会话 ✓；Busy 可重试，会话仍可复用 ✓

#include <cstdint>
#include <string>
#include <utility>

namespace eugraph {

enum class TxnKind : uint8_t {
    None,
    Read,
    Write
};

enum class TxnState : uint8_t {
    None,
    Active,
    Committed,
    RolledBack,
    Failed
};

/// WT 错误分类（§5）：决定「能否重试」与「是否毒化会话 ✓」
enum class TxnErrorClass : uint8_t {
    None,          // 成功
    Busy,          // EBUSY：可重试，**不毒化** ✓
    Rollback,      // WT_ROLLBACK：事务失效 ⇒ 毒化，语句边界可换会话重试 ✓
    CacheOverflow, // WT_CACHE_OVERFLOW：事务过大 ⇒ 毒化 ✓
    Panic,         // WT_PANIC：引擎不可信 ⇒ 毒化并停止服务 ✓
    Unknown        // 未知错误 ⇒ 保守毒化 ✓
};

[[nodiscard]] constexpr bool is_retryable(TxnErrorClass cls) noexcept {
    return cls == TxnErrorClass::Busy;
}

/// 是否**必须销毁**该会话（不回池）✓：保守策略——未知错误也算不可信 ✗
[[nodiscard]] constexpr bool poisons_session(TxnErrorClass cls) noexcept {
    return cls == TxnErrorClass::Rollback || cls == TxnErrorClass::CacheOverflow || cls == TxnErrorClass::Panic ||
           cls == TxnErrorClass::Unknown;
}

[[nodiscard]] constexpr const char* to_string(TxnErrorClass cls) noexcept {
    switch (cls) {
    case TxnErrorClass::None:
        return "None";
    case TxnErrorClass::Busy:
        return "Busy";
    case TxnErrorClass::Rollback:
        return "Rollback";
    case TxnErrorClass::CacheOverflow:
        return "CacheOverflow";
    case TxnErrorClass::Panic:
        return "Panic";
    case TxnErrorClass::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

/// 事务结局（替代 bool ✗：调用方需要区分「没有事务」「可重试失败」「毒化失败」✓）
struct TxnOutcome {
    TxnState state = TxnState::None;
    int wt_error = 0; // WiredTiger 错误码；0 表示成功
    std::string message;
    TxnErrorClass error_class = TxnErrorClass::None;

    [[nodiscard]] bool committed() const noexcept {
        return state == TxnState::Committed;
    }
    [[nodiscard]] bool failed() const noexcept {
        return state == TxnState::Failed;
    }
    /// 失败但**可重试**（会话不毒化）✓
    [[nodiscard]] bool retryable() const noexcept {
        return failed() && is_retryable(effective_error_class());
    }
    /// 失败且**必须毒化会话** ✓
    [[nodiscard]] bool poisons_session() const noexcept {
        return failed() && eugraph::poisons_session(effective_error_class());
    }
    /// 未显式分类的失败按 Unknown 处理 ✓（保守：不可重试、毒化）
    [[nodiscard]] TxnErrorClass effective_error_class() const noexcept {
        if (failed() && error_class == TxnErrorClass::None)
            return TxnErrorClass::Unknown;
        return error_class;
    }
};

/// 粘性错误：只记住**第一个**失败，避免被后续错误覆盖 ✓
///
/// 语义分两层：
///  - `has()`：本事务内出现过失败 ⇒ `GraphSession::commit()` 必须拒绝提交 ✓
///  - `fatal()`：该失败会毒化会话 ⇒ 池销毁、不再复用 ✓
class StickyError {
public:
    void set(const TxnOutcome& outcome) {
        if (!has_ && outcome.failed()) {
            outcome_ = outcome;
            has_ = true;
        }
    }
    void set_message(std::string message, TxnErrorClass error_class = TxnErrorClass::Unknown) {
        if (!has_) {
            outcome_.state = TxnState::Failed;
            outcome_.message = std::move(message);
            outcome_.error_class = error_class;
            has_ = true;
        }
    }
    [[nodiscard]] bool has() const noexcept {
        return has_;
    }
    /// 首个错误是否**不可重试**（⇒ 会话毒化）
    [[nodiscard]] bool fatal() const noexcept {
        return has_ && eugraph::poisons_session(outcome_.effective_error_class());
    }
    [[nodiscard]] TxnErrorClass error_class() const noexcept {
        return has_ ? outcome_.effective_error_class() : TxnErrorClass::None;
    }
    [[nodiscard]] const TxnOutcome& outcome() const noexcept {
        return outcome_;
    }
    void clear() noexcept {
        has_ = false;
        outcome_ = {};
    }

private:
    bool has_ = false;
    TxnOutcome outcome_{};
};

} // namespace eugraph
