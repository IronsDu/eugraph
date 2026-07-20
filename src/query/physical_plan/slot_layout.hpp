#pragma once

#include "query/planner/slot_id.hpp"

#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

/// Maps logical binder::SlotId → physical column index (array subscript into
/// a DataChunk's columns vector).  Every physical operator owns or
/// receives a TupleSlotLayout describing its output.
class TupleSlotLayout {
public:
    TupleSlotLayout() = default;

    /// Append a slot at the next physical position.
    /// First occurrence wins: when the same slot is appended multiple times
    /// (e.g. LeftJoin's right child re-emits correlated columns from the
    /// left), only the FIRST position is recorded. This is what callers need
    /// — a column reference resolves to the leftmost (always-non-null) copy,
    /// not a later duplicate that may be null in unmatched rows.
    TupleSlotLayout& append(binder::SlotId id) {
        slots_.push_back(id);
        if (index_map_.find(id) == index_map_.end())
            index_map_[id] = static_cast<uint32_t>(slots_.size() - 1);
        return *this;
    }

    /// Look up the physical column index for a slot.  Returns -1 when
    /// the slot is not present (e.g. variable is out of scope).
    int getColumnIndex(binder::SlotId id) const {
        auto it = index_map_.find(id);
        return (it != index_map_.end()) ? static_cast<int>(it->second) : -1;
    }

    /// Number of physical columns.
    size_t size() const {
        return slots_.size();
    }

    /// Get the slot at physical position `col`.
    binder::SlotId slotAt(size_t col) const {
        return (col < slots_.size()) ? slots_[col] : binder::INVALID_SLOT_ID;
    }

    /// Merge another layout by appending all its slots.  Used by
    /// BinaryJoin / LeftJoin / SemiJoin to produce the combined
    /// output layout.
    void merge(const TupleSlotLayout& other) {
        for (size_t i = 0; i < other.slots_.size(); ++i)
            append(other.slots_[i]);
    }

    /// Replace all occurences of `old_slot` with `new_slot` (WITH
    /// aliasing / Project).
    void replace(binder::SlotId old_slot, binder::SlotId new_slot) {
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i] == old_slot) {
                slots_[i] = new_slot;
                index_map_.erase(old_slot);
                index_map_[new_slot] = static_cast<uint32_t>(i);
            }
        }
    }

    /// Raw slot list (index = physical column).
    const std::vector<binder::SlotId>& slots() const {
        return slots_;
    }

private:
    std::vector<binder::SlotId> slots_;
    std::unordered_map<binder::SlotId, uint32_t> index_map_;
};

} // namespace compute
} // namespace eugraph
