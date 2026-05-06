#pragma once

#include "compute_service/binder/bound_type.hpp"

#include <string>

namespace eugraph {
namespace binder {

/// Bound parameter reference ($param_name).
/// The actual value is provided at execution time.
struct BoundParameter {
    std::string name;
    BoundType expected_type; // inferred from usage context

    BoundParameter() : expected_type(BoundType::Any()) {}
    explicit BoundParameter(std::string n, BoundType t = BoundType::Any())
        : name(std::move(n)), expected_type(std::move(t)) {}
};

} // namespace binder
} // namespace eugraph
