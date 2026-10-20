#pragma once

#include <cerrno>
#include <string>
#include <wiredtiger.h>

namespace eugraph {

class WtCursor {
public:
    WtCursor() = default;
    WtCursor(WT_SESSION* session, const std::string& table_name);
    ~WtCursor();

    WtCursor(WtCursor&&) noexcept;
    WtCursor& operator=(WtCursor&&) noexcept;
    WtCursor(const WtCursor&) = delete;
    WtCursor& operator=(const WtCursor&) = delete;

    WT_CURSOR* get() const {
        return cursor_;
    }
    WT_CURSOR* operator->() const {
        return cursor_;
    }
    explicit operator bool() const {
        return cursor_ != nullptr;
    }
    /// 打开失败时的 WT 错误码（0 = 成功）✓：调用方必须把失败粘到会话上 ✓
    [[nodiscard]] int error() const noexcept {
        return error_;
    }

private:
    WT_CURSOR* cursor_ = nullptr;
    int error_ = EINVAL; // 默认构造 = 没有 cursor（不是成功）✓
};

} // namespace eugraph
