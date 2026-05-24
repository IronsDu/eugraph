#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
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
        TargetKind kind;
        std::string variable_name;
    };
    bool detach = false;
    std::vector<DeleteTarget> targets;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
