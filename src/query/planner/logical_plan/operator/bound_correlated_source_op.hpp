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
};

} // namespace binder
} // namespace eugraph
