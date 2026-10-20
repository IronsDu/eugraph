// 事务状态机（纯逻辑）单测
#include "storage/data/txn_controller.hpp"

#include <gtest/gtest.h>

#include <cerrno>

namespace {
using eugraph::TxnController;
using eugraph::TxnErrorClass;
using eugraph::TxnKind;
using eugraph::TxnOutcome;
using eugraph::TxnState;
} // namespace

TEST(TxnController, BeginFromIdleThenRejectNestedBegin) {
    TxnController c;
    EXPECT_EQ(c.state(), TxnState::None);
    EXPECT_TRUE(c.begin(TxnKind::Read));
    EXPECT_TRUE(c.active());
    EXPECT_TRUE(c.transaction_open());
    EXPECT_EQ(c.kind(), TxnKind::Read);

    EXPECT_FALSE(c.begin(TxnKind::Write)) << "同一时刻只能有一个活动事务 ✗";
    EXPECT_EQ(c.kind(), TxnKind::Read) << "被拒绝的 begin 不得改变类型";
}

TEST(TxnController, CommitRequiresActiveTransaction) {
    TxnController c;
    EXPECT_FALSE(c.commit_request().has_value()) << "没有活动事务时不得提交 ✗";

    ASSERT_TRUE(c.begin(TxnKind::Write));
    auto kind = c.commit_request();
    ASSERT_TRUE(kind.has_value());
    EXPECT_EQ(*kind, TxnKind::Write);
}

TEST(TxnController, SuccessfulCommitResetsKindAndAllowsNextTxn) {
    TxnController c;
    ASSERT_TRUE(c.begin(TxnKind::Write));
    c.on_commit_result(TxnOutcome{TxnState::Committed, 0, {}});
    EXPECT_EQ(c.state(), TxnState::Committed);
    EXPECT_EQ(c.kind(), TxnKind::None);
    EXPECT_FALSE(c.active());
    EXPECT_FALSE(c.transaction_open());

    EXPECT_TRUE(c.begin(TxnKind::Read)) << "已提交后允许开新事务 ✓";
    EXPECT_EQ(c.kind(), TxnKind::Read);
}

TEST(TxnController, FailedCommitPoisonsSessionForever) {
    TxnController c;
    ASSERT_TRUE(c.begin(TxnKind::Write));
    c.on_commit_result(TxnOutcome{TxnState::Failed, -31804, "WT_PANIC"});
    EXPECT_TRUE(c.poisoned());
    EXPECT_FALSE(c.begin(TxnKind::Read)) << "毒化后不得再开事务 ✗";
    EXPECT_FALSE(c.begin(TxnKind::Write));

    c.on_rollback();
    EXPECT_TRUE(c.poisoned()) << "Failed 不可被 rollback 洗白 ✗";
    EXPECT_FALSE(c.begin(TxnKind::Read));
}

// ★ 区分性用例：Busy 可重试 ⇒ 事务结束但**不毒化** ✓
TEST(TxnController, RetryableCommitFailureEndsTxnWithoutPoison) {
    TxnController c;
    ASSERT_TRUE(c.begin(TxnKind::Write));
    c.on_commit_result(TxnOutcome{TxnState::Failed, EBUSY, "WT_BUSY", TxnErrorClass::Busy});
    EXPECT_EQ(c.state(), TxnState::RolledBack);
    EXPECT_FALSE(c.poisoned());
    EXPECT_FALSE(c.transaction_open());
    EXPECT_TRUE(c.begin(TxnKind::Write)) << "Busy 后会话仍可用 ✓";
}

TEST(TxnController, RollbackFromActiveEndsTransaction) {
    TxnController c;
    ASSERT_TRUE(c.begin(TxnKind::Write));
    c.on_rollback();
    EXPECT_EQ(c.state(), TxnState::RolledBack);
    EXPECT_FALSE(c.active());
    EXPECT_FALSE(c.transaction_open());
    EXPECT_FALSE(c.poisoned());
    EXPECT_TRUE(c.begin(TxnKind::Read)) << "回滚后可复用该会话 ✓";
}

TEST(TxnController, NonCommittedOutcomeAlsoPoisons) {
    TxnController c;
    ASSERT_TRUE(c.begin(TxnKind::Read));
    c.on_commit_result(TxnOutcome{TxnState::RolledBack, 0, "unexpected"});
    EXPECT_TRUE(c.poisoned()) << "非 Committed 的提交结果同样不可信 ✗";
}

// ★ 毒化不等于事务已结束：后端仍需 rollback 收尾 ✓
TEST(TxnController, PoisonKeepsTransactionOpenUntilRollback) {
    TxnController c;
    ASSERT_TRUE(c.begin(TxnKind::Write));
    c.poison();
    EXPECT_TRUE(c.poisoned());
    EXPECT_TRUE(c.transaction_open()) << "毒化后必须仍能收尾 ✓";
    EXPECT_FALSE(c.begin(TxnKind::Read));

    c.on_rollback();
    EXPECT_FALSE(c.transaction_open());
    EXPECT_TRUE(c.poisoned()) << "收尾不得洗白毒化 ✗";
}

TEST(TxnController, ResetClearsHealthyStateButNotPoison) {
    TxnController healthy;
    ASSERT_TRUE(healthy.begin(TxnKind::Read));
    healthy.on_rollback();
    healthy.reset();
    EXPECT_EQ(healthy.state(), TxnState::None);
    EXPECT_FALSE(healthy.transaction_open());

    TxnController poisoned;
    ASSERT_TRUE(poisoned.begin(TxnKind::Read));
    poisoned.poison();
    poisoned.on_rollback();
    poisoned.reset();
    EXPECT_TRUE(poisoned.poisoned()) << "毒化会话不得复位复用 ✗";
}
