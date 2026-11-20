#pragma once
/// 索引运行时状态模型（**纯逻辑，无 WT / 无线程原语**）。
///
/// 设计依据：docs/storage/online-index-build-design.md §4 / §10。
///
/// 分层约定（见设计文档 §12「模块划分」）：
///   * 本文件只做**判定**：状态迁移合法性、规划器可用性、写入去向、唯一性是否强制；
///   * 准入/在飞计数/读句柄等有状态的东西在 `index_runtime_registry.hpp`；
///   * 两者都不依赖 WiredTiger，可直接单测（`index_runtime_tests`）。
///
/// 术语：
///   * `IndexPhase`      —— 构建阶段（进程内，决定写入去向）；
///   * `IndexLifecycle`  —— 生命周期（进程内，决定规划器是否可选、数据是否保留）；
///   * 持久化的 `IndexState`（WRITE_ONLY/PUBLIC/ERROR）是 phase 的**投影**，不在这里定义。

#include <cstddef>
#include <cstdint>

namespace eugraph {

/// 构建阶段。`kAbsent` 表示注册表里还没有这个索引（未注册/已清理）。
enum class IndexPhase : uint8_t {
    kAbsent = 0,   ///< 未注册
    kScan = 1,     ///< 扫描基础数据（构建中）
    kCatchup = 2,  ///< 多轮重放变更表（构建中）
    kFinalize = 3, ///< 已关闸：排空 + 最后一批 + 校验
    kPublic = 4,   ///< 完整可用
    kError = 5,    ///< 构建失败（终态，重建 = 新 runtime）
};

/// 生命周期。与 phase 正交：只影响"规划器是否可选 / 维护是否继续 / 数据何时删"。
enum class IndexLifecycle : uint8_t {
    kActive = 0,
    kDropping = 1,     ///< 新计划不可选；在飞读者继续；写者继续直写索引
    kPendingPurge = 2, ///< 等待旧版本引用归零（后台清扫）
    kPurging = 3,      ///< 停止维护，删表中
    kPurged = 4,       ///< 已清理（终态）
};

/// 一次写入对某个索引的去向。
enum class IndexWriteDestination : uint8_t {
    kDelta = 0,      ///< 构建中：写变更表 `idx_delta_<id>`
    kIndexTable = 1, ///< 已发布：直写索引表
    kRetryLater = 2, ///< 收尾中：等待后重试（**绝不**直写索引表）
    kSkip = 3,       ///< 不维护（ERROR / PURGING / 不存在）
};

inline const char* indexPhaseName(IndexPhase p) noexcept {
    switch (p) {
    case IndexPhase::kAbsent:
        return "ABSENT";
    case IndexPhase::kScan:
        return "SCAN";
    case IndexPhase::kCatchup:
        return "CATCHUP";
    case IndexPhase::kFinalize:
        return "FINALIZE";
    case IndexPhase::kPublic:
        return "PUBLIC";
    case IndexPhase::kError:
        return "ERROR";
    }
    return "UNKNOWN";
}

inline const char* indexLifecycleName(IndexLifecycle l) noexcept {
    switch (l) {
    case IndexLifecycle::kActive:
        return "ACTIVE";
    case IndexLifecycle::kDropping:
        return "DROPPING";
    case IndexLifecycle::kPendingPurge:
        return "PENDING_PURGE";
    case IndexLifecycle::kPurging:
        return "PURGING";
    case IndexLifecycle::kPurged:
        return "PURGED";
    }
    return "UNKNOWN";
}

/// "构建中"（SCAN ∪ CATCHUP）：对写路径/读路径/DDL 的语义相同，只是 builder 在做什么不同。
inline bool indexIsBuilding(IndexPhase p) noexcept {
    return p == IndexPhase::kScan || p == IndexPhase::kCatchup;
}

/// phase 迁移是否合法（**只允许前进/回退到明确允许的相邻态**；同态不算迁移）。
inline bool indexPhaseTransitionAllowed(IndexPhase from, IndexPhase to) noexcept {
    switch (from) {
    case IndexPhase::kAbsent:
        return to == IndexPhase::kScan || to == IndexPhase::kError;
    case IndexPhase::kScan:
        return to == IndexPhase::kCatchup || to == IndexPhase::kError;
    case IndexPhase::kCatchup:
        return to == IndexPhase::kFinalize || to == IndexPhase::kError;
    case IndexPhase::kFinalize:
        // 排空超时 ⇒ 回到 CATCHUP；校验失败 ⇒ ERROR；成功 ⇒ PUBLIC。
        return to == IndexPhase::kCatchup || to == IndexPhase::kError || to == IndexPhase::kPublic;
    case IndexPhase::kPublic:
        // DROP 通过 lifecycle 表达，phase 保持 PUBLIC 直到被清理；不允许再改 phase。
        return false;
    case IndexPhase::kError:
        return false; // 终态；重建 = 新建 runtime（新 index_id）
    }
    return false;
}

/// lifecycle 迁移是否合法。
inline bool indexLifecycleTransitionAllowed(IndexLifecycle from, IndexLifecycle to) noexcept {
    switch (from) {
    case IndexLifecycle::kActive:
        return to == IndexLifecycle::kDropping || to == IndexLifecycle::kPurging;
    case IndexLifecycle::kDropping:
        return to == IndexLifecycle::kPendingPurge || to == IndexLifecycle::kPurging;
    case IndexLifecycle::kPendingPurge:
        return to == IndexLifecycle::kPurging;
    case IndexLifecycle::kPurging:
        return to == IndexLifecycle::kPurged;
    case IndexLifecycle::kPurged:
        return false;
    }
    return false;
}

/// 规划器（**新计划**）是否可选这个索引。
///
/// 两个条件缺一不可：
///   1. 当前 phase==PUBLIC ∧ lifecycle==ACTIVE；
///   2. `public_generation <= txn_start_generation` —— 索引必须在事务开始前就已经可用，
///      否则"新索引 + 旧数据快照"会漏行（设计 §10.1）。
///
/// 注意：旧事务 pin 里是 PUBLIC 还不够；DROP 之后必须立刻对新计划不可选（§9.3/§10）。
inline bool indexPlannerSelectable(IndexPhase phase, IndexLifecycle lifecycle, uint64_t public_generation,
                                   uint64_t txn_start_generation) noexcept {
    return phase == IndexPhase::kPublic && lifecycle == IndexLifecycle::kActive &&
           public_generation <= txn_start_generation;
}

/// 写入去向（I2：每个已提交写入只有一个去向）。
///
///   * SCAN/CATCHUP ⇒ 变更表（受准入保护）；
///   * FINALIZE     ⇒ 等待重试（**绝不**直写索引表，I3）；
///   * PUBLIC       ⇒ 直写索引表；
///   * ERROR / PURGING / PURGED / ABSENT ⇒ 不维护。
///
/// 注意：`DROPPING` 期间**仍直写索引表**（保护在飞长查询，§9.3），所以这里不按 lifecycle 停维护，
/// 只有 `PURGING`/`PURGED` 才停。
inline IndexWriteDestination indexWriteDestination(IndexPhase phase, IndexLifecycle lifecycle) noexcept {
    if (lifecycle == IndexLifecycle::kPurging || lifecycle == IndexLifecycle::kPurged)
        return IndexWriteDestination::kSkip;
    if (indexIsBuilding(phase))
        return IndexWriteDestination::kDelta;
    if (phase == IndexPhase::kFinalize)
        return IndexWriteDestination::kRetryLater;
    if (phase == IndexPhase::kPublic)
        return IndexWriteDestination::kIndexTable;
    return IndexWriteDestination::kSkip; // ABSENT / ERROR
}

/// 空索引护栏：扫到了"属性齐全的实体"但一条条目都没产出 ⇒ 索引不可信（历史事故）。
/// 注意：全部实体都缺属性时 scanned=0，不触发（那是合法的空索引）。
inline bool indexShouldFailEmpty(size_t scanned_with_all_props, size_t emitted_entries) noexcept {
    return scanned_with_all_props > 0 && emitted_entries == 0;
}

/// 唯一性检查是否生效：只在 `PUBLIC` 之后（构建期不强制，§8.4）。
inline bool indexUniqueEnforced(IndexPhase phase) noexcept {
    return phase == IndexPhase::kPublic;
}

/// 是否必须保留数据给在飞读者（DROPPING / PENDING_PURGE 期间为真）。
inline bool indexDataRetainedForReaders(IndexLifecycle lifecycle) noexcept {
    return lifecycle == IndexLifecycle::kActive || lifecycle == IndexLifecycle::kDropping ||
           lifecycle == IndexLifecycle::kPendingPurge;
}

} // namespace eugraph
