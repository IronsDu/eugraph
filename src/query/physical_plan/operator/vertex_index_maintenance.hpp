#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/Task.h>

#include <optional>

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

struct VertexIndexEntry {
    std::string table;
    std::vector<PropertyValue> values;
    VertexId vid;
    bool unique = false;
};

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
                    std::optional<PropertyValue> found;
                    bool conflict = false;
                    for (LabelId lid : labels) {
                        auto lit = label_defs.find(lid);
                        if (lit == label_defs.end())
                            continue;
                        uint16_t pid = UINT16_MAX;
                        for (const auto& pd : lit->second.properties) {
                            if (pd.name == acc.property_name) {
                                pid = pd.id;
                                break;
                            }
                        }
                        if (pid == UINT16_MAX)
                            continue;
                        auto props = co_await store.getVertexProperties(vid, lid);
                        if (!props || pid >= props->size() || !(*props)[pid].has_value())
                            continue;
                        const auto& candidate = (*props)[pid].value();
                        if (found.has_value()) {
                            if (!(found.value() == candidate)) {
                                conflict = true;
                                break;
                            }
                        } else {
                            found = candidate;
                        }
                    }
                    if (conflict) {
                        all_present = false;
                        break;
                    }
                    if (!found.has_value()) {
                        all_present = false;
                        break;
                    }
                    values.push_back(std::move(*found));
                }
            }

            if (!all_present)
                continue;

            if (idx.index_id != 0)
                entries.push_back(VertexIndexEntry{vidxTableById(idx.index_id), values, vid, idx.unique});
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
                    std::optional<PropertyValue> found;
                    bool conflict = false;
                    for (const auto& [lid, lp] : label_props) {
                        auto lit = label_defs.find(lid);
                        if (lit == label_defs.end())
                            continue;
                        uint16_t pid = UINT16_MAX;
                        for (const auto& pd : lit->second.properties)
                            if (pd.name == acc.property_name) {
                                pid = pd.id;
                                break;
                            }
                        if (pid == UINT16_MAX || pid >= lp.size() || !lp[pid].has_value())
                            continue;
                        if (found.has_value()) {
                            if (!(found.value() == lp[pid].value())) {
                                conflict = true;
                                break;
                            }
                        } else {
                            found = lp[pid].value();
                        }
                    }
                    if (conflict || !found.has_value()) {
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

inline folly::coro::Task<void> deleteVertexIndexEntries(IAsyncGraphDataStore& store,
                                                        const std::vector<VertexIndexEntry>& entries) {
    for (const auto& entry : entries)
        co_await store.deleteIndexEntry(entry.table, entry.values, entry.vid);
}

inline folly::coro::Task<bool> insertVertexIndexEntriesChecked(IAsyncGraphDataStore& store,
                                                               const std::vector<VertexIndexEntry>& entries) {
    for (const auto& entry : entries) {
        if (entry.unique) {
            bool constraint_ok = co_await store.checkUniqueConstraint(entry.table, entry.values);
            if (!constraint_ok) {
                spdlog::warn("Unique index constraint violated while maintaining index entry for vertex {}", entry.vid);
                co_return false;
            }
        }
        co_await store.insertIndexEntry(entry.table, entry.values, entry.vid);
    }
    co_return true;
}

inline folly::coro::Task<void> insertVertexIndexEntries(IAsyncGraphDataStore& store,
                                                        const std::vector<VertexIndexEntry>& entries) {
    (void)co_await insertVertexIndexEntriesChecked(store, entries);
}

} // namespace compute
} // namespace eugraph
