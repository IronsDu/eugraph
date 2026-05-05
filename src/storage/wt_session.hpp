#pragma once

#include "common/types/constants.hpp"
#include "storage/wt_cursor.hpp"

#include <string>
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
    WT_SESSION* operator->() const {
        return session_;
    }
    explicit operator bool() const {
        return session_ != nullptr;
    }

    WtCursor openCursor(const std::string& table_name);
    bool createTable(const char* name, const char* config = WT_TABLE_CONFIG);

private:
    WT_SESSION* session_ = nullptr;
};

} // namespace eugraph
