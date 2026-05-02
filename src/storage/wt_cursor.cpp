#include "storage/wt_cursor.hpp"
#include <spdlog/spdlog.h>

namespace eugraph {

WtCursor::WtCursor(WT_SESSION* session, const std::string& table_name) {
    int ret = session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &cursor_);
    if (ret != 0) {
        spdlog::error("Failed to open cursor on {}: error {}", table_name, ret);
    }
}

WtCursor::~WtCursor() {
    if (cursor_) {
        cursor_->close(cursor_);
        cursor_ = nullptr;
    }
}

WtCursor::WtCursor(WtCursor&& other) noexcept : cursor_(other.cursor_) {
    other.cursor_ = nullptr;
}

WtCursor& WtCursor::operator=(WtCursor&& other) noexcept {
    if (this != &other) {
        if (cursor_) {
            cursor_->close(cursor_);
        }
        cursor_ = other.cursor_;
        other.cursor_ = nullptr;
    }
    return *this;
}

} // namespace eugraph
