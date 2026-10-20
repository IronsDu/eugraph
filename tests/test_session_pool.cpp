// 会话池（拥有 GraphSession ✓）+ 非拥有租约（析构即归还 ✓）+ 事务错误分类的测试。
// 会话本身用真实 WT（GraphSession 直接持 WtSession，不再有假 backend 抽象）；
// 纯状态机/错误分类部分仍不依赖连接 ✓。
#include "storage/data/session_pool.hpp"
#include "storage/data/session_types.hpp"
#include "storage/wt_connection_store.hpp"
#include "storage/wt_error.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {
using eugraph::classifyWtError;
using eugraph::GraphSession;
using eugraph::is_retryable;
using eugraph::poisons_session;
using eugraph::SessionLease;
using eugraph::SessionPool;
using eugraph::StickyError;
using eugraph::TxnErrorClass;
using eugraph::TxnKind;
using eugraph::TxnOutcome;
using eugraph::TxnState;
using eugraph::WtConnectionStore;
using eugraph::WtSession;

class SessionPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = std::filesystem::current_path() / ("eugraph_pool_" + std::string(info->name()));
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

TEST_F(SessionPoolTest, AcquireCreatesUpToLimitThenReportsExhaustion) {
    SessionPool pool = make_pool(2);
    auto a = pool.acquire();
    auto b = pool.acquire();
    EXPECT_TRUE(a.has_value());
    EXPECT_TRUE(b.has_value());
    EXPECT_EQ(pool.live(), 2u);
    EXPECT_EQ(pool.outstanding(), 2u);

    // 达到上限 ⇒ 返回 nullopt（调用方转明确错误 ✓，不阻塞 ✗）
    EXPECT_FALSE(pool.acquire().has_value());
}

TEST_F(SessionPoolTest, PutThenAcquireReusesSameSession) {
    SessionPool pool = make_pool(4);
    GraphSession* first = nullptr;
    {
        auto lease = pool.acquire();
        ASSERT_TRUE(lease.has_value());
        first = &**lease;
    } // 析构 ⇒ 归还池 ✓
    EXPECT_EQ(pool.idle_count(), 1u);
    EXPECT_EQ(pool.outstanding(), 0u);

    auto again = pool.acquire(); // 同节点 ⇒ 复用，不新建 ✓
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(&**again, first);
}

TEST_F(SessionPoolTest, PoisonedSessionIsDestroyedNotReturned) {
    SessionPool pool = make_pool(4);
    {
        auto lease = pool.acquire();
        ASSERT_TRUE(lease.has_value());
        ASSERT_TRUE((*lease)->begin_write());
        // 模拟不可恢复的数据 op 失败 ⇒ 毒化 ✓
        (*lease)->fail(TxnErrorClass::Unknown, -1, "data op failed");
        EXPECT_TRUE((*lease)->poisoned());
    } // 毒化 ⇒ 销毁 ✗ 不回池 ✓
    EXPECT_EQ(pool.live(), 0u);
    EXPECT_EQ(pool.idle_count(), 0u);
}

TEST_F(SessionPoolTest, FactoryFailureIsReportedAndDoesNotConsumeCapacity) {
    SessionPool pool(4, []() { return WtSession{}; });
    EXPECT_FALSE(pool.acquire().has_value());
    EXPECT_EQ(pool.live(), 0u);
    EXPECT_EQ(pool.outstanding(), 0u);
}

TEST_F(SessionPoolTest, ClearClosesIdleSessions) {
    SessionPool pool = make_pool(4);
    {
        auto lease = pool.acquire();
        ASSERT_TRUE(lease.has_value());
        ASSERT_TRUE((*lease)->begin_read());
        EXPECT_TRUE((*lease)->commit().committed());
    }
    ASSERT_EQ(pool.idle_count(), 1u);
    pool.clear();
    EXPECT_EQ(pool.idle_count(), 0u);
    EXPECT_EQ(pool.live(), 0u);
    EXPECT_TRUE(pool.closed());
}

TEST_F(SessionPoolTest, AcquireAfterClearFails) {
    SessionPool pool = make_pool(4);
    pool.clear();
    EXPECT_FALSE(pool.acquire().has_value()) << "关闭后的池不得再借出会话 ✗";
}

TEST_F(SessionPoolTest, DuplicatePutIsIgnored) {
    SessionPool pool = make_pool(4);
    GraphSession* raw = nullptr;
    {
        auto lease = pool.acquire();
        ASSERT_TRUE(lease.has_value());
        raw = &**lease;
        ASSERT_TRUE(raw->begin_read());
        EXPECT_TRUE(raw->commit().committed());
    } // 正常归还
    ASSERT_EQ(pool.idle_count(), 1u);
    // 对已归还的会话再次 put ⇒ 记日志后忽略，不得污染空闲表 ✓
    pool.put(*raw);
    EXPECT_EQ(pool.idle_count(), 1u);
    EXPECT_EQ(pool.live(), 1u);
}

