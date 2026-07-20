#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"
#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {

// Forward declarations for evaluator store access (lazy property loading).
class IAsyncGraphDataStore;
class IAsyncGraphMetaStore;

namespace catalog {
class Catalog;
}
struct Column;
namespace function {

/// Base class for aggregate function state (e.g. CountState, SumState, etc.).
struct AggStateBase {
    virtual ~AggStateBase() = default;
};

/// Execution context passed to function callbacks at evaluation time.
/// Provides access to catalog metadata (e.g. for type() to resolve edge label names).
/// Extend with additional fields as needed by future functions.
struct EvalContext {
    const catalog::Catalog* catalog = nullptr;
    /// Live label definitions, updated by DDL operators mid-query.
    const std::unordered_map<LabelId, LabelDef>* label_defs = nullptr;
    /// Live edge label definitions, updated by DDL operators mid-query.
    const std::unordered_map<EdgeLabelId, EdgeLabelDef>* edge_label_defs = nullptr;
    /// Data store for lazy property loading (e.g. startNode result vertices).
    class IAsyncGraphDataStore* store = nullptr;
    /// Meta store for label lookups during lazy loading.
    class IAsyncGraphMetaStore* meta = nullptr;
};

/// Scalar function execution callback: takes argument values and context, returns result.
using ScalarFn = std::function<Value(const std::vector<Value>&, const EvalContext&)>;

/// Batch scalar function: processes all rows at once.
using BatchScalarFn =
    std::function<void(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&)>;

/// Aggregate state factory.
using AggInitFn = std::function<std::unique_ptr<AggStateBase>()>;

/// Accumulate one value into aggregate state.
using AggUpdateFn = std::function<void(AggStateBase&, const Value&)>;

/// Produce final aggregate result from state.
using AggFinalizeFn = std::function<Value(const AggStateBase&)>;

/// Describes a registered function (scalar or aggregate).
struct FunctionDef {
    std::string name;
    std::vector<binder::BoundType> arg_types; // empty = variadic / any-type args
    binder::BoundType return_type;
    bool is_aggregate = false;
    bool has_variadic_args = false; // e.g. coalesce(...)

    // Scalar execution callback (non-null for scalar functions).
    ScalarFn scalar_fn;

    // Batch scalar execution callback (processes all rows at once).
    BatchScalarFn batch_scalar_fn;

    // Aggregate callbacks (non-null for aggregate functions).
    AggInitFn agg_init;
    AggUpdateFn agg_update;
    AggFinalizeFn agg_finalize;

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
