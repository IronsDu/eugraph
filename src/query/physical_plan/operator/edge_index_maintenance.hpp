#pragma once

#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/Task.h>

#include <spdlog/spdlog.h>

#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

struct EdgeIndexEntry {
    std::string table;
    std::vector<PropertyValue> values;
    EdgeId eid;
    bool unique = false;
};

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
        entries.push_back(EdgeIndexEntry{std::move(table), std::move(values), eid, idx.unique});
    }
    co_return entries;
}

inline folly::coro::Task<void> deleteEdgeIndexEntries(IAsyncGraphDataStore& store,
                                                      const std::vector<EdgeIndexEntry>& entries) {
    for (const auto& entry : entries)
        co_await store.deleteIndexEntry(entry.table, entry.values, entry.eid);
}

inline folly::coro::Task<void> insertEdgeIndexEntries(IAsyncGraphDataStore& store,
                                                      const std::vector<EdgeIndexEntry>& entries) {
    for (const auto& entry : entries) {
        if (entry.unique) {
            bool constraint_ok = co_await store.checkUniqueConstraint(entry.table, entry.values);
            if (!constraint_ok) {
                spdlog::warn("Unique edge index constraint violated while maintaining index entry for edge {}",
                             entry.eid);
                continue;
            }
        }
        co_await store.insertIndexEntry(entry.table, entry.values, entry.eid);
    }
}

} // namespace compute
} // namespace eugraph
