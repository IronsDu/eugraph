/// IndexBuildService 的单测：队列/幂等/取消/关图排空（不依赖 WT）。

#include <gtest/gtest.h>

#include "storage/index/index_build_service.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace eugraph;

namespace {

/// 简单门闩：测试用它控制 Job 的推进，不靠 sleep 断言。
class Gate {
public:
    void open() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            open_ = true;
        }
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this] { return open_; });
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    bool open_ = false;
};

} // namespace

TEST(IndexBuildServiceTest, SubmitRunsJobOnWorker) {
    IndexBuildService service;
    std::promise<void> done;
    auto future = done.get_future();
    ASSERT_TRUE(service.submit("idx1", [&done] { done.set_value(); }));
    EXPECT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(service.waitUntilIdle(5000));
    service.shutdown();
}

TEST(IndexBuildServiceTest, DuplicateIndexSubmitIsIdempotent) {
    IndexBuildService service;
    Gate gate;
    std::promise<void> started;
    auto started_future = started.get_future();
    ASSERT_TRUE(service.submit("idx7", [&] {
        started.set_value();
        gate.wait();
    }));
    started_future.wait();
    // 运行中重复提交 ⇒ false，且不会执行第二个 Job。
    EXPECT_FALSE(service.submit("idx7", [] {}));
    gate.open();
    EXPECT_TRUE(service.waitUntilIdle(5000));
    service.shutdown();
}

TEST(IndexBuildServiceTest, CancelDropsQueuedJob) {
    IndexBuildService service; // 1 线程
    Gate gate;
    std::promise<void> first_started;
    auto first_started_future = first_started.get_future();
    std::atomic<bool> second_ran{false};

    ASSERT_TRUE(service.submit("first", [&] {
        first_started.set_value();
        gate.wait();
    }));
    first_started_future.wait();
    ASSERT_TRUE(service.submit("second", [&] { second_ran.store(true); }));
    service.cancel("second");
    gate.open();
    EXPECT_TRUE(service.waitUntilIdle(5000));
    EXPECT_FALSE(second_ran.load());
    EXPECT_FALSE(service.isCancelled("second")); // 任务结束后标记清除
    service.shutdown();
}

TEST(IndexBuildServiceTest, RunningJobSeesCancellation) {
    IndexBuildService service;
    Gate gate;
    std::promise<void> started;
    auto started_future = started.get_future();
    std::atomic<bool> observed{false};

    ASSERT_TRUE(service.submit("idx9", [&] {
        started.set_value();
        gate.wait();
        observed.store(service.isCancelled("idx9"));
    }));
    started_future.wait();
    service.cancel("idx9");
    gate.open();
    EXPECT_TRUE(service.waitUntilIdle(5000));
    EXPECT_TRUE(observed.load());
    EXPECT_FALSE(service.isCancelled("idx9"));
    service.shutdown();
}

TEST(IndexBuildServiceTest, ShutdownDropsQueuedJobsAndJoinsRunning) {
    IndexBuildService service;
    Gate gate;
    std::promise<void> started;
    auto started_future = started.get_future();
    std::atomic<bool> queued_ran{false};

    ASSERT_TRUE(service.submit("running", [&] {
        started.set_value();
        gate.wait();
    }));
    started_future.wait();
    ASSERT_TRUE(service.submit("queued", [&] { queued_ran.store(true); }));

    auto shutdown_thread = std::thread([&] { service.shutdown(); });
    // 确定性等待 shutdown 已置 stop_（队列被清空且新 submit 被拒），再放行运行中的 Job；
    // 否则运行中的 Job 可能先结束、把排队 Job 取走（测试竞态，不是服务语义）。
    while (true) {
        if (service.queued() == 0 && !service.submit("probe", [] {}))
            break;
        std::this_thread::yield();
    }
    gate.open();
    shutdown_thread.join();

    EXPECT_FALSE(queued_ran.load());
    EXPECT_FALSE(service.submit("after", [] {})); // 已停止
    EXPECT_EQ(service.running(), 0u);
    EXPECT_EQ(service.queued(), 0u);
}

TEST(IndexBuildServiceTest, WaitUntilIdleReportsBusy) {
    IndexBuildService service;
    Gate gate;
    std::promise<void> started;
    auto started_future = started.get_future();
    ASSERT_TRUE(service.submit("idx1", [&] {
        started.set_value();
        gate.wait();
    }));
    started_future.wait();
    EXPECT_FALSE(service.waitUntilIdle(0)); // 门闩未开 ⇒ 忙
    gate.open();
    EXPECT_TRUE(service.waitUntilIdle(5000));
    service.shutdown();
}

TEST(IndexBuildServiceTest, CancelWithoutJobDoesNotPolluteNextSubmit) {
    IndexBuildService service;
    // 构建已结束（无对应任务）时 cancel 不应留下标记，否则同名重建会被误丢弃。
    service.cancel("idx");
    std::promise<void> done;
    auto future = done.get_future();
    ASSERT_TRUE(service.submit("idx", [&done] { done.set_value(); }));
    EXPECT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(service.waitUntilIdle(5000));
    service.shutdown();
}

TEST(IndexBuildServiceTest, ShutdownCancelsRunningJob) {
    IndexBuildService service;
    std::promise<void> started;
    auto started_future = started.get_future();
    std::atomic<bool> observed{false};

    ASSERT_TRUE(service.submit("idx", [&] {
        started.set_value();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (service.isCancelled("idx")) {
                observed.store(true);
                return;
            }
            std::this_thread::yield();
        }
    }));
    started_future.wait();

    service.shutdown(); // 必须给在飞 Job 打取消标记，否则这里会等到 Job 超时
    EXPECT_TRUE(observed.load());
}
