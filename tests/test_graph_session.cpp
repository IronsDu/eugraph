// GraphSession 测试：直接持 WtSession（无 backend 抽象）⇒ 用真实 WT 连接。
// 纯事务状态机（TxnController/StickyError）另有不依赖 WT 的单测 ✓。
#include "storage/data/graph_session.hpp"
#include "storage/wt_connection_store.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {
using eugraph::GraphSession;
using eugraph::TxnErrorClass;
using eugraph::TxnKind;
using eugraph::TxnState;
using eugraph::WtConnectionStore;
using eugraph::WtSession;

class GraphSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = std::filesystem::current_path() / ("eugraph_session_" + std::string(info->name()));
        std::filesystem::remove_all(dir_);
        ASSERT_TRUE(conn_.open(dir_.string()));
    }
    void TearDown() override {
        conn_.close();
        std::filesystem::remove_all(dir_);
    }
    [[nodiscard]] std::unique_ptr<GraphSession> make_session() {
        return std::make_unique<GraphSession>(conn_.make_session());
    }

    /// 非毒化会话收尾后必须能复用（能重新 begin/commit ⇒ 没有泄漏事务）✓
    void expect_session_reusable(GraphSession& session) {
        session.reset_for_reuse();
        ASSERT_TRUE(session.begin_read()) << "上一个事务必须已回滚 ✓";
        EXPECT_TRUE(session.commit().committed());
    }

    std::filesystem::path dir_;
    WtConnectionStore conn_;
};
} // namespace

TEST_F(GraphSessionTest, ReadOnlyLifecycleCommitsExplicitly) {
    auto session = make_session();
    EXPECT_TRUE(session->begin_read());
    EXPECT_TRUE(session->active());
    EXPECT_EQ(session->kind(), TxnKind::Read);
    EXPECT_TRUE(session->commit().committed());
    session->close();
    EXPECT_TRUE(session->closed());
}

TEST_F(GraphSessionTest, DataOperationFailurePoisonsSession) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());
    session->fail(TxnErrorClass::Panic, -31804, "WT_PANIC");
    EXPECT_TRUE(session->commit().failed());
    EXPECT_TRUE(session->poisoned());
    EXPECT_FALSE(session->begin_read());
}

// ★ Busy 可重试 ⇒ 事务失败但**不毒化**；新事务可继续 ✓
TEST_F(GraphSessionTest, RetryableDataOperationFailureDoesNotPoison) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());
    session->fail(TxnErrorClass::Busy, EBUSY, "WT_BUSY");
    EXPECT_TRUE(session->commit().failed());
    EXPECT_FALSE(session->poisoned()) << "Busy 不得毒化会话 ✓";
    EXPECT_EQ(session->state(), TxnState::RolledBack);

    EXPECT_TRUE(session->begin_read()) << "可重试失败后允许新事务 ✓";
    EXPECT_TRUE(session->commit().committed());
}

TEST_F(GraphSessionTest, CloseWithoutCommitRollsBack) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());
    session->close();
    expect_session_reusable(*session);
}

TEST_F(GraphSessionTest, CloseIsIdempotent) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());
    session->close();
    session->close();
    expect_session_reusable(*session);
}

TEST_F(GraphSessionTest, DoubleBeginIsRejected) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_read());
    EXPECT_FALSE(session->begin_write()) << "同一时刻只允许一个活动事务 ✗";
}

TEST_F(GraphSessionTest, ResetForReuseClearsStickyErrorAndReopens) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());
    session->fail(TxnErrorClass::Busy, EBUSY, "busy");
    EXPECT_TRUE(session->transaction_failed());

    session->reset_for_reuse();
    EXPECT_FALSE(session->error().has()) << "复用前必须清粘性错误 ✓";
    EXPECT_TRUE(session->begin_write()) << "复用后必须能重新 begin/commit ✓";
    EXPECT_TRUE(session->commit().committed());
}

// ★ 数据 op 失败后**禁止提交**，并回滚收尾 ✓
TEST_F(GraphSessionTest, DataOperationFailureRefusesCommitAndRollsBack) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());

    session->fail_from_wt(WT_ROLLBACK, "table_put failed on vprop_1");
    EXPECT_TRUE(session->poisoned()) << "WT_ROLLBACK ⇒ 会话毒化 ✓";

    const auto out = session->commit();
    EXPECT_TRUE(out.failed());
    EXPECT_TRUE(out.message.find("commit refused") != std::string::npos);
    EXPECT_FALSE(session->active());
}

// ★ 毒化时后端事务可能仍开着 ⇒ close() 必须回滚 ✓
TEST_F(GraphSessionTest, PoisonedButOpenTransactionIsRolledBackOnClose) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());
    session->fail(TxnErrorClass::Unknown, -1, "data op failed");
    EXPECT_TRUE(session->poisoned());

    session->close();
    EXPECT_TRUE(session->closed());
    EXPECT_FALSE(session->active());
}

TEST_F(GraphSessionTest, RollbackIsIdempotentAndDoesNotClearPoison) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());
    session->rollback();
    session->rollback();
    expect_session_reusable(*session);

    auto poisoned = make_session();
    ASSERT_TRUE(poisoned->begin_write());
    poisoned->fail(TxnErrorClass::Panic, WT_PANIC, "panic");
    poisoned->rollback();
    EXPECT_TRUE(poisoned->poisoned()) << "rollback 不得洗白毒化 ✗";
}

TEST_F(GraphSessionTest, FailedBeginPreservesStickyErrorAndBlocksCommit) {
    auto session = make_session();
    ASSERT_TRUE(session->begin_write());
    session->fail(TxnErrorClass::Busy, EBUSY, "busy");
    EXPECT_TRUE(session->transaction_failed());

    // 重复 begin 必须失败，且不得清掉 Busy 的粘性错误
    EXPECT_FALSE(session->begin_read());
    EXPECT_TRUE(session->transaction_failed()) << "重复 begin 不得洗掉失败标记";

    const auto out = session->commit();
    EXPECT_TRUE(out.failed());
    EXPECT_TRUE(out.message.find("commit refused") != std::string::npos);
    EXPECT_FALSE(session->active());
}

TEST_F(GraphSessionTest, CommitWithoutActiveTransactionFails) {
    auto session = make_session();
    const auto out = session->commit();
    EXPECT_TRUE(out.failed());
    EXPECT_FALSE(session->active());
}

TEST_F(GraphSessionTest, BeginWithoutWtSessionFailsButDoesNotPoison) {
    GraphSession session{WtSession{}};
    EXPECT_FALSE(session.begin_read());
    EXPECT_FALSE(session.poisoned());
}
