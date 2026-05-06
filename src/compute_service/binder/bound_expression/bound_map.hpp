#pragma once

#include "compute_service/binder/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/binder/bound_type.hpp"

#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

/// Bound map literal: {key1: val1, key2: val2, ...}
struct BoundMap {
    std::vector<std::pair<std::string, BoundExpression>> entries;
};

} // namespace binder
} // namespace eugraph
