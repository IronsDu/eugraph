// 索引构建调度器：并发上限 / FIFO / 幂等 / 取消 / 异常不阻塞 / drain
// 用**注入的假运行器**（只记录不执行）⇒ 断言完全确定性，无 sleep、无竞态。
#include "storage/index/index_build_scheduler.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using eugraph::IndexBuildScheduler;

/// 假运行器：记录被启动的任务（不执行），由测试决定何时"完成"它们。
class FakeRunner {
public:
    IndexBuildScheduler::Runner asRunner() {
        return [this](IndexBuildScheduler::Job job) { started_.push_back(std::move(job)); };
    }
    size_t startedCount() const {
        return started_.size();
    }
    /// 执行第 i 个已启动任务（即"完成"它）
    void complete(size_t i) {
        started_.at(i)();
    }

private:
    std::vector<IndexBuildScheduler::Job> started_;
};

} // namespace

TEST(IndexBuildScheduler, RespectsConcurrencyLimit) {
    FakeRunner runner;
    IndexBuildScheduler sched(IndexBuildScheduler::Options{/*max_concurrent=*/2}, runner.asRunner());
    EXPECT_EQ(sched.maxConcurrent(), 2u);
    int completed = 0;
    for (uint64_t id = 1; id <= 3; ++id)
        EXPECT_TRUE(sched.submit(id, "idx" + std::to_string(id), [&completed] { ++completed; }));
    // 只应启动 2 个；第 3 个仍在排队
    EXPECT_EQ(runner.startedCount(), 2u);
    EXPECT_EQ(sched.runningCount(), 2u);
    EXPECT_EQ(sched.queuedCount(), 1u);
    runner.complete(0);                  // 完成 #1 ⇒ 释放的名额立刻给队首 #3
    EXPECT_EQ(sched.runningCount(), 2u); // 此时在跑 #2 与 #3
    EXPECT_EQ(sched.queuedCount(), 0u);
    EXPECT_EQ(runner.startedCount(), 3u);
    runner.complete(1);
    runner.complete(2);
    EXPECT_EQ(completed, 3);
    EXPECT_EQ(sched.runningCount(), 0u);
}

TEST(IndexBuildScheduler, StartsInFifoOrder) {
    FakeRunner runner;
    IndexBuildScheduler sched(IndexBuildScheduler::Options{1}, runner.asRunner());
    std::vector<uint64_t> order;
    for (uint64_t id = 10; id <= 12; ++id)
        sched.submit(id, "idx", [&order, id] { order.push_back(id); });
    EXPECT_EQ(runner.startedCount(), 1u); // 并发度为 1 ⇒ 只启动队首
    runner.complete(0);
    runner.complete(1);
    runner.complete(2);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 10u);
    EXPECT_EQ(order[1], 11u);
    EXPECT_EQ(order[2], 12u);
}

TEST(IndexBuildScheduler, SubmitIsIdempotentPerIndex) {
    FakeRunner runner;
    IndexBuildScheduler sched({2}, runner.asRunner());
    EXPECT_TRUE(sched.submit(7, "idx7", [] {}));
    EXPECT_FALSE(sched.submit(7, "idx7", [] {})); // 已在运行 ⇒ 幂等
    EXPECT_EQ(sched.runningCount(), 1u);
    auto snap = sched.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].index_id, 7u);
    EXPECT_TRUE(snap[0].running);
    runner.complete(0); // 必须让已启动任务结束：析构会 drain（等 queue/running 皆空）
    EXPECT_EQ(sched.runningCount(), 0u);
}

TEST(IndexBuildScheduler, CancelQueuedJobNeverStarts) {
    FakeRunner runner;
    IndexBuildScheduler sched(IndexBuildScheduler::Options{1}, runner.asRunner());
    int second_ran = 0;
    sched.submit(1, "a", [] {});
    sched.submit(2, "b", [&second_ran] { ++second_ran; });
    EXPECT_EQ(sched.queuedCount(), 1u);
    EXPECT_TRUE(sched.cancel(2));
    EXPECT_EQ(sched.queuedCount(), 0u);
    runner.complete(0);                   // 完成第一个
    EXPECT_EQ(runner.startedCount(), 1u); // 第二个从未启动
    EXPECT_EQ(second_ran, 0);
}

TEST(IndexBuildScheduler, CancelRunningJobIsObservableByJob) {
    FakeRunner runner;
    IndexBuildScheduler sched(IndexBuildScheduler::Options{1}, runner.asRunner());
    auto* sched_ptr = &sched;
    bool observed_cancel = false;
    sched.submit(5, "idx5", [sched_ptr, &observed_cancel] { observed_cancel = sched_ptr->isCancelled(5); });
    EXPECT_TRUE(sched.cancel(5)); // 运行中 ⇒ 置位
    EXPECT_TRUE(sched.isCancelled(5));
    runner.complete(0);
    EXPECT_TRUE(observed_cancel) << "任务应在阶段边界看到取消位";
}

TEST(IndexBuildScheduler, ThrowingJobReleasesSlot) {
    FakeRunner runner;
    IndexBuildScheduler sched(IndexBuildScheduler::Options{1}, runner.asRunner());
    int second_ran = 0;
    sched.submit(1, "boom", [] { throw std::runtime_error("job failed"); });
    sched.submit(2, "next", [&second_ran] { ++second_ran; });
    EXPECT_EQ(sched.queuedCount(), 1u);
    runner.complete(0); // 抛异常的任务也必须释放名额
    EXPECT_EQ(sched.queuedCount(), 0u);
    EXPECT_EQ(sched.runningCount(), 1u);
    runner.complete(1);
    EXPECT_EQ(second_ran, 1);
}

TEST(IndexBuildScheduler, RejectsZeroConcurrency) {
    EXPECT_THROW((IndexBuildScheduler(IndexBuildScheduler::Options{0})), std::invalid_argument);
}
