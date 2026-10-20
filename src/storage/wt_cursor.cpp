#include "storage/wt_cursor.hpp"
#include <spdlog/spdlog.h>

namespace eugraph {

WtCursor::WtCursor(WT_SESSION* session, const std::string& table_name) {
    if (session == nullptr) {
        error_ = EINVAL;
        spdlog::error("Failed to open cursor on {}: null WT session", table_name);
        return;
    }
    error_ = session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &cursor_);
    if (error_ != 0) {
        spdlog::error("Failed to open cursor on {}: error {}", table_name, error_);
        cursor_ = nullptr;
    }
}

WtCursor::~WtCursor() {
    if (cursor_) {
        cursor_->close(cursor_);
        cursor_ = nullptr;
    }
}

WtCursor::WtCursor(WtCursor&& other) noexcept : cursor_(other.cursor_), error_(other.error_) {
    other.cursor_ = nullptr;
    other.error_ = 0;
}

WtCursor& WtCursor::operator=(WtCursor&& other) noexcept {
    if (this != &other) {
        if (cursor_) {
            cursor_->close(cursor_);
        }
        cursor_ = other.cursor_;
        error_ = other.error_;
        other.cursor_ = nullptr;
        other.error_ = 0;
    }
    return *this;
}

} // namespace eugraph
