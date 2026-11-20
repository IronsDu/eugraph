#pragma once
/// 可复用的索引 E2E 测试环境（从 test_index_e2e.cpp 抽出）。
///
/// 数据目录：默认 `$TMPDIR`，可用 `EUGRAPH_TEST_DATA_DIR` 指到磁盘目录
/// （AGENTS 要求测试数据不落 tmpfs）。

#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/session/graph_session.hpp"
#include "storage/session/schema_generation_tracker.hpp"
#include "storage/session/session_pool.hpp"
#include "test_sync_store.hpp"

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include <unistd.h>

namespace eugraph::test {

inline std::string makeTempDir(const std::string& suffix) {
    const char* configured = std::getenv("EUGRAPH_TEST_DATA_DIR");
    const std::filesystem::path base = (configured != nullptr && *configured != '\0')
                                           ? std::filesystem::path(configured)
                                           : std::filesystem::temp_directory_path();
    auto dir = base / ("eugraph_idx_e2e_" + suffix + "_" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir.string();
}

struct TestEnv {
    std::string db_path;
    std::unique_ptr<TestSyncGraphDataStore> data_store;
    std::unique_ptr<TestSyncGraphMetaStore> meta_store;
    std::shared_ptr<SessionPool> session_pool;
    std::unique_ptr<IoScheduler> io;
    std::unique_ptr<AsyncGraphMetaStore> async_meta;
    std::unique_ptr<GraphSession> async_session;
    std::unique_ptr<AsyncGraphDataStore> async_data;
    std::shared_ptr<SchemaGenerationTracker> schema_generations;

    void init(const std::string& name) {
        db_path = makeTempDir(name);
        data_store = std::make_unique<TestSyncGraphDataStore>();
        meta_store = std::make_unique<TestSyncGraphMetaStore>();
        ASSERT_TRUE(data_store->open(db_path + "/data"));
        ASSERT_TRUE(meta_store->open(db_path + "/meta"));

        io = std::make_unique<IoScheduler>(1);

        async_meta = std::make_unique<AsyncGraphMetaStore>();
        auto ok = folly::coro::blockingWait(async_meta->open(*meta_store, *io));
        ASSERT_TRUE(ok);

        async_session = std::make_unique<GraphSession>(data_store->conn().make_session());
        async_data = std::make_unique<AsyncGraphDataStore>(*data_store, *io, *async_session);
        session_pool = std::make_shared<SessionPool>(8, [this]() { return data_store->conn().make_session(); });
        schema_generations = std::make_shared<SchemaGenerationTracker>();
    }

    void shutdown() {
        async_data.reset();
        async_session.reset();
        folly::coro::blockingWait(async_meta->close());
        async_meta.reset();
        io.reset();
        session_pool.reset(); // ★ 会话先于连接销毁 ✓
        data_store->close();
        meta_store->close();
        std::filesystem::remove_all(db_path);
    }
};

/// Temporarily set an environment variable for a test and restore it on exit.
class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const char* value) : name_(name) {
        const char* old = std::getenv(name);
        if (old != nullptr) {
            had_old_ = true;
            old_ = old;
        }
        setenv(name, value, 1);
    }
    ~ScopedEnvVar() {
        if (had_old_)
            setenv(name_.c_str(), old_.c_str(), 1);
        else
            unsetenv(name_.c_str());
    }
    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
    std::string name_;
    std::string old_;
    bool had_old_ = false;
};

} // namespace eugraph::test
