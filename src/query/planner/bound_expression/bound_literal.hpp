#pragma once

#include "query/dataset/row.hpp"
#include "query/planner/bound_type.hpp"

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
    explicit BoundLiteral(const Value& v) : value(v), type(inferType(v)) {}

private:
    static BoundType inferType(const Value& v) {
        if (std::holds_alternative<bool>(v))
            return BoundType::Bool();
        if (std::holds_alternative<int64_t>(v))
            return BoundType::Int64();
        if (std::holds_alternative<double>(v))
            return BoundType::Double();
        if (std::holds_alternative<std::string>(v))
            return BoundType::String();
        if (std::holds_alternative<ListValue>(v))
            return BoundType::List(BoundType::Any());
        if (std::holds_alternative<MapValue>(v))
            return BoundType::Map(BoundType::String(), BoundType::Any());
        return BoundType::Null();
    }

public:
};

} // namespace binder
} // namespace eugraph
