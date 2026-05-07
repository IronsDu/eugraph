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
    bool matches(const std::vector<binder::BoundType>& call_arg_types) const {
        return matchCost(call_arg_types) >= 0;
    }

    /// Compute the total implicit cast cost for matching this overload.
    /// Returns -1 if not matched, 0 for exact match, higher values for worse matches.
    /// Used by FunctionRegistry to select the best overload.
    int matchCost(const std::vector<binder::BoundType>& call_arg_types) const {
        if (has_variadic_args)
            return 0; // variadic matches everything at zero cost
        if (arg_types.empty())
            return 0; // untyped overload accepts anything
        if (arg_types.size() != call_arg_types.size())
            return -1;
        int total_cost = 0;
        for (size_t i = 0; i < arg_types.size(); ++i) {
            int cost = call_arg_types[i].implicitCastCost(arg_types[i]);
            if (cost < 0)
                return -1;
            total_cost += cost;
        }
        return total_cost;
    }
};

} // namespace function
} // namespace eugraph
