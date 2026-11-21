#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/logical_plan/operator/join_type.hpp"

namespace eugraph {
namespace binder {

struct BoundBinaryJoinOp {
    JoinType join_type;
    /// 逻辑连接条件（算法无关）；物理层选择 HashJoin/IndexJoin/NestedLoop。
    std::vector<BoundExpression> join_conditions;
    BoundLogicalOperator left;
    BoundLogicalOperator right;
};

} // namespace binder
} // namespace eugraph
