#pragma once

#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

/// Leaf operator for EXISTS sub-plan: provides correlated variable values
/// injected from the outer scope at execution time.
struct BoundCorrelatedSourceOp {
    std::vector<std::string> variables;
    std::vector<BoundType> types;
    std::vector<uint32_t> column_indices;
    /// SlotIds matching variables (same order). Set by bindExistsSubPlan from
    /// correlation entries so the physical planner can build the correct
    /// TupleSlotLayout without allocating new slots via slot_allocator.
    std::vector<SlotId> slot_ids;
};

} // namespace binder
} // namespace eugraph
