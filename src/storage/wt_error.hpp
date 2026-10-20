#pragma once

// WT 错误码 → `TxnErrorClass` 的**唯一分类入口** ✓（纯函数，可单测 ✓）
//
// 设计依据：docs/storage/session-cursor-pool-design.md §3（事务错误模型）/ §5。
// 调用方：
//  - `GraphSession`：begin/commit 的错误分类 ✓
//  - `wt::table_*`：数据 op 失败时经 `GraphSession::fail_from_wt()` 粘住 ✓
//  - `GraphSession`：数据 op 后禁止提交 ✓

#include "storage/data/session_types.hpp"

#include <cerrno>
#include <wiredtiger.h>

namespace eugraph {

/// 把 WT 返回码分类（0 = 成功 ⇒ None）✓
[[nodiscard]] inline TxnErrorClass classifyWtError(int ret) noexcept {
    if (ret == 0)
        return TxnErrorClass::None;
    switch (ret) {
    case EBUSY: // WT_BUSY 即 EBUSY：可重试 ✓
        return TxnErrorClass::Busy;
    case WT_ROLLBACK:
        return TxnErrorClass::Rollback;
    case WT_CACHE_OVERFLOW:
        return TxnErrorClass::CacheOverflow;
    case WT_PANIC:
        return TxnErrorClass::Panic;
    default:
        return TxnErrorClass::Unknown;
    }
}

/// 人类可读描述（日志/错误消息用）✓
[[nodiscard]] inline const char* describeWtError(int ret) noexcept {
    switch (ret) {
    case 0:
        return "success";
    case EBUSY:
        return "WT_BUSY (retryable)";
    case WT_ROLLBACK:
        return "WT_ROLLBACK (transaction invalidated)";
    case WT_CACHE_OVERFLOW:
        return "WT_CACHE_OVERFLOW (transaction too large)";
    case WT_PANIC:
        return "WT_PANIC (storage engine is shutting down)";
    case WT_NOTFOUND:
        return "WT_NOTFOUND";
    default:
        return "WiredTiger error";
    }
}

} // namespace eugraph
