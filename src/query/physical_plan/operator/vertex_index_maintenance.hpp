#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/index/index_entry.hpp"
#include "storage/index/index_write_applier.hpp"

#include <folly/coro/Task.h>

#include <optional>
#include <stdexcept>

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

using VertexIndexEntry = ::eugraph::VertexIndexEntry;

/// Collect all index entries the vertex currently contributes to.
/// Index definitions live in label_defs keyed by their filter label.
inline folly::coro::Task<std::vector<VertexIndexEntry>>
collectVertexIndexEntries(IAsyncGraphDataStore& store, const std::unordered_map<LabelId, LabelDef>& label_defs,
                          VertexId vid, LabelId anon_label_id) {
    std::vector<VertexIndexEntry> entries;

    auto labels = co_await store.getVertexLabels(vid);
    if (anon_label_id != INVALID_LABEL_ID && label_defs.count(anon_label_id))
        labels.insert(anon_label_id);

    for (LabelId filter_label : labels) {
        auto def_it = label_defs.find(filter_label);
        if (def_it == label_defs.end())
            continue;

        for (const auto& idx : def_it->second.indexes) {
            if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                continue;

            std::vector<PropertyValue> values;
            bool all_present = true;
            for (const auto& acc : idx.accessors) {
                if (acc.is_strong) {
                    auto src_it = label_defs.find(acc.source_label_id);
                    uint16_t pid = UINT16_MAX;
                    if (src_it != label_defs.end()) {
                        for (const auto& pd : src_it->second.properties) {
                            if (pd.name == acc.property_name) {
                                pid = pd.id;
                                break;
                            }
                        }
                    }
                    if (pid == UINT16_MAX) {
                        all_present = false;
                        break;
                    }
                    auto props = co_await store.getVertexProperties(vid, acc.source_label_id);
                    if (props && pid < props->size() && (*props)[pid].has_value())
                        values.push_back((*props)[pid].value());
                    else {
                        all_present = false;
                        break;
                    }
                } else {
                    // Weak accessor: resolve the property's homes. A vertex
                    // with more than one home is ambiguous and cannot be
                    // represented in a weak index -> fail the write/backfill.
                    int home_count = 0;
                    LabelId home_label = INVALID_LABEL_ID;
                    bool home_anon = false;
                    std::optional<PropertyValue> anon_value;
                    for (LabelId lid : labels) {
                        if (lid == anon_label_id)
                            continue;
                        auto lit = label_defs.find(lid);
                        if (lit == label_defs.end())
                            continue;
                        for (const auto& pd : lit->second.properties) {
                            if (pd.name == acc.property_name) {
                                home_count++;
                                home_label = lid;
                                break;
                            }
                        }
                    }
                    if (anon_label_id != INVALID_LABEL_ID && labels.count(anon_label_id)) {
                        auto ait = label_defs.find(anon_label_id);
                        if (ait != label_defs.end()) {
                            auto props = co_await store.getVertexProperties(vid, anon_label_id);
                            for (const auto& pd : ait->second.properties) {
                                if (pd.name != acc.property_name)
                                    continue;
                                if (props && pd.id < props->size() && (*props)[pd.id].has_value()) {
                                    home_count++;
                                    home_anon = true;
                                    anon_value = (*props)[pd.id].value();
                                }
                                break;
                            }
                        }
                    }
                    if (home_count > 1) {
                        throw std::runtime_error("AmbiguousProperty: property '" + acc.property_name +
                                                 "' exists in multiple places on vertex " + std::to_string(vid) +
                                                 "; clean it up before using a weak index");
                    }
                    if (home_count == 0) {
                        all_present = false;
                        break;
                    }
                    if (home_anon) {
                        values.push_back(std::move(*anon_value));
                    } else {
                        auto props = co_await store.getVertexProperties(vid, home_label);
                        uint16_t pid = UINT16_MAX;
                        auto lit = label_defs.find(home_label);
                        if (lit != label_defs.end()) {
                            for (const auto& pd : lit->second.properties) {
                                if (pd.name == acc.property_name) {
                                    pid = pd.id;
                                    break;
                                }
                            }
                        }
                        if (!props || pid == UINT16_MAX || pid >= props->size() || !(*props)[pid].has_value()) {
                            all_present = false;
                            break;
                        }
                        values.push_back((*props)[pid].value());
                    }
                }
            }

            if (!all_present)
                continue;

            if (idx.index_id != 0)
                entries.push_back(VertexIndexEntry{vidxTableById(idx.index_id), values, vid, idx.unique, idx.index_id});
        }
    }

    co_return entries;
}

