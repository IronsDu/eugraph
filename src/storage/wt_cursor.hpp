#pragma once

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

private:
    WT_CURSOR* cursor_ = nullptr;
};

} // namespace eugraph
