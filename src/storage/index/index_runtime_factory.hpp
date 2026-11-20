#pragma once
/// 从持久化的索引定义构造 `IndexRuntime` 描述（表名解析只做一次）。
///
/// 单独成文件的原因：`index_runtime_registry.hpp` 是纯运行时（不依赖 GraphSchema/表命名），
/// 而"定义 → 运行时"的映射需要 `graph_types` 与 `constants`，放在这里可以各自单测。

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/index/index_runtime_registry.hpp"

#include <cstdint>

namespace eugraph {

/// 索引的**物理表名**（与 CREATE INDEX / 扫描侧保持一致）。
/// 顶点索引按 index_id；边索引历史上按 (label, prop...) 命名，这里保持同一规则。
inline std::string indexTableName(const LabelDef::IndexDef& idx, uint32_t label_id, bool is_edge) {
    if (is_edge)
        return eidxCompositeTable(static_cast<EdgeLabelId>(label_id), idx.prop_ids);
    return vidxTableById(idx.index_id);
}

/// 由定义构造运行时描述。默认按持久化状态映射：
///   PUBLIC → phase=PUBLIC；其余（WRITE_ONLY 已被启动恢复转 ERROR）→ phase=ERROR。
/// 新建索引的调用方需要把 `initial_phase` 覆盖成 `kScan`（见 QueryExecutor::registerIndexRuntime）。
inline IndexRuntime::Descriptor makeIndexRuntimeDescriptor(const LabelDef::IndexDef& idx, uint32_t label_id,
                                                           bool is_edge) {
    IndexRuntime::Descriptor desc;
    desc.index_id = idx.index_id;
    desc.label_id = label_id;
    desc.is_edge = is_edge;
    desc.unique = idx.unique;
    desc.index_table = indexTableName(idx, label_id, is_edge);
    desc.delta_table = indexDeltaTable(idx.index_id);
    desc.initial_phase = idx.state == IndexState::PUBLIC ? IndexPhase::kPublic : IndexPhase::kError;
    desc.initial_lifecycle = IndexLifecycle::kActive;
    desc.public_generation = 0; // 尚无 schema_generation；P1b 接入
    return desc;
}

} // namespace eugraph
