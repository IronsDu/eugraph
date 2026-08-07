#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundCallOp {
    std::string procedure_name;
    std::vector<BoundExpression> arguments;
    std::vector<std::string> yield_items;
    std::vector<std::string> output_names;
    std::vector<BoundType> output_types;
};

} // namespace binder
} // namespace eugraph
