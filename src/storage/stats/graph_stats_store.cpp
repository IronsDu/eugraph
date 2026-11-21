#include "storage/stats/graph_stats_store.hpp"

#include <mutex>
#include <vector>

namespace eugraph {

std::shared_ptr<GraphStatsStore::LabelSlot> GraphStatsStore::labelSlot(LabelId id) {
    {
        std::shared_lock<std::shared_mutex> lock(slots_mu_);
        auto it = labels_.find(id);
        if (it != labels_.end())
            return it->second;
    }
    std::unique_lock<std::shared_mutex> lock(slots_mu_);
    auto& slot = labels_[id];
    if (!slot)
        slot = std::make_shared<LabelSlot>();
    return slot;
}

std::shared_ptr<GraphStatsStore::EdgeSlot> GraphStatsStore::edgeSlot(EdgeLabelId id) {
    {
        std::shared_lock<std::shared_mutex> lock(slots_mu_);
        auto it = edge_labels_.find(id);
        if (it != edge_labels_.end())
            return it->second;
    }
    std::unique_lock<std::shared_mutex> lock(slots_mu_);
    auto& slot = edge_labels_[id];
    if (!slot)
        slot = std::make_shared<EdgeSlot>();
    return slot;
}

std::shared_ptr<GraphStatsStore::IndexSlot> GraphStatsStore::indexSlot(uint32_t id) {
    {
        std::shared_lock<std::shared_mutex> lock(slots_mu_);
        auto it = indexes_.find(id);
        if (it != indexes_.end())
            return it->second;
    }
    std::unique_lock<std::shared_mutex> lock(slots_mu_);
    auto& slot = indexes_[id];
    if (!slot)
        slot = std::make_shared<IndexSlot>();
    return slot;
}

void GraphStatsStore::applyCommitted(const StatsDelta& delta) {
    if (delta.empty())
        return;
    // 先复制槽位引用，再在锁外做原子加；槽位生命周期由 shared_ptr 保活。
    std::vector<std::pair<std::shared_ptr<LabelSlot>, int64_t>> vertex_updates;
    vertex_updates.reserve(delta.vertex_delta.size());
    for (const auto& [id, d] : delta.vertex_delta)
        vertex_updates.emplace_back(labelSlot(id), d);

    std::vector<std::pair<std::shared_ptr<EdgeSlot>, int64_t>> edge_updates;
    edge_updates.reserve(delta.edge_delta.size());
    for (const auto& [id, d] : delta.edge_delta)
        edge_updates.emplace_back(edgeSlot(id), d);

    std::vector<std::pair<std::shared_ptr<IndexSlot>, int64_t>> index_updates;
    index_updates.reserve(delta.index_delta.size());
    for (const auto& [id, d] : delta.index_delta)
        index_updates.emplace_back(indexSlot(id), d);

    for (auto& [slot, d] : vertex_updates)
        slot->vertices.fetch_add(d, std::memory_order_relaxed);
    for (auto& [slot, d] : edge_updates)
        slot->edges.fetch_add(d, std::memory_order_relaxed);
    for (auto& [slot, d] : index_updates) {
        slot->rows.fetch_add(d, std::memory_order_relaxed);
        slot->ready.store(true, std::memory_order_release);
    }
}

std::shared_ptr<const GraphStatsSnapshot> GraphStatsStore::snapshot() const {
    auto snap = std::make_shared<GraphStatsSnapshot>();
    snap->epoch = epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    {
        std::shared_lock<std::shared_mutex> lock(slots_mu_);
        for (const auto& [id, slot] : labels_)
            snap->labels[id].vertices = slot->vertices.load(std::memory_order_relaxed);
        for (const auto& [id, slot] : edge_labels_)
            snap->edge_labels[id].edges = slot->edges.load(std::memory_order_relaxed);
        for (const auto& [id, slot] : indexes_) {
            auto& st = snap->indexes[id];
            st.rows = slot->rows.load(std::memory_order_relaxed);
            st.null_keys = slot->null_keys.load(std::memory_order_relaxed);
            st.ready = slot->ready.load(std::memory_order_acquire);
        }
    }
    return snap;
}

} // namespace eugraph
