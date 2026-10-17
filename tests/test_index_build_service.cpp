// 每图构建服务：**完全确定性**测试 —— 注入"同步运行器"，任务在 `submit()` 内直接执行完
// ⇒ 断言无需等待、不用 sleep、不依赖超时（CI 的 -O0/coverage/串行环境同样稳定）。
#include "storage/index/index_build_scheduler.hpp"
#include "storage/index/index_build_service.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {
using eugraph::IndexBuildOutcome;
using eugraph::IndexBuildResult;
using eugraph::IndexBuildScheduler;
using eugraph::IndexBuildService;

/// 同步运行器：**就地执行**任务（无线程、无竞速、无等待）
IndexBuildScheduler::Runner synchronousRunner() {
    return [](IndexBuildScheduler::Job job) { job(); };
}

struct Published {
    uint64_t index_id = 0;
    IndexBuildOutcome outcome = IndexBuildOutcome::ERROR;
    std::string error;
};
} // namespace

TEST(IndexBuildService, PublishesPublicResultDeterministically) {
    std::vector<Published> got;
    IndexBuildService svc([](uint64_t) { return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}}; },
                          [&](uint64_t id, const std::string&, IndexBuildOutcome o, const std::string& e) {
                              got.push_back(Published{id, o, e});
                          },
                          {}, synchronousRunner());
    ASSERT_TRUE(svc.submit(1, "idx1"));
    // 同步运行器 ⇒ 提交返回时**已发布**（无需等待）
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].index_id, 1u);
    EXPECT_EQ(got[0].outcome, IndexBuildOutcome::PUBLIC);
    EXPECT_TRUE(got[0].error.empty());
}

TEST(IndexBuildService, PublishesErrorWithReason) {
    std::vector<Published> got;
    IndexBuildService svc(
        [](uint64_t) { return IndexBuildResult{IndexBuildOutcome::ERROR, "unique constraint violated: key=42"}; },
        [&](uint64_t id, const std::string&, IndexBuildOutcome o, const std::string& e) {
            got.push_back(Published{id, o, e});
        },
        {}, synchronousRunner());
    svc.submit(2, "idx2");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].outcome, IndexBuildOutcome::ERROR);
    EXPECT_NE(got[0].error.find("unique constraint"), std::string::npos);
}

TEST(IndexBuildService, ThrowingRunnerBecomesErrorNotCrash) {
    std::vector<Published> got;
    IndexBuildService svc([](uint64_t) -> IndexBuildResult { throw std::runtime_error("wt rollback loop"); },
                          [&](uint64_t id, const std::string&, IndexBuildOutcome o, const std::string& e) {
                              got.push_back(Published{id, o, e});
                          },
                          {}, synchronousRunner());
    svc.submit(3, "idx3");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].outcome, IndexBuildOutcome::ERROR);
    EXPECT_NE(got[0].error.find("wt rollback loop"), std::string::npos);
}

TEST(IndexBuildService, PublishesCancelledOutcome) {
    std::vector<Published> got;
    IndexBuildService svc([](uint64_t) { return IndexBuildResult{IndexBuildOutcome::CANCELLED, "cancelled"}; },
                          [&](uint64_t id, const std::string&, IndexBuildOutcome o, const std::string& e) {
                              got.push_back(Published{id, o, e});
                          },
                          {}, synchronousRunner());
    svc.submit(4, "idx4");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].outcome, IndexBuildOutcome::CANCELLED) << "取消必须发布（由调用方清理，不落 PUBLIC/ERROR）";
}

// 幂等：任务**运行中**再次提交同一索引必须被拒（在任务内部重入提交 ⇒ 确定性、无时序依赖）
TEST(IndexBuildService, SubmitIsIdempotentWhileJobIsRunning) {
    bool second_submit_result = true;
    IndexBuildService* svc_ptr = nullptr;
    std::vector<Published> got;
    IndexBuildService svc(
        [&](uint64_t id) {
            second_submit_result = svc_ptr->submit(id, "again"); // 运行中重入
            return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}};
        },
        [&](uint64_t id, const std::string&, IndexBuildOutcome o, const std::string& e) {
            got.push_back(Published{id, o, e});
        },
        {}, synchronousRunner());
    svc_ptr = &svc;
    ASSERT_TRUE(svc.submit(7, "idx7"));
    EXPECT_FALSE(second_submit_result) << "同一索引在运行中重复提交必须返回 false（幂等）";
    ASSERT_EQ(got.size(), 1u);
}

TEST(IndexBuildService, ShutdownRejectsNewSubmits) {
    std::vector<Published> got;
    IndexBuildService svc([](uint64_t) { return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}}; },
                          [&](uint64_t id, const std::string&, IndexBuildOutcome o, const std::string& e) {
                              got.push_back(Published{id, o, e});
                          },
                          {}, synchronousRunner());
    ASSERT_TRUE(svc.submit(1, "a"));
    svc.shutdown();
    EXPECT_FALSE(svc.submit(2, "b")) << "关图后不得再提交";
    EXPECT_EQ(svc.runningCount(), 0u);
}

TEST(IndexBuildService, CancelUnknownIndexReturnsFalse) {
    IndexBuildService svc([](uint64_t) { return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}}; }, nullptr, {},
                          synchronousRunner());
    EXPECT_FALSE(svc.cancel(999)) << "取消不存在的索引应返回 false";
}
