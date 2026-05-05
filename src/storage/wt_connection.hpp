#pragma once

#include "storage/wt_session.hpp"

#include <string>
#include <wiredtiger.h>

namespace eugraph {

class WtConnection {
public:
    WtConnection() = default;
    ~WtConnection();

    WtConnection(WtConnection&&) noexcept;
    WtConnection& operator=(WtConnection&&) noexcept;
    WtConnection(const WtConnection&) = delete;
    WtConnection& operator=(const WtConnection&) = delete;

    bool open(const std::string& db_path, const char* config);
    void close();
    WT_CONNECTION* get() const {
        return conn_;
    }
    WT_CONNECTION* operator->() const {
        return conn_;
    }
    explicit operator bool() const {
        return conn_ != nullptr;
    }

    WtSession openSession();

private:
    WT_CONNECTION* conn_ = nullptr;
};

} // namespace eugraph
