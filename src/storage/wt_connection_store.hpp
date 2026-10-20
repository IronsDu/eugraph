#pragma once

// `WtConnectionStore`：**连接所有者**（每个 WT home 一个：data / meta 各一 ✓）
//
// 职责**仅限**：
//  - open / close ✓（连接生命周期 ✓）
//  - make_session()：创建 WtSession 的**唯一入口** ✓
//  - checkpoint(WtSession&)：显式传入会话 ✓（不再内部偷偷开会话）
// **不做任何业务读写** ✗（那是各 store 的职责 ✓）；**不持有事务** ✗（那是 GraphSession 的 ✓）。

#include "storage/wt_session.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <wiredtiger.h>

namespace eugraph {

class WtConnectionStore {
public:
    WtConnectionStore() = default;
    ~WtConnectionStore() {
        close();
    }

    WtConnectionStore(const WtConnectionStore&) = delete;
    WtConnectionStore& operator=(const WtConnectionStore&) = delete;
    WtConnectionStore(WtConnectionStore&&) = delete;
    WtConnectionStore& operator=(WtConnectionStore&&) = delete;

    /// 打开（幂等 ✓）：默认启用 WAL + fsync-on-commit ✓
    bool open(const std::string& db_path, const std::string& extra_config = "") {
        if (conn_)
            return true;
        std::error_code ec;
        std::filesystem::create_directories(db_path, ec);
        if (ec) {
            spdlog::error("Failed to create database directory: {}", ec.message());
            return false;
        }
        std::string config = "create,log=(enabled=true)";
        if (extra_config.empty())
            config += ",transaction_sync=(enabled=true,method=fsync)";
        else
            config += "," + extra_config;
        const auto t0 = std::chrono::steady_clock::now();
        const int ret = wiredtiger_open(db_path.c_str(), nullptr, config.c_str(), &conn_);
        if (ret != 0) {
            spdlog::error("Failed to open WT connection at {}: {}", db_path, wiredtiger_strerror(ret));
            conn_ = nullptr;
            return false;
        }
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        spdlog::info("Opened WT connection at {} ({}ms)", db_path, ms);
        return true;
    }

    /// 关闭（幂等 ✓）
    void close() noexcept {
        if (!conn_)
            return;
        conn_->close(conn_, nullptr);
        conn_ = nullptr;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return conn_ != nullptr;
    }

    /// 创建 WtSession 的**唯一入口** ✓
    [[nodiscard]] WtSession make_session() {
        return WtSession(conn_);
    }

    /// 显式传入会话做 checkpoint ✓（会话由调用方创建/拥有）
    bool checkpoint(WtSession& session) {
        if (!conn_ || !session)
            return false;
        const int ret = session.get()->checkpoint(session.get(), nullptr);
        if (ret != 0) {
            spdlog::error("Checkpoint failed: error {}", ret);
            return false;
        }
        return true;
    }

private:
    WT_CONNECTION* conn_ = nullptr;
};

} // namespace eugraph
