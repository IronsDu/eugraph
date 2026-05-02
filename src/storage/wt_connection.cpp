#include "storage/wt_connection.hpp"
#include <spdlog/spdlog.h>

namespace eugraph {

WtConnection::~WtConnection() {
    close();
}

bool WtConnection::open(const std::string& db_path, const char* config) {
    if (conn_)
        return true;

    int ret = wiredtiger_open(db_path.c_str(), nullptr, config, &conn_);
    if (ret != 0) {
        spdlog::error("Failed to open WiredTiger: error {}", ret);
        return false;
    }
    return true;
}

void WtConnection::close() {
    if (conn_) {
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
    }
}

WtConnection::WtConnection(WtConnection&& other) noexcept : conn_(other.conn_) {
    other.conn_ = nullptr;
}

WtConnection& WtConnection::operator=(WtConnection&& other) noexcept {
    if (this != &other) {
        close();
        conn_ = other.conn_;
        other.conn_ = nullptr;
    }
    return *this;
}

WtSession WtConnection::openSession() {
    return WtSession(conn_);
}

} // namespace eugraph