inline std::vector<VertexIndexEntry>
collectVertexIndexEntriesFromLabelProps(const std::unordered_map<LabelId, LabelDef>& label_defs,
                                        const std::vector<std::pair<LabelId, Properties>>& label_props, VertexId vid) {
    std::vector<VertexIndexEntry> entries;

    auto props_for = [&](LabelId lid) -> const Properties* {
        for (const auto& [l, p] : label_props)
            if (l == lid)
                return &p;
        return nullptr;
    };

    for (const auto& [filter_label, props] : label_props) {
        auto def_it = label_defs.find(filter_label);
        if (def_it == label_defs.end())
            continue;
        for (const auto& idx : def_it->second.indexes) {
            if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                continue;
            if (idx.index_id == 0)
                continue;

            std::vector<PropertyValue> values;
            bool all_present = true;
            for (const auto& acc : idx.accessors) {
                if (acc.is_strong) {
                    const Properties* src = props_for(acc.source_label_id);
                    uint16_t pid = UINT16_MAX;
                    auto sit = label_defs.find(acc.source_label_id);
                    if (sit != label_defs.end()) {
                        for (const auto& pd : sit->second.properties)
                            if (pd.name == acc.property_name) {
                                pid = pd.id;
                                break;
                            }
                    }
                    if (!src || pid == UINT16_MAX || pid >= src->size() || !(*src)[pid].has_value()) {
                        all_present = false;
                        break;
                    }
                    values.push_back((*src)[pid].value());
                } else {
                    // Weak accessor: one home only (see the store-based
                    // overload). Multiple homes are ambiguous -> fail the write.
                    int home_count = 0;
                    const Properties* home_props = nullptr;
                    uint16_t home_pid = UINT16_MAX;
                    std::optional<PropertyValue> anon_value;
                    for (const auto& [lid, lp] : label_props) {
                        auto lit = label_defs.find(lid);
                        if (lit == label_defs.end())
                            continue;
                        for (const auto& pd : lit->second.properties) {
                            if (pd.name != acc.property_name)
                                continue;
                            if (lit->second.name == std::string(kAnonLabelName)) {
                                if (pd.id < lp.size() && lp[pd.id].has_value()) {
                                    home_count++;
                                    anon_value = lp[pd.id].value();
                                }
                            } else {
                                home_count++;
                                home_props = &lp;
                                home_pid = pd.id;
                            }
                            break;
                        }
                    }
                    if (home_count > 1) {
                        throw std::runtime_error("AmbiguousProperty: property '" + acc.property_name +
                                                 "' exists in multiple places on vertex " + std::to_string(vid) +
                                                 "; clean it up before using a weak index");
                    }
                    if (home_count == 0) {
                        all_present = false;
                        break;
                    }
                    if (anon_value.has_value()) {
                        values.push_back(std::move(*anon_value));
                    } else {
                        if (!home_props || home_pid >= home_props->size() || !(*home_props)[home_pid].has_value()) {
                            all_present = false;
                            break;
                        }
                        values.push_back((*home_props)[home_pid].value());
                    }
                }
            }
            if (all_present)
                entries.push_back(
                    VertexIndexEntry{vidxTableById(idx.index_id), std::move(values), vid, idx.unique, idx.index_id});
        }
    }
    return entries;
}

inline IndexWriteItem toIndexWriteItem(const VertexIndexEntry& entry, bool is_delete) {
    IndexWriteItem item;
    item.index_id = entry.index_id;
    item.legacy_table = entry.table;
    item.values = entry.values;
    item.entity_id = entry.vid;
    item.unique = entry.unique;
    item.is_delete = is_delete;
    return item;
}

inline folly::coro::Task<void> deleteVertexIndexEntries(IAsyncGraphDataStore& store,
                                                        const std::vector<VertexIndexEntry>& entries) {
    for (const auto& entry : entries) {
        const auto status = co_await applyIndexWrite(store, toIndexWriteItem(entry, /*is_delete=*/true));
        if (status == IndexWriteStatus::kRetryLater || status == IndexWriteStatus::kUniqueConflict)
            co_return; // 失败已由底层粘到会话；不要继续写半套
    }
}

inline folly::coro::Task<bool> insertVertexIndexEntriesChecked(IAsyncGraphDataStore& store,
                                                               const std::vector<VertexIndexEntry>& entries) {
    for (const auto& entry : entries) {
        const auto status = co_await applyIndexWrite(store, toIndexWriteItem(entry, /*is_delete=*/false));
        if (status == IndexWriteStatus::kUniqueConflict) {
            spdlog::warn("Unique index constraint violated while maintaining index entry for vertex {}", entry.vid);
            co_return false;
        }
        if (status == IndexWriteStatus::kRetryLater) {
            spdlog::error("Index write could not be admitted (index {}); statement will fail", entry.index_id);
            co_return false;
        }
    }
    co_return true;
}

inline folly::coro::Task<void> insertVertexIndexEntries(IAsyncGraphDataStore& store,
                                                        const std::vector<VertexIndexEntry>& entries) {
    (void)co_await insertVertexIndexEntriesChecked(store, entries);
}

} // namespace compute
} // namespace eugraph
