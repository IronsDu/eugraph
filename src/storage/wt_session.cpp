#include "storage/wt_session.hpp"
#include <cerrno>
#include <spdlog/spdlog.h>

namespace eugraph {

WtSession::WtSession(WT_CONNECTION* conn) {
    if (conn == nullptr) {
        return; // no connection (never opened or already closed): stay empty
    }

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

} // namespace eugraph
