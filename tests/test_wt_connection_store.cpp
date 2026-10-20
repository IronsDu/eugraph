// WtConnectionStore：连接所有者（真实 WT ✓）
#include "storage/wt_connection_store.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {
using eugraph::WtConnectionStore;

class WtConnectionStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = std::filesystem::current_path() / ("eugraph_conn_" + std::string(info->name()));
        std::filesystem::remove_all(dir_);
    }
    void TearDown() override {
        std::filesystem::remove_all(dir_);
    }
    std::filesystem::path dir_;
};
} // namespace

TEST_F(WtConnectionStoreTest, OpenMakeSessionUseThenClose) {
    WtConnectionStore conn;
    ASSERT_TRUE(conn.open(dir_.string()));
    EXPECT_TRUE(conn.is_open());
    EXPECT_TRUE(conn.open(dir_.string())) << "open 必须幂等 ✓";

    { // ★ 会话必须先于连接关闭而析构（§3.2.1 硬规则 ✓）
        auto session = conn.make_session();
        ASSERT_TRUE(static_cast<bool>(session));
        WT_SESSION* raw = session.get();
        ASSERT_EQ(raw->begin_transaction(raw, "isolation=snapshot"), 0);
        EXPECT_EQ(raw->commit_transaction(raw, nullptr), 0);
    }
    {
        auto checkpoint_session = conn.make_session();
        EXPECT_TRUE(conn.checkpoint(checkpoint_session));
    }
    conn.close();
    EXPECT_FALSE(conn.is_open());
    conn.close(); // 幂等 ✓
}

TEST_F(WtConnectionStoreTest, MakeSessionIsIndependentOfOtherSessions) {
    WtConnectionStore conn;
    ASSERT_TRUE(conn.open(dir_.string()));
    auto session = conn.make_session();
    ASSERT_TRUE(static_cast<bool>(session));
    EXPECT_EQ(session.get()->begin_transaction(session.get(), "isolation=snapshot"), 0);
    EXPECT_EQ(session.get()->rollback_transaction(session.get(), nullptr), 0);
}

TEST_F(WtConnectionStoreTest, DestructorClosesSafely) {
    {
        WtConnectionStore conn;
        ASSERT_TRUE(conn.open(dir_.string()));
    }
    // 析构后目录可被删除 ⇒ 连接确实关闭 ✓
    std::filesystem::remove_all(dir_);
    EXPECT_TRUE(!std::filesystem::exists(dir_));
}