// ★ 区分性用例：clear() 只关闭**空闲**会话；借出的会话仍然可用 ✓
TEST_F(SessionPoolTest, ClearDoesNotCloseLeasedSessions) {
    SessionPool pool = make_pool(4);
    auto lease = pool.acquire();
    ASSERT_TRUE(lease.has_value());
    ASSERT_TRUE((*lease)->begin_write());

    pool.clear();
    EXPECT_EQ(pool.outstanding(), 1u);
    EXPECT_FALSE(pool.acquire().has_value());

    // 会话仍活着：还能提交（若被 clear() 关闭，这里会失败/UB）✓
    EXPECT_TRUE((*lease)->commit().committed());
    lease->release();
    EXPECT_EQ(pool.outstanding(), 0u);
    EXPECT_EQ(pool.live(), 0u);
}

// ★ 区分性用例：池对象先销毁、租约后释放 ⇒ 不得悬空 ✓
TEST_F(SessionPoolTest, LeaseOutlivesPoolObject) {
    std::optional<SessionLease> lease;
    {
        SessionPool pool = make_pool(2);
        auto acquired = pool.acquire();
        ASSERT_TRUE(acquired.has_value());
        ASSERT_TRUE((*acquired)->begin_write()); // 未提交
        lease = std::move(acquired);
    } // 池析构：不得碰借出的会话 ✓
    EXPECT_TRUE(lease->valid());
    // 共享核心仍存活 ⇒ WT session 仍可用 ✓
    EXPECT_TRUE((*lease)->commit().committed());
    lease->release();
}

TEST(SessionTypes, StickyErrorKeepsFirstFatalError) {
    StickyError err;
    EXPECT_FALSE(err.has());

    TxnOutcome soft{TxnState::RolledBack, 0, "soft"};
    err.set(soft);
    EXPECT_FALSE(err.has()) << "非失败结局不应粘住";

    TxnOutcome fatal1{TxnState::Failed, -31804, "first", TxnErrorClass::Panic};
    err.set(fatal1);
    ASSERT_TRUE(err.has());
    EXPECT_TRUE(err.fatal());
    EXPECT_EQ(err.error_class(), TxnErrorClass::Panic);
    EXPECT_EQ(err.outcome().message, "first");

    TxnOutcome fatal2{TxnState::Failed, -31802, "second", TxnErrorClass::Rollback};
    err.set(fatal2);
    EXPECT_EQ(err.outcome().message, "first") << "首错不可被覆盖 ✓";

    err.clear();
    EXPECT_FALSE(err.has());
}

TEST(SessionTypes, BusyFailureIsStickyButNotFatal) {
    StickyError err;
    err.set(TxnOutcome{TxnState::Failed, EBUSY, "busy", TxnErrorClass::Busy});
    ASSERT_TRUE(err.has());
    EXPECT_FALSE(err.fatal()) << "Busy 可重试 ⇒ 不得毒化会话 ✓";
    EXPECT_EQ(err.error_class(), TxnErrorClass::Busy);
}

TEST(SessionTypes, UnclassifiedFailureIsTreatedAsUnknownAndPoisons) {
    TxnOutcome out{TxnState::Failed, -1, "no class"};
    EXPECT_EQ(out.effective_error_class(), TxnErrorClass::Unknown);
    EXPECT_FALSE(out.retryable());
    EXPECT_TRUE(out.poisons_session());
}

TEST(SessionTypes, TxnOutcomeSemantics) {
    EXPECT_TRUE((TxnOutcome{TxnState::Committed, 0, {}}).committed());
    EXPECT_FALSE((TxnOutcome{TxnState::Committed, 0, {}}).failed());
    EXPECT_TRUE((TxnOutcome{TxnState::Failed, 1, "x"}).failed());
    EXPECT_EQ(static_cast<int>(TxnKind::Write), 2);
}

TEST(WtErrorClassification, MapsWtCodesToTxnErrorClass) {
    EXPECT_EQ(classifyWtError(0), TxnErrorClass::None);
    EXPECT_EQ(classifyWtError(EBUSY), TxnErrorClass::Busy);
    EXPECT_EQ(classifyWtError(WT_ROLLBACK), TxnErrorClass::Rollback);
    EXPECT_EQ(classifyWtError(WT_CACHE_OVERFLOW), TxnErrorClass::CacheOverflow);
    EXPECT_EQ(classifyWtError(WT_PANIC), TxnErrorClass::Panic);
    EXPECT_EQ(classifyWtError(-999999), TxnErrorClass::Unknown);

    EXPECT_TRUE(is_retryable(TxnErrorClass::Busy));
    EXPECT_FALSE(poisons_session(TxnErrorClass::Busy));
    EXPECT_TRUE(poisons_session(TxnErrorClass::Rollback));
    EXPECT_TRUE(poisons_session(TxnErrorClass::CacheOverflow));
    EXPECT_TRUE(poisons_session(TxnErrorClass::Panic));
    EXPECT_TRUE(poisons_session(TxnErrorClass::Unknown));
    EXPECT_FALSE(poisons_session(TxnErrorClass::None));
}
