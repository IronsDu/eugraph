#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundDeleteOp {
    enum class TargetKind {
        VERTEX,
        EDGE
    };
    struct DeleteTarget {
        /// For simple variable targets (`DELETE n`): the static kind and the
        /// variable name, resolved by the physical planner to the constructed
        /// object column.
        std::optional<TargetKind> kind;
        std::string variable_name;
        /// For arbitrary entity expressions (`DELETE list[$i]`,
        /// `DELETE map.key`, path variables): the bound expression evaluated
        /// per row to a node/edge/path/list/map.
        std::optional<BoundExpression> expr;
    };
    bool detach = false;
    std::vector<DeleteTarget> targets;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
