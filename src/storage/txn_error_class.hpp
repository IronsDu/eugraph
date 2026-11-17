#pragma once

// WT 错误分类的**纯类型**（不依赖 WiredTiger，也不依赖会话/事务状态机）。
//
// 为什么单独成文件：`storage/wt_error.hpp`（WT 原语层）需要把返回码分类成
// `TxnErrorClass`，而该 enum 原先定义在 `storage/data/session_types.hpp` ——
// 等于最底层 include 了上层目录。分类语义属于"WT 错误码 → 重试/毒化"这一层，
// 与事务状态机（TxnState / TxnOutcome / StickyError）无关，故拆到独立头，
// 供 `wt_error.hpp`（WT 层）与 `storage/session/session_types.hpp`（会话层）共用。
//
// 设计依据：docs/storage/session-cursor-pool-design.md §5。

#include <cstdint>

namespace eugraph {

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

} // namespace eugraph
