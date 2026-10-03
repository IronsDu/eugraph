#pragma once
/// 索引状态的**两维模型**与句柄所有权约定。
/// 设计依据：docs/storage/online-index-build-design.md §3（BuildState × Lifecycle）、§7.2/§7.3、I1–I5。
///
/// 约定（实现契约）：
///  1. **持久化的只有 `IndexState`**（WRITE_ONLY / PUBLIC / DELETE_ONLY / ERROR）——已落盘，**不得重编号**；
///  2. `phase` 与 `lifecycle` 是**进程内**状态：本期不持久化、不做崩溃续建（见设计 §9）；
///  3. **读路径只有一个判据**：`plannerUsable()`（`== PUBLIC ∧ ACTIVE`）——I1；
///  4. **写路径只有一个判据**：`writeMaintained()`。P2 之前为"构建中或已发布都维护"（与既有行为逐位一致，
///     否则构建期间的并发写入会直接丢失）；P2 引入变更表后改为按 BuildState 决定**去向**；
///  5. **一切句柄用 `std::shared_ptr`**：句柄存活期间对象不会被释放 ⇒ DROP/清扫不会让在飞查询悬空
///     （不使用裸指针，见 §7.2）。
#include "common/types/graph_types.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace eugraph {

/// 构建阶段（`BUILDING` 的内部阶段；只有"是否改变写入去向"才配当独立状态，见设计 §3）
enum class IndexBuildPhase : uint8_t {
    SCAN = 0,    ///< 扫描基础数据
    CATCHUP = 1, ///< 反复重放变更表
    FINALIZE = 2 ///< 已关闸：排空 + 重放最后一批 + 校验
};

/// 对象生命周期（正交于构建状态：只影响"规划器是否可选/维护是否继续/何时删除"）
enum class IndexLifecycle : uint8_t {
    ACTIVE = 0,        ///< 正常
    DROPPING = 1,      ///< 新计划不可选；**仍维护**（保护在飞长查询，见 §7.2）
    PENDING_PURGE = 2, ///< 等待者由 DROP 语句换成后台清扫；**仍维护**
    PURGING = 3        ///< 使用者已归零：**停止维护**并删除表/变更表/定义（幂等）
};

/// 索引的运行时状态（与持久化的 `IndexState` 并列，进程内维护）
struct IndexRuntimeState {
    IndexState durable = IndexState::WRITE_ONLY;
    IndexBuildPhase phase = IndexBuildPhase::SCAN;
    IndexLifecycle lifecycle = IndexLifecycle::ACTIVE;
    bool build_cancelled = false; ///< 取消构建（如构建中被 DROP）：写路径随即变为"不维护"

    /// **读路径（规划器）唯一判据** —— I1
    bool plannerUsable() const {
        return durable == IndexState::PUBLIC && lifecycle == IndexLifecycle::ACTIVE;
    }

    /// **写路径是否维护该索引**（P2 前与既有行为一致：构建中与已发布都维护）
    bool writeMaintained() const {
        if (purgeStarted() || build_cancelled)
            return false;
        return durable == IndexState::WRITE_ONLY || durable == IndexState::PUBLIC;
    }

    bool buildInProgress() const {
        return durable == IndexState::WRITE_ONLY && !build_cancelled;
    }
    bool purgeStarted() const {
        return lifecycle == IndexLifecycle::PURGING;
    }
    /// 数据是否必须保留给在飞查询（DROPPING/PENDING_PURGE 期间为真）
    bool dataRetainedForReaders() const {
        return lifecycle != IndexLifecycle::PURGING;
    }

    /// 生命周期迁移合法性（改状态的唯一入口用它校验）
    bool canTransitionLifecycleTo(IndexLifecycle next) const {
        switch (lifecycle) {
        case IndexLifecycle::ACTIVE:
            return next == IndexLifecycle::DROPPING;
        case IndexLifecycle::DROPPING:
            return next == IndexLifecycle::PENDING_PURGE || next == IndexLifecycle::PURGING;
        case IndexLifecycle::PENDING_PURGE:
            return next == IndexLifecycle::PURGING;
        case IndexLifecycle::PURGING:
            return false; // 终态
        }
        return false;
    }
};

/// 按**持久化状态**判定"写路径是否维护"（供各层共用，避免各处手写 `state==WRITE_ONLY||PUBLIC`）
inline bool indexWriteMaintained(IndexState durable) {
    IndexRuntimeState st;
    st.durable = durable;
    return st.writeMaintained();
}

/// 按**持久化状态**判定"规划器是否可用"（I1）
inline bool indexPlannerUsable(IndexState durable) {
    IndexRuntimeState st;
    st.durable = durable;
    return st.plannerUsable();
}

/// 索引句柄：**只持 `shared_ptr`**。
/// 语义：① 句柄存活 ⇒ 对象不会被释放（DROP 不会悬空）；② `acquire()` 在 pin 之后**复查可见性**，
/// 失败返回无效句柄，调用方必须**重新计划**（消除"选好索引 ↔ 真正拿 cursor"之间的缺口，§7.2）。
template <typename Entry> class IndexHandle {
public:
    IndexHandle() = default;

    /// 从"已 pin 的对象"构造；`stateOf` 取出其运行时状态用于复查。
    /// 返回无效句柄表示"不可用，请重新计划"。
    template <typename StateFn> static IndexHandle acquire(std::shared_ptr<const Entry> entry, StateFn state_of) {
        if (entry == nullptr)
            return IndexHandle{};
        if (!state_of(*entry).plannerUsable())
            return IndexHandle{};
        return IndexHandle{std::move(entry)};
    }

    bool valid() const {
        return entry_ != nullptr;
    }
    /// 仅在 `valid()` 为真时调用
    const Entry& entry() const {
        return *entry_;
    }
    /// 释放句柄（RAII：析构即释放 pin；此处仅放弃引用）
    void reset() {
        entry_.reset();
    }

private:
    explicit IndexHandle(std::shared_ptr<const Entry> entry) : entry_(std::move(entry)) {}
    std::shared_ptr<const Entry> entry_;
};

} // namespace eugraph
