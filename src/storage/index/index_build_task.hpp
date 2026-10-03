#pragma once
/// 索引构建任务的**相位机**（设计 §6：SCAN → CATCHUP(多轮) → FINALIZE → PUBLIC）。
///
/// 为什么单独抽出来：`CREATE INDEX` 现在同步调用它；等后台调度器接上（P1-②）只需把"调用点"
/// 换成"调度器任务"，**流程语义不变**。它把四条最容易写错的规则钉在一处并用单测守住：
///   1. **失败绝不置 PUBLIC**（I4）：任一步失败 ⇒ `ERROR`，`validate` 不会被跳过当作成功；
///   2. **CATCHUP 是循环**：`catch_up_round()` 返回 true 表示"仍有剩余工作"，必须继续下一轮（§6.1）；
///   3. **取消只在阶段边界生效**：每轮/每阶段开始前查取消令牌（§8）；
///   4. **FINALIZE 是唯一分界线**（I3）：关闸 + 排空 + 重放最后一批 + 三项校验都在这一步内完成。
///
/// 无裸指针：全部回调为 `std::function`，结果按值返回。
#include "common/types/index_state.hpp"

#include <functional>
#include <string>
#include <utility>

namespace eugraph {

enum class IndexBuildOutcome {
    PUBLIC,
    ERROR,
    CANCELLED
};

struct IndexBuildResult {
    IndexBuildOutcome outcome = IndexBuildOutcome::ERROR;
    std::string error; ///< outcome == ERROR 时的原因（写入日志/索引状态）
};

class IndexBuildTask {
public:
    struct Callbacks {
        /// 阶段①：扫描基础数据（分批提交由实现内部负责）。返回 false = 失败。
        std::function<bool()> scan_base;
        /// 阶段②：追赶一轮（重放变更表）。返回 **true 表示仍有剩余工作**，需要再追一轮。
        std::function<bool()> catch_up_round;
        /// 阶段③：关闸 + 排空在飞写事务 + 重放最后一批（+ 清理已应用的 delta 行）。返回 false = 失败。
        std::function<bool()> finalize_gate;
        /// 阶段③的校验（delta 已空 / 唯一性 / 条目数护栏）。返回 false = 失败。
        std::function<bool(std::string&)> validate;
        /// 阶段变化通知（可观测；可空）
        std::function<void(const char* phase)> on_phase;
        /// 取消令牌：返回 true 表示已取消（在**阶段边界**检查）
        std::function<bool()> cancelled;
        /// 进度通知（0..100，可空）
        std::function<void(double)> on_progress;
    };

    /// 同步执行完整流程。调用方负责把 `PUBLIC` 落到元数据（本函数只决定"能不能置 PUBLIC"）。
    static IndexBuildResult run(const Callbacks& cb) {
        const auto cancelled = [&cb] { return cb.cancelled && cb.cancelled(); };
        const auto phase = [&cb](const char* name) {
            if (cb.on_phase)
                cb.on_phase(name);
        };

        if (cancelled())
            return IndexBuildResult{IndexBuildOutcome::CANCELLED, "cancelled before scan"};

        phase("SCAN");
        if (!cb.scan_base || !cb.scan_base())
            return IndexBuildResult{IndexBuildOutcome::ERROR, "base scan failed"};
        if (cb.on_progress)
            cb.on_progress(50.0);

        phase("CATCHUP");
        while (true) {
            if (cancelled())
                return IndexBuildResult{IndexBuildOutcome::CANCELLED, "cancelled during catch-up"};
            if (!cb.catch_up_round)
                break;
            const bool more = cb.catch_up_round();
            if (!more)
                break; // 没有剩余工作 ⇒ 进入最终阶段
        }
        if (cb.on_progress)
            cb.on_progress(90.0);

        if (cancelled())
            return IndexBuildResult{IndexBuildOutcome::CANCELLED, "cancelled before finalize"};

        phase("FINALIZE");
        if (!cb.finalize_gate || !cb.finalize_gate())
            return IndexBuildResult{IndexBuildOutcome::ERROR, "finalize (gate/drain/replay) failed"};

        std::string why;
        if (!cb.validate || !cb.validate(why))
            return IndexBuildResult{IndexBuildOutcome::ERROR,
                                    why.empty() ? std::string("final validation failed") : why};

        if (cb.on_progress)
            cb.on_progress(100.0);
        return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}};
    }
};

} // namespace eugraph
