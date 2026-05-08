#pragma once

#include "compute_service/planner/bound_type.hpp"

#include <cstdint>
#include <string>

namespace eugraph {
namespace binder {

/// Reference to a column in the current DataChunk.
/// Variables (n, r, etc.) are resolved to column indices during binding.
struct BoundColumnRef {
    uint32_t column_index = 0;
    BoundType type;
    std::string name; // original variable name (for debugging / error messages)

    BoundColumnRef() : type(BoundType::Any()) {}
    BoundColumnRef(uint32_t idx, BoundType t, std::string n)
        : column_index(idx), type(std::move(t)), name(std::move(n)) {}
};

} // namespace binder
} // namespace eugraph
