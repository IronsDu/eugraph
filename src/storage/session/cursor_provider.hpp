#pragma once

// cursor 的**所有权边界**（docs/storage/session-cursor-pool-design.md §3）✓
//
//  - `CursorHandle`：move-only 句柄，二选一：
//      * **borrowed**：cursor 归 `CursorProvider`（会话缓存），析构只“归还”、不 close ✓
//      * **owned**：cursor 归句柄，析构即 close ✓（扫描用）
//    打开失败不抛异常，而是 `valid()==false` + `error()` ✓（调用方必须粘到会话 ✓）
//  - `CursorProvider`：`acquire()`（point op 复用）/ `acquire_scan()`（独立扫描）/ `clear()` ✓
//    默认实现 = 每会话缓存；未来替换成 cursor 池时所有数据 op 签名不变 ✓
//
// 生命周期：borrowed 句柄**不得**活过 provider（会话）；owned 句柄无此约束 ✓。

#include "storage/wt_cursor.hpp"

#include <cerrno>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace eugraph {

class CursorProvider;

/// move-only cursor 句柄（borrowed / owned 二选一）✓
class CursorHandle {
public:
    CursorHandle() = default;
    ~CursorHandle();

    CursorHandle(CursorHandle&& other) noexcept;
    CursorHandle& operator=(CursorHandle&& other) noexcept;
    CursorHandle(const CursorHandle&) = delete;
    CursorHandle& operator=(const CursorHandle&) = delete;

    /// 借用 provider 的 cursor（provider 必须活得比句柄久 ✓）
    [[nodiscard]] static CursorHandle borrow(CursorProvider& provider, WtCursor& cursor) noexcept;
    /// 独占一个 cursor（扫描用；析构即 close ✓）
    [[nodiscard]] static CursorHandle own(std::unique_ptr<WtCursor> cursor) noexcept;

    /// 原生 WT cursor（未打开 ⇒ nullptr）✓
    [[nodiscard]] WT_CURSOR* get() const noexcept {
        return cursor_ != nullptr ? cursor_->get() : nullptr;
    }
    [[nodiscard]] WtCursor& operator*() const noexcept {
        return *cursor_;
    }
    [[nodiscard]] WtCursor* operator->() const noexcept {
        return cursor_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return cursor_ != nullptr && static_cast<bool>(*cursor_);
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return valid();
    }
    /// 打开失败时的 WT 错误码（0 = 成功）✓
    [[nodiscard]] int error() const noexcept {
        return error_;
    }

    /// 幂等：borrowed ⇒ 归还 provider；owned ⇒ close；随后句柄为空 ✓
    void reset() noexcept;

private:
    CursorHandle(WtCursor* cursor, CursorProvider* provider, int error) noexcept
        : cursor_(cursor), provider_(provider), error_(error) {}

    WtCursor* cursor_ = nullptr;
    std::unique_ptr<WtCursor> owned_;
    CursorProvider* provider_ = nullptr;
    int error_ = 0;
};

/// cursor 提供者：默认实现 = 每会话缓存；未来可替换为 cursor 池 ✓
class CursorProvider {
public:
    virtual ~CursorProvider() = default;

    /// point op 用的**会话级缓存** cursor（同表复用 ✓）
    [[nodiscard]] virtual CursorHandle acquire(const std::string& table) = 0;
    /// 扫描用的**独立** cursor（同表可并发多个 ✓，不复用缓存 ✓）
    [[nodiscard]] virtual CursorHandle acquire_scan(const std::string& table) = 0;
    /// 关闭缓存中的全部 cursor（会话收尾/复用 ✓）
    virtual void clear() noexcept = 0;
    /// borrowed 句柄析构时归还（默认缓存语义无须动作 ✓；池化实现可在此回收 ✓）
    virtual void release(WtCursor& /*cursor*/) noexcept {}
};

/// 默认 provider：1 个 WT session + 按表名缓存的 cursor 映射 ✓
class SessionCursorProvider final : public CursorProvider {
public:
    explicit SessionCursorProvider(WT_SESSION* session) noexcept : session_(session) {}
    ~SessionCursorProvider() override = default;

    SessionCursorProvider(const SessionCursorProvider&) = delete;
    SessionCursorProvider& operator=(const SessionCursorProvider&) = delete;

    [[nodiscard]] CursorHandle acquire(const std::string& table) override {
        auto it = cursors_.find(table);
        if (it == cursors_.end())
            it = cursors_.emplace(table, WtCursor(session_, table)).first;
        return CursorHandle::borrow(*this, it->second);
    }

    [[nodiscard]] CursorHandle acquire_scan(const std::string& table) override {
        return CursorHandle::own(std::make_unique<WtCursor>(session_, table));
    }

    void clear() noexcept override {
        cursors_.clear();
    }

private:
    WT_SESSION* session_ = nullptr;
    std::unordered_map<std::string, WtCursor> cursors_; // 会话级缓存 ✓
};

// ── CursorHandle 内联实现（需要 CursorProvider 完整定义）────────────────────

inline CursorHandle::~CursorHandle() {
    reset();
}

inline CursorHandle::CursorHandle(CursorHandle&& other) noexcept
    : cursor_(other.cursor_), owned_(std::move(other.owned_)), provider_(other.provider_), error_(other.error_) {
    other.cursor_ = nullptr;
    other.provider_ = nullptr;
    other.error_ = 0;
}

inline CursorHandle& CursorHandle::operator=(CursorHandle&& other) noexcept {
    if (this != &other) {
        reset();
        cursor_ = other.cursor_;
        owned_ = std::move(other.owned_);
        provider_ = other.provider_;
        error_ = other.error_;
        other.cursor_ = nullptr;
        other.provider_ = nullptr;
        other.error_ = 0;
    }
    return *this;
}

inline CursorHandle CursorHandle::borrow(CursorProvider& provider, WtCursor& cursor) noexcept {
    return CursorHandle(&cursor, &provider, cursor.error());
}

inline CursorHandle CursorHandle::own(std::unique_ptr<WtCursor> cursor) noexcept {
    CursorHandle handle;
    handle.error_ = cursor ? cursor->error() : EINVAL;
    handle.cursor_ = cursor.get();
    handle.owned_ = std::move(cursor);
    return handle;
}

inline void CursorHandle::reset() noexcept {
    if (cursor_ == nullptr && !owned_)
        return;
    WtCursor* cursor = cursor_;
    CursorProvider* provider = provider_;
    cursor_ = nullptr;
    provider_ = nullptr;
    error_ = 0;
    if (owned_) {
        owned_.reset(); // owned：析构即 close ✓
    } else if (cursor != nullptr && provider != nullptr) {
        provider->release(*cursor); // borrowed：归还 provider ✓
    }
}

} // namespace eugraph
