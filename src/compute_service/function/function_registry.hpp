#pragma once

#include "compute_service/function/function_def.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace function {

/// Registry for all built-in and user-defined functions.
///
/// Supports overloaded functions (same name, different arg types).
/// Lookup by name + argument types returns the best-matching overload.
class FunctionRegistry {
public:
    FunctionRegistry();

    /// Register all built-in functions.
    void registerBuiltins();

    /// Register a function definition.
    void registerFunction(FunctionDef def);

    /// Find the best-matching overload for a function call.
    /// Returns nullptr if no matching overload exists.
    const FunctionDef* lookup(const std::string& name, const std::vector<binder::BoundType>& arg_types) const;

    /// Look up all overloads of a function by name.
    /// Returns empty vector if the function is not registered.
    const std::vector<FunctionDef>* lookupAll(const std::string& name) const;

    /// Check whether a function with the given name exists.
    bool exists(const std::string& name) const;

private:
    // name -> list of overloads
    std::unordered_map<std::string, std::vector<FunctionDef>> functions_;

    void registerScalarBuiltins();
    void registerAggregateBuiltins();
};

} // namespace function
} // namespace eugraph
