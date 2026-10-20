// 会话租约（非拥有 ✓ 析构即归还 ✓）测试：真实 WT 会话 + 真 SessionPool。
#include "storage/data/session_pool.hpp"
#include "storage/wt_connection_store.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {
using eugraph::SessionLease;
using eugraph::SessionPool;
using eugraph::TxnErrorClass;
using eugraph::TxnKind;
using eugraph::WtConnectionStore;

class SessionLeaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = std::filesystem::current_path() / ("eugraph_lease_" + std::string(info->name()));
        std::filesystem::remove_all(dir_);
        ASSERT_TRUE(conn_.open(dir_.string()));
    }
    void TearDown() override {
        conn_.close();
        std::filesystem::remove_all(dir_);
    }
    [[nodiscard]] SessionPool make_pool(std::size_t max_total = 4) {
        return SessionPool(max_total, [this]() { return conn_.make_session(); });
    }

    std::filesystem::path dir_;
    WtConnectionStore conn_;
};
} // namespace

TEST_F(SessionLeaseTest, DestructorReturnsSessionToPool) {
    SessionPool pool = make_pool();
    {
        auto lease = pool.acquire();
        ASSERT_TRUE(lease.has_value());
        EXPECT_TRUE(lease->valid());
    }
    EXPECT_EQ(pool.idle_count(), 1u) << "析构即归还 ✓";
    EXPECT_EQ(pool.outstanding(), 0u);
}

TEST_F(SessionLeaseTest, ReleaseIsIdempotent) {
    SessionPool pool = make_pool();
    auto lease = pool.acquire();
    ASSERT_TRUE(lease.has_value());
    lease->release();
    lease->release();
    EXPECT_FALSE(lease->valid());
    EXPECT_EQ(pool.idle_count(), 1u) << "只归还一次 ✓";
}

TEST_F(SessionLeaseTest, MoveTransfersOwnership) {
    SessionPool pool = make_pool();
    auto a = pool.acquire();
    ASSERT_TRUE(a.has_value());
    SessionLease b(std::move(*a));
    EXPECT_FALSE(a->valid());
    EXPECT_TRUE(b.valid());
    b.release();
    EXPECT_EQ(pool.idle_count(), 1u) << "移动后只归还一次 ✓";
}

TEST_F(SessionLeaseTest, MoveAssignmentReleasesPreviousLease) {
    SessionPool pool = make_pool();
    auto a = pool.acquire();
    auto b = pool.acquire();
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(pool.outstanding(), 2u);

    *a = std::move(*b); // 覆盖 a ⇒ 先归还 a 的会话 ✓
    EXPECT_EQ(pool.outstanding(), 1u);
    EXPECT_EQ(pool.idle_count(), 1u);
    EXPECT_FALSE(b->valid());
    EXPECT_TRUE(a->valid());
}

TEST_F(SessionLeaseTest, PoisonedSessionIsDestroyedOnRelease) {
    SessionPool pool = make_pool();
    {
        auto lease = pool.acquire();
        ASSERT_TRUE(lease.has_value());
        ASSERT_TRUE((*lease)->begin_write());
        (*lease)->fail(TxnErrorClass::Unknown, -1, "data op failed");
    }
    EXPECT_EQ(pool.live(), 0u) << "毒化会话不得回池 ✗";
}

TEST_F(SessionLeaseTest, SessionStateIsAccessibleThroughLease) {
    SessionPool pool = make_pool();
    auto lease = pool.acquire();
    ASSERT_TRUE(lease.has_value());
    EXPECT_TRUE((*lease)->begin_read());
    EXPECT_TRUE((*lease)->active());
    EXPECT_EQ((*lease)->kind(), TxnKind::Read);
    (*lease)->rollback();
    EXPECT_FALSE((*lease)->active());
}
