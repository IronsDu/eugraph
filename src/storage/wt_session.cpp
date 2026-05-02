#include "storage/wt_session.hpp"
#include <cerrno>
#include <spdlog/spdlog.h>

namespace eugraph {

WtSession::WtSession(WT_CONNECTION* conn) {
    int ret = conn->open_session(conn, nullptr, nullptr, &session_);
    if (ret != 0) {
        spdlog::error("Failed to open session: error {}", ret);
    }
}

WtSession::~WtSession() {
    if (session_) {
        session_->close(session_, nullptr);
        session_ = nullptr;
    }
}

WtSession::WtSession(WtSession&& other) noexcept : session_(other.session_) {
    other.session_ = nullptr;
}

WtSession& WtSession::operator=(WtSession&& other) noexcept {
    if (this != &other) {
        if (session_) {
            session_->close(session_, nullptr);
        }
        session_ = other.session_;
        other.session_ = nullptr;
    }
    return *this;
}

WtCursor WtSession::openCursor(const std::string& table_name) {
    return WtCursor(session_, table_name);
}

bool WtSession::createTable(const char* name, const char* config) {
    int ret = session_->create(session_, name, config);
    if (ret != 0 && ret != EBUSY) {
        spdlog::error("Failed to create table {}: error {}", name, ret);
        return false;
    }
    return true;
}

} // namespace eugraph
