#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

/// Bound map literal: {key1: val1, key2: val2, ...}
struct BoundMap {
    std::vector<std::pair<std::string, BoundExpression>> entries;
    BoundType result_type = BoundType::Map(BoundType::String(), BoundType::Any());
};

} // namespace binder
} // namespace eugraph
