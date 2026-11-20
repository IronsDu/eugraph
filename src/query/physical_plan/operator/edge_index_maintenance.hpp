#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/index/index_entry.hpp"
#include "storage/index/index_write_applier.hpp"

#include <folly/coro/Task.h>

#include <spdlog/spdlog.h>

#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

using EdgeIndexEntry = ::eugraph::EdgeIndexEntry;

inline folly::coro::Task<std::vector<EdgeIndexEntry>>
collectEdgeIndexEntries(IAsyncGraphDataStore& store,
                        const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs, EdgeId eid,
                        EdgeLabelId elid) {
    std::vector<EdgeIndexEntry> entries;
    auto def_it = edge_label_defs.find(elid);
    if (def_it == edge_label_defs.end())
        co_return entries;

    auto props = co_await store.getEdgeProperties(elid, eid);
    for (const auto& idx : def_it->second.indexes) {
        if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
            continue;

        std::vector<PropertyValue> values;
        bool all_present = true;
        for (uint16_t pid : idx.prop_ids) {
            if (props && pid < props->size() && (*props)[pid].has_value())
                values.push_back((*props)[pid].value());
            else {
                all_present = false;
                break;
            }
        }
        if (!all_present)
            continue;
        auto table =
            idx.prop_ids.size() == 1 ? eidxTable(elid, idx.prop_ids[0]) : eidxCompositeTable(elid, idx.prop_ids);
        entries.push_back(EdgeIndexEntry{std::move(table), std::move(values), eid, idx.unique, idx.index_id});
    }
    co_return entries;
}

inline IndexWriteItem toIndexWriteItem(const EdgeIndexEntry& entry, bool is_delete) {
    IndexWriteItem item;
    item.index_id = entry.index_id;
    item.legacy_table = entry.table;
    item.values = entry.values;
    item.entity_id = entry.eid;
    item.unique = entry.unique;
    item.is_delete = is_delete;
    return item;
}

inline folly::coro::Task<void> deleteEdgeIndexEntries(IAsyncGraphDataStore& store,
                                                      const std::vector<EdgeIndexEntry>& entries) {
    for (const auto& entry : entries) {
        const auto status = co_await applyIndexWrite(store, toIndexWriteItem(entry, /*is_delete=*/true));
        if (status == IndexWriteStatus::kRetryLater)
            co_return;
    }
}

inline folly::coro::Task<void> insertEdgeIndexEntries(IAsyncGraphDataStore& store,
                                                      const std::vector<EdgeIndexEntry>& entries) {
    for (const auto& entry : entries) {
        const auto status = co_await applyIndexWrite(store, toIndexWriteItem(entry, /*is_delete=*/false));
        if (status == IndexWriteStatus::kUniqueConflict) {
            spdlog::warn("Unique edge index constraint violated while maintaining index entry for edge {}", entry.eid);
            continue;
        }
        if (status == IndexWriteStatus::kRetryLater)
            co_return;
    }
}

} // namespace compute
} // namespace eugraph
