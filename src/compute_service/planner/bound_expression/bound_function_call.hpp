#pragma once

#include "compute_service/function/function_def.hpp"
#include "compute_service/planner/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/planner/bound_type.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace binder {

/// Bound function call with resolved function definition and typed arguments.
struct BoundFunctionCall {
    const function::FunctionDef* func_def = nullptr;
    std::vector<BoundExpression> args;
    BoundType return_type;
    bool distinct = false;

    BoundFunctionCall() : return_type(BoundType::Any()) {}

    bool isAggregate() const {
        return func_def && func_def->is_aggregate;
    }
};

} // namespace binder
} // namespace eugraph
