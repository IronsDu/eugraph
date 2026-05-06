#pragma once

#include "compute_service/binder/bound_type.hpp"
#include "compute_service/executor/row.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace eugraph {
namespace binder {

/// Bound literal with resolved type.
struct BoundLiteral {
    Value value; // runtime literal value (already stored as typed Value)
    BoundType type;

    BoundLiteral() : value(), type(BoundType::Null()) {}
    explicit BoundLiteral(bool v) : value(v), type(BoundType::Bool()) {}
    explicit BoundLiteral(int64_t v) : value(v), type(BoundType::Int64()) {}
    explicit BoundLiteral(double v) : value(v), type(BoundType::Double()) {}
    explicit BoundLiteral(const std::string& v) : value(v), type(BoundType::String()) {}
};

} // namespace binder
} // namespace eugraph
