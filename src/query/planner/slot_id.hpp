#pragma once

#include <cstdint>

namespace eugraph {
namespace binder {

/// Global unique logical identifier for a column.  Assigned by the Binder
/// and immutable for the lifetime of the query.  BoundColumnRef carries
/// a slot_id instead of (or in addition to) a physical column_index.
using SlotId = uint32_t;

constexpr SlotId INVALID_SLOT_ID = 0;

/// SlotId is partitioned into two disjoint ranges so that user-visible
/// slots (allocated for variables in the bound tree) and internal slots
/// (allocated by the lowering passes for PE / passthrough-derived
/// columns) can never collide:
///
///   user slots     : [1,           kInternalSlotFlag)
///   internal slots : [kInternalSlotFlag, 2^32)
///
/// This partition is structural — not a naming convention. Call sites
/// that need to filter internal columns (e.g. RETURN *, findColumn)
/// can use isUserSlot() / isInternalSlot() rather than relying on the
/// `__pe_*` / `__fwd_*` name prefixes.
///
/// See docs/query/demand-pull-lowering-design.md §13.2-B.
constexpr SlotId kInternalSlotFlag = 0x80000000U; // bit 31

constexpr bool isInternalSlot(SlotId s) {
    return (s & kInternalSlotFlag) != 0;
}

constexpr bool isUserSlot(SlotId s) {
    return s != INVALID_SLOT_ID && !isInternalSlot(s);
}

/// Allocates globally-unique SlotIds during binding.  Each call to
/// next() returns a fresh user id, starting from 1 (0 = invalid);
/// nextInternal() returns a fresh internal id with the high bit set.
class SlotAllocator {
public:
    SlotId next() {
        return ++next_user_;
    }
    SlotId nextInternal() {
        return ++next_internal_;
    }
    SlotId current() const {
        return next_user_;
    }
    void reset() {
        next_user_ = 0;
        next_internal_ = kInternalSlotFlag;
    }
    void seed(SlotId start) {
        if (isInternalSlot(start)) {
            if (start > next_internal_)
                next_internal_ = start;
        } else if (start > next_user_) {
            next_user_ = start;
        }
    }

private:
    SlotId next_user_ = 0;
    SlotId next_internal_ = kInternalSlotFlag;
};

} // namespace binder
} // namespace eugraph
