#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <spdlog/spdlog.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {

/// 索引维护的**存储层**实现。
///
/// 为什么放在这里：批量导入（`batchInsertVertices` / `batchInsertEdges`）必须和
/// 查询写路径一样维护索引，否则「索引静默变脏」。批量路径在数据存储层，若直接
/// 复用 `query/physical_plan/operator/*_index_maintenance.hpp`（compute 命名空间）
/// 会形成 storage → query 的反向依赖，因此把收集/校验/写入逻辑下沉到本文件，
/// 查询算子改为调用它（行为不变）。
///
/// 语义与查询写路径一致：
///   - 只维护 state ∈ {WRITE_ONLY, PUBLIC} 且 index_id != 0 的索引；
///   - 弱 accessor 按属性名在实体的所有标签里取值，多标签冲突则跳过该条目；
///   - 任一 accessor 缺值则不产生条目；
///   - unique 索引先 `checkUniqueConstraint`，冲突时**跳过**（不抛异常、不阻断导入）。
struct VertexIndexEntry {
    std::string table;
    std::vector<PropertyValue> values;
    VertexId vid = INVALID_VERTEX_ID;
    bool unique = false;
};

struct EdgeIndexEntry {
    std::string table;
    std::vector<PropertyValue> values;
    EdgeId eid = INVALID_EDGE_ID;
    bool unique = false;
};

namespace index_maint_detail {

/// 在标签定义里按属性名解析 prop_id；找不到返回 UINT16_MAX。
inline uint16_t findPropId(const std::unordered_map<LabelId, LabelDef>& label_defs, LabelId label_id,
                           const std::string& prop_name) {
    auto it = label_defs.find(label_id);
    if (it == label_defs.end())
        return UINT16_MAX;
    for (const auto& pd : it->second.properties) {
        if (pd.name == prop_name)
            return pd.id;
    }
    return UINT16_MAX;
}

/// 从一组 (标签, 属性) 里按属性名做弱 accessor 取值：单处命中取该值，
/// 多处命中且值相同取该值，值冲突返回 nullopt（调用方跳过该条目）。
inline std::optional<PropertyValue> resolveWeakAccessor(const std::unordered_map<LabelId, LabelDef>& label_defs,
                                                        const std::vector<std::pair<LabelId, Properties>>& label_props,
                                                        const std::string& prop_name) {
    std::optional<PropertyValue> found;
    for (const auto& [lid, props] : label_props) {
        uint16_t pid = findPropId(label_defs, lid, prop_name);
        if (pid == UINT16_MAX || pid >= props.size() || !props[pid].has_value())
            continue;
        const auto& candidate = props[pid].value();
        if (found.has_value()) {
            if (!(found.value() == candidate))
                return std::nullopt; // 多标签取值冲突
        } else {
            found = candidate;
        }
    }
    return found;
}

inline bool indexIsLive(const LabelDef::IndexDef& idx) {
    return idx.state == IndexState::WRITE_ONLY || idx.state == IndexState::PUBLIC;
}

} // namespace index_maint_detail

/// 依据即将写入的 (标签, 属性) 计算顶点应当写入的索引条目。
/// 供 `batchInsertVertices` 在写入顶点后调用。
inline std::vector<VertexIndexEntry>
collectVertexIndexEntriesFromLabelProps(const std::unordered_map<LabelId, LabelDef>& label_defs,
                                        const std::vector<std::pair<LabelId, Properties>>& label_props, VertexId vid) {
    using namespace index_maint_detail;
    std::vector<VertexIndexEntry> entries;

    for (const auto& [filter_label, filter_props] : label_props) {
        (void)filter_props;
        auto def_it = label_defs.find(filter_label);
        if (def_it == label_defs.end())
            continue;

        for (const auto& idx : def_it->second.indexes) {
            if (!indexIsLive(idx) || idx.index_id == 0)
                continue;

            std::vector<PropertyValue> values;
            bool all_present = true;
            for (const auto& acc : idx.accessors) {
                if (acc.is_strong) {
                    uint16_t pid = findPropId(label_defs, acc.source_label_id, acc.property_name);
                    const Properties* src = nullptr;
                    for (const auto& [lid, props] : label_props) {
                        if (lid == acc.source_label_id) {
                            src = &props;
                            break;
                        }
                    }
                    if (src == nullptr || pid == UINT16_MAX || pid >= src->size() || !(*src)[pid].has_value()) {
                        all_present = false;
                        break;
                    }
                    values.push_back((*src)[pid].value());
                } else {
                    auto found = resolveWeakAccessor(label_defs, label_props, acc.property_name);
                    if (!found.has_value()) {
                        all_present = false;
                        break;
                    }
                    values.push_back(std::move(*found));
                }
            }
            if (all_present)
                entries.push_back(VertexIndexEntry{vidxTableById(idx.index_id), std::move(values), vid, idx.unique});
        }
    }
    return entries;
}

/// 依据边属性计算边应当写入的索引条目（边索引按 prop_ids 组织，见 IndexDef）。
inline std::vector<EdgeIndexEntry> collectEdgeIndexEntries(const std::unordered_map<EdgeLabelId, EdgeLabelDef>& defs,
                                                           EdgeLabelId elid, EdgeId eid, const Properties& props) {
    std::vector<EdgeIndexEntry> entries;
    auto def_it = defs.find(elid);
    if (def_it == defs.end())
        return entries;

    for (const auto& idx : def_it->second.indexes) {
        if (!index_maint_detail::indexIsLive(idx))
            continue;

        std::vector<PropertyValue> values;
        bool all_present = true;
        for (uint16_t pid : idx.prop_ids) {
            if (pid < props.size() && props[pid].has_value()) {
                values.push_back(props[pid].value());
            } else {
                all_present = false;
                break;
            }
        }
        if (!all_present)
            continue;

        auto table =
            idx.prop_ids.size() == 1 ? eidxTable(elid, idx.prop_ids[0]) : eidxCompositeTable(elid, idx.prop_ids);
        entries.push_back(EdgeIndexEntry{std::move(table), std::move(values), eid, idx.unique});
    }
    return entries;
}

/// 写入顶点索引条目。unique 索引冲突时跳过并返回冲突计数（不抛异常）。
inline int insertVertexIndexEntries(ISyncGraphDataStore& store, GraphTxnHandle txn,
                                    const std::vector<VertexIndexEntry>& entries) {
    int conflicts = 0;
    for (const auto& entry : entries) {
        if (entry.unique && !store.checkUniqueConstraint(txn, entry.table, entry.values)) {
            spdlog::warn("Unique index constraint violated while maintaining index entry for vertex {}", entry.vid);
            ++conflicts;
            continue;
        }
        store.insertIndexEntry(txn, entry.table, entry.values, entry.vid);
    }
    return conflicts;
}

/// 写入边索引条目。unique 索引冲突时跳过并返回冲突计数（不抛异常）。
inline int insertEdgeIndexEntries(ISyncGraphDataStore& store, GraphTxnHandle txn,
                                  const std::vector<EdgeIndexEntry>& entries) {
    int conflicts = 0;
    for (const auto& entry : entries) {
        if (entry.unique && !store.checkUniqueConstraint(txn, entry.table, entry.values)) {
            spdlog::warn("Unique edge index constraint violated while maintaining index entry for edge {}", entry.eid);
            ++conflicts;
            continue;
        }
        store.insertIndexEntry(txn, entry.table, entry.values, entry.eid);
    }
    return conflicts;
}

} // namespace eugraph
