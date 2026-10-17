// 索引构建相位机：相位顺序 / CATCHUP 多轮 / 失败绝不 PUBLIC / 阶段边界取消
#include "storage/index/index_build_task.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {
using eugraph::IndexBuildOutcome;
using eugraph::IndexBuildTask;

/// 记录相位与调用顺序的假实现
struct FakeBackfill {
    std::vector<std::string> log;
    int scan_result = 1;      // 1=成功
    int rounds_remaining = 0; // catch_up_round 返回 true 的次数
    int gate_result = 1;
    int validate_result = 1;
    std::string validate_error = "duplicate key";
    bool cancel_after_rounds = false;
    int rounds_done = 0;

    IndexBuildTask::Callbacks callbacks() {
        IndexBuildTask::Callbacks cb;
        cb.on_phase = [this](const char* p) { log.emplace_back(p); };
        cb.scan_base = [this] {
            log.emplace_back("scan:run");
            return scan_result != 0;
        };
        cb.catch_up_round = [this] {
            log.emplace_back("catchup:run");
            ++rounds_done;
            if (rounds_remaining > 0) {
                --rounds_remaining;
                return true; // 仍有剩余工作
            }
            return false;
        };
        cb.finalize_gate = [this] {
            log.emplace_back("gate:run");
            return gate_result != 0;
        };
        cb.validate = [this](std::string& why) {
            log.emplace_back("validate:run");
            why = validate_error;
            return validate_result != 0;
        };
        cb.cancelled = [this] { return cancel_after_rounds && rounds_done >= 1; };
        return cb;
    }
};
} // namespace

TEST(IndexBuildTask, HappyPathRunsAllPhasesInOrder) {
    FakeBackfill f;
    auto res = IndexBuildTask::run(f.callbacks());
    EXPECT_EQ(res.outcome, IndexBuildOutcome::PUBLIC);
    EXPECT_TRUE(res.error.empty());
    ASSERT_GE(f.log.size(), 5u);
    EXPECT_EQ(f.log[0], "SCAN");
    EXPECT_EQ(f.log[1], "scan:run");
    EXPECT_EQ(f.log[2], "CATCHUP");
    EXPECT_NE(std::find(f.log.begin(), f.log.end(), "FINALIZE"), f.log.end());
    EXPECT_EQ(f.log.back(), "validate:run"); // 校验是最后一步
}

TEST(IndexBuildTask, CatchUpLoopsUntilNoRemainingWork) {
    FakeBackfill f;
    f.rounds_remaining = 3; // 前三轮都报"仍有剩余工作"
    auto res = IndexBuildTask::run(f.callbacks());
    EXPECT_EQ(res.outcome, IndexBuildOutcome::PUBLIC);
    EXPECT_EQ(f.rounds_done, 4); // 3 轮 + 1 轮收尾
}

TEST(IndexBuildTask, ScanFailureNeverReachesValidateOrPublic) {
    FakeBackfill f;
    f.scan_result = 0;
    auto res = IndexBuildTask::run(f.callbacks());
    EXPECT_EQ(res.outcome, IndexBuildOutcome::ERROR);
    EXPECT_NE(res.error.find("scan"), std::string::npos);
    EXPECT_EQ(std::find(f.log.begin(), f.log.end(), "validate:run"), f.log.end());
    EXPECT_EQ(std::find(f.log.begin(), f.log.end(), "FINALIZE"), f.log.end());
}

TEST(IndexBuildTask, GateFailureIsErrorNotPublic) {
    FakeBackfill f;
    f.gate_result = 0;
    auto res = IndexBuildTask::run(f.callbacks());
    EXPECT_EQ(res.outcome, IndexBuildOutcome::ERROR);
    EXPECT_EQ(std::find(f.log.begin(), f.log.end(), "validate:run"), f.log.end());
}

TEST(IndexBuildTask, ValidationFailureIsErrorAndCarriesReason) {
    FakeBackfill f;
    f.validate_result = 0;
    f.validate_error = "unique constraint violated: key=42";
    auto res = IndexBuildTask::run(f.callbacks());
    EXPECT_EQ(res.outcome, IndexBuildOutcome::ERROR);
    EXPECT_NE(res.error.find("unique constraint"), std::string::npos);
}

TEST(IndexBuildTask, CancelIsHonouredAtPhaseBoundaries) {
    FakeBackfill f;
    f.rounds_remaining = 5;
    f.cancel_after_rounds = true; // 第一轮之后取消
    auto res = IndexBuildTask::run(f.callbacks());
    EXPECT_EQ(res.outcome, IndexBuildOutcome::CANCELLED);
    EXPECT_EQ(f.rounds_done, 1);
    EXPECT_EQ(std::find(f.log.begin(), f.log.end(), "FINALIZE"), f.log.end()); // 不进入最终阶段
}
