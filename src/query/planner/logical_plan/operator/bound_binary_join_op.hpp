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
    /// 语义标注（optimizer normalization）：直接覆在 join 上的谓词把右侧
    /// label-scan 顶点限制在该边变量的两个端点。非空时 `CorrelatedVertexProbe`
    /// 才是合法候选；lowering 按这个变量名解析边列（不做谓词反解）。
    std::string endpoint_probe_edge_var;
    BoundLogicalOperator left;
    BoundLogicalOperator right;
};

} // namespace binder
} // namespace eugraph
