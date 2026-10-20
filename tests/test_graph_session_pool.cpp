// 会话池（拥有 GraphSession ✓）+ 非拥有租约（析构即归还 ✓）的真实 WT 测试
#include "storage/data/session_pool.hpp"
#include "test_sync_store.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <memory>
#include <string>

namespace {
using eugraph::SessionLease;
using eugraph::SessionPool;
using eugraph::TxnKind;

class GraphSessionPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = std::filesystem::current_path() / ("eugraph_sesspool_" + std::string(info->name()));
        std::filesystem::remove_all(dir_);
        store_ = std::make_unique<eugraph::test::TestSyncGraphDataStore>();
        ASSERT_TRUE(store_->open(dir_.string()));
    }
    void TearDown() override {
        pool_.reset(); // ★ 池先于 store 销毁（session 在连接关闭前 close ✓）
        store_.reset();
        std::filesystem::remove_all(dir_);
    }
    std::shared_ptr<SessionPool> make_pool(std::size_t max_total = 4) {
        return std::make_shared<SessionPool>(max_total, [this]() { return store_->conn().make_session(); });
    }
    std::filesystem::path dir_;
    std::unique_ptr<eugraph::test::TestSyncGraphDataStore> store_;
    std::shared_ptr<SessionPool> pool_;
};
} // namespace

TEST_F(GraphSessionPoolTest, AcquireBeginCommitReleaseThenReuse) {
    pool_ = make_pool(4);
    {
        auto lease = pool_->acquire();
        ASSERT_TRUE(lease.has_value());
        ASSERT_TRUE((*lease)->begin_read());
        EXPECT_TRUE((*lease)->commit().committed());
    } // 析构 ⇒ 归还池 ✓
    EXPECT_EQ(pool_->idle_count(), 1u);

    auto again = pool_->acquire(); // 复用同一会话 ✓
    ASSERT_TRUE(again.has_value());
    ASSERT_TRUE((*again)->begin_read()) << "复用的会话必须仍可用 ✓";
    EXPECT_TRUE((*again)->commit().committed());
}

TEST_F(GraphSessionPoolTest, ExhaustionReturnsNullopt) {
    pool_ = make_pool(1);
    auto first = pool_->acquire();
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(pool_->acquire().has_value()) << "有界：耗尽 ⇒ nullopt ✓（不阻塞 ✗）";
    first->release();
    EXPECT_TRUE(pool_->acquire().has_value()) << "归还后可再取 ✓";
}

// ★ 区分性用例：**未提交就释放** ⇒ 池自动回滚（GraphSession::close ✓）⇒ 会话仍可复用 ✓
TEST_F(GraphSessionPoolTest, ReleaseWithoutCommitRollsBackAndSessionIsReusable) {
    pool_ = make_pool(4);
    {
        auto lease = pool_->acquire();
        ASSERT_TRUE(lease.has_value());
        ASSERT_TRUE((*lease)->begin_write()); // 开事务但**不提交** ✗
        // 析构 ⇒ put ⇒ close() ⇒ 回滚 ✓
    }
    auto again = pool_->acquire();
    ASSERT_TRUE(again.has_value());
    ASSERT_TRUE((*again)->begin_write()) << "未提交释放后会话必须可复用 ✓";
    EXPECT_TRUE((*again)->commit().committed());
}

TEST_F(GraphSessionPoolTest, ClearClosesIdleSessionsBeforeStoreDestruction) {
    pool_ = make_pool(2);
    {
        auto lease = pool_->acquire();
        ASSERT_TRUE(lease.has_value());
        ASSERT_TRUE((*lease)->begin_read());
        EXPECT_TRUE((*lease)->commit().committed());
    }
    ASSERT_EQ(pool_->idle_count(), 1u);
    pool_->clear();
    EXPECT_EQ(pool_->idle_count(), 0u);
    EXPECT_EQ(pool_->live(), 0u);
}

// ★ cursor 打开失败必须显式可见（valid()/error()），不得静默返回空结果 ✓
TEST_F(GraphSessionPoolTest, CursorOpenFailureIsVisibleOnHandle) {
    pool_ = make_pool(2);
    auto lease = pool_->acquire();
    ASSERT_TRUE(lease.has_value());

    auto point = (*lease)->acquire_cursor("table:does_not_exist");
    EXPECT_FALSE(point.valid());
    EXPECT_EQ(point.error(), ENOENT) << "打开失败必须带 WT 错误码 ✓";

    auto scan = (*lease)->acquire_scan_cursor("table:does_not_exist");
    EXPECT_FALSE(scan.valid());
    EXPECT_EQ(scan.error(), ENOENT);
}

TEST_F(GraphSessionPoolTest, LeaseDestructorReturnsSessionToPool) {
    pool_ = make_pool(2);
    {
        auto lease = pool_->acquire();
        ASSERT_TRUE(lease.has_value());
        ASSERT_TRUE((*lease)->begin_read());
        EXPECT_TRUE((*lease)->commit().committed());
        // 不显式 release()：析构即归还 ✓
    }
    EXPECT_EQ(pool_->idle_count(), 1u);
    EXPECT_EQ(pool_->live(), 1u);
}
