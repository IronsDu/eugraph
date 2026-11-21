#pragma once
#include "query/planner/bound_logical_plan_fwd.hpp"
namespace eugraph {
namespace optimizer {
/// 从 Filter(CrossProduct) 中提取等值 join 条件到 Join 的**逻辑**字段。
/// 注意：Filter 本身保留原谓词（用于语义兜底和残差），这里只是给 CBO 提供
/// “这是 equi-join”的语义信息；不选择任何物理算法。
void extractJoinConditions(binder::BoundLogicalOperator& root);
} // namespace optimizer
} // namespace eugraph
