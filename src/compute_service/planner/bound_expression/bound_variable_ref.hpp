#pragma once

#include "compute_service/planner/bound_type.hpp"

#include <string>

namespace eugraph {
namespace binder {

/// Symbolic reference to a variable in the logical plan.
///
/// Unlike BoundColumnRef (which holds a physical column_index),
/// BoundVariableRef holds the variable name string. It is used
/// during the logical plan phase (Binder → Optimizer) where
/// column layout is not yet fixed.
///
/// ColumnResolver converts BoundVariableRef → BoundColumnRef
/// once the physical schema is finalized.
struct BoundVariableRef {
    std::string name;
    BoundType type;

    BoundVariableRef() : type(BoundType::Any()) {}
    BoundVariableRef(std::string n, BoundType t) : name(std::move(n)), type(std::move(t)) {}
};

} // namespace binder
} // namespace eugraph
