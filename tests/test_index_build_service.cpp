// 每图构建服务：发布 PUBLIC/ERROR/CANCELLED、异常兜底、幂等、取消排队任务、关图编排
#include "storage/index/index_build_scheduler.hpp"
#include "storage/index/index_build_service.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {
using eugraph::IndexBuildOutcome;
using eugraph::IndexBuildResult;
using eugraph::IndexBuildScheduler;
using eugraph::IndexBuildService;

struct Published {
    uint64_t index_id = 0;
    IndexBuildOutcome outcome = IndexBuildOutcome::ERROR;
    std::string error;
};

/// 发布回调 + 可等待的"已发布"信号（避免 sleep）
struct Recorder {
    std::vector<Published> all;
    std::promise<Published> first;
    std::shared_ptr<std::promise<Published>> first_ptr = std::make_shared<std::promise<Published>>();
    std::mutex mu;

    IndexBuildService::Publisher publisher() {
        return [this](uint64_t id, const std::string& /*name*/, IndexBuildOutcome outcome, const std::string& error) {
            {
                std::lock_guard<std::mutex> lock(mu);
                all.push_back(Published{id, outcome, error});
            }
            try {
                first_ptr->set_value(Published{id, outcome, error});
            } catch (...) {
                // 只关心第一次
            }
        };
    }
    Published waitFirst(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        auto fut = first_ptr->get_future();
        EXPECT_EQ(fut.wait_for(timeout), std::future_status::ready) << "发布回调未被调用（超时）";
        return fut.get();
    }
};
} // namespace

TEST(IndexBuildService, PublishesPublicResult) {
    Recorder rec;
    IndexBuildService svc([](uint64_t) { return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}}; }, rec.publisher());
    EXPECT_TRUE(svc.submit(1, "idx1"));
    auto p = rec.waitFirst();
    EXPECT_EQ(p.index_id, 1u);
    EXPECT_EQ(p.outcome, IndexBuildOutcome::PUBLIC);
    EXPECT_TRUE(p.error.empty());
}

TEST(IndexBuildService, PublishesErrorWithReason) {
    Recorder rec;
    IndexBuildService svc(
        [](uint64_t) { return IndexBuildResult{IndexBuildOutcome::ERROR, "unique constraint violated: key=42"}; },
        rec.publisher());
    svc.submit(2, "idx2");
    auto p = rec.waitFirst();
    EXPECT_EQ(p.outcome, IndexBuildOutcome::ERROR);
    EXPECT_NE(p.error.find("unique constraint"), std::string::npos);
}

TEST(IndexBuildService, ThrowingRunnerBecomesErrorNotCrash) {
    Recorder rec;
    IndexBuildService svc([](uint64_t) -> IndexBuildResult { throw std::runtime_error("wt rollback loop"); },
                          rec.publisher());
    svc.submit(3, "idx3");
    auto p = rec.waitFirst();
    EXPECT_EQ(p.outcome, IndexBuildOutcome::ERROR);
    EXPECT_NE(p.error.find("wt rollback loop"), std::string::npos);
}

TEST(IndexBuildService, SubmitIsIdempotentPerIndex) {
    std::promise<void> release;
    auto fut = release.get_future();
    Recorder rec;
    IndexBuildService svc(
        [&fut](uint64_t) {
            fut.wait(); // 卡住，保证第一条仍在运行
            return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}};
        },
        rec.publisher());
    EXPECT_TRUE(svc.submit(7, "idx7"));
    EXPECT_FALSE(svc.submit(7, "idx7")); // 幂等
    release.set_value();
    EXPECT_EQ(rec.waitFirst().outcome, IndexBuildOutcome::PUBLIC);
}

TEST(IndexBuildService, CancelQueuedJobPreventsItFromRunning) {
    std::promise<void> release_first;
    auto first_fut = release_first.get_future();
    std::atomic<int> second_runs{0};
    Recorder rec;
    IndexBuildService svc( // 并发度 1 ⇒ 第二条排队
        [&](uint64_t id) {
            if (id == 1) {
                first_fut.wait();
                return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}};
            }
            ++second_runs;
            return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}};
        },
        rec.publisher(), IndexBuildScheduler::Options{1});
    EXPECT_TRUE(svc.submit(1, "a"));
    EXPECT_TRUE(svc.submit(2, "b"));
    EXPECT_EQ(svc.queuedCount(), 1u);
    EXPECT_TRUE(svc.cancel(2)); // 排队中 ⇒ 直接丢弃
    release_first.set_value();
    auto p = rec.waitFirst();
    EXPECT_EQ(p.index_id, 1u);
    EXPECT_EQ(second_runs.load(), 0) << "被取消的排队任务绝不应运行";
}

TEST(IndexBuildService, ShutdownRejectsNewSubmitsAndDrains) {
    Recorder rec;
    IndexBuildService svc([](uint64_t) { return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}}; }, rec.publisher());
    svc.submit(1, "a");
    EXPECT_EQ(rec.waitFirst().outcome, IndexBuildOutcome::PUBLIC);
    svc.shutdown();
    EXPECT_FALSE(svc.submit(2, "b")) << "关图后不得再提交";
    EXPECT_EQ(svc.runningCount(), 0u);
}
