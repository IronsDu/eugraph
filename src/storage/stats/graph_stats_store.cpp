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

void GraphStatsStore::applyCommitted(const StatsDelta& delta, uint64_t journal_seq) {
    if (journal_seq != 0) {
        uint64_t cur = applied_journal_seq_.load(std::memory_order_acquire);
        while (cur < journal_seq &&
               !applied_journal_seq_.compare_exchange_weak(cur, journal_seq, std::memory_order_acq_rel)) {}
    }
    if (delta.empty())
        return;
    // 先复制槽位引用，再在锁外做原子加；slots_mu_ 只保护 map 结构，
    // 不保护这里的计数器加法。槽位生命周期由 shared_ptr 保活。
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

void GraphStatsStore::restore(const GraphStatsSnapshot& snapshot) {
    std::unique_lock<std::shared_mutex> lock(slots_mu_);
    labels_.clear();
    edge_labels_.clear();
    indexes_.clear();
    for (const auto& [id, st] : snapshot.labels) {
        auto slot = std::make_shared<LabelSlot>();
        slot->vertices.store(st.vertices, std::memory_order_relaxed);
        labels_[id] = std::move(slot);
    }
    for (const auto& [id, st] : snapshot.edge_labels) {
        auto slot = std::make_shared<EdgeSlot>();
        slot->edges.store(st.edges, std::memory_order_relaxed);
        edge_labels_[id] = std::move(slot);
    }
    for (const auto& [id, st] : snapshot.indexes) {
        auto slot = std::make_shared<IndexSlot>();
        slot->rows.store(st.rows, std::memory_order_relaxed);
        slot->null_keys.store(st.null_keys, std::memory_order_relaxed);
        slot->ready.store(st.ready, std::memory_order_release);
        indexes_[id] = std::move(slot);
    }
    epoch_.store(snapshot.epoch, std::memory_order_release);
    applied_journal_seq_.store(snapshot.applied_journal_seq, std::memory_order_release);
}

std::shared_ptr<const GraphStatsSnapshot> GraphStatsStore::snapshot() const {
    auto snap = std::make_shared<GraphStatsSnapshot>();
    snap->epoch = epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    snap->applied_journal_seq = applied_journal_seq_.load(std::memory_order_acquire);
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
