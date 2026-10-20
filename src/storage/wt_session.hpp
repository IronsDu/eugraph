#pragma once

#include <wiredtiger.h>

namespace eugraph {

class WtSession {
public:
    WtSession() = default;
    explicit WtSession(WT_CONNECTION* conn);
    ~WtSession();

    WtSession(WtSession&&) noexcept;
    WtSession& operator=(WtSession&&) noexcept;
    WtSession(const WtSession&) = delete;
    WtSession& operator=(const WtSession&) = delete;

    WT_SESSION* get() const {
        return session_;
    }
    explicit operator bool() const {
        return session_ != nullptr;
    }

private:
    WT_SESSION* session_ = nullptr;
};

} // namespace eugraph
