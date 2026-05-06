#pragma once

#include "compute_service/binder/bound_type.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace function {

/// Describes a registered function (scalar or aggregate).
struct FunctionDef {
    std::string name;
    std::vector<binder::BoundType> arg_types; // empty = variadic / any-type args
    binder::BoundType return_type;
    bool is_aggregate = false;
    bool has_variadic_args = false; // e.g. coalesce(...)

    /// Unique key for overload resolution: name + arg_types
    std::string signatureKey() const {
        std::string key = name + "(";
        for (size_t i = 0; i < arg_types.size(); ++i) {
            if (i > 0)
                key += ", ";
            key += arg_types[i].toString();
        }
        key += ")";
        return key;
    }

    /// Check if this overload matches the given argument types.
    /// Returns true if exact match, or if variadic with compatible types.
    bool matches(const std::vector<binder::BoundType>& call_arg_types) const {
        if (has_variadic_args)
            return true;
        if (arg_types.empty())
            return true; // untyped overload accepts anything
        if (arg_types.size() != call_arg_types.size())
            return false;
        for (size_t i = 0; i < arg_types.size(); ++i) {
            if (!call_arg_types[i].canCastTo(arg_types[i]) && !arg_types[i].canCastTo(call_arg_types[i]))
                return false;
        }
        return true;
    }
};

} // namespace function
} // namespace eugraph
