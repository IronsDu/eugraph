#pragma once

#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <string>

namespace eugraph {
namespace binder {

/// Reference to a column in the current DataChunk.
/// Variables (n, r, etc.) are resolved to column indices during binding.
///
/// `slot_id` is the logical identifier (assigned by the Binder, immutable).
/// `column_index` is the physical position, resolved from slot_id at
/// operator init time by ExpressionCompiler.  During planning, column_index
/// may hold the binder-assigned legacy index (for backward-compat during
/// the transition).
struct BoundColumnRef {
    uint32_t column_index = 0;
    uint32_t slot_id = 0; // logical slot (0 = unassigned / not yet resolved)
    BoundType type;
    std::string name; // original variable name (for debugging / error messages)

    BoundColumnRef() : type(BoundType::Any()) {}
    BoundColumnRef(uint32_t idx, BoundType t, std::string n)
        : column_index(idx), type(std::move(t)), name(std::move(n)) {}
    BoundColumnRef(uint32_t idx, BoundType t, std::string n, uint32_t slot)
        : column_index(idx), slot_id(slot), type(std::move(t)), name(std::move(n)) {}
};

} // namespace binder
} // namespace eugraph
