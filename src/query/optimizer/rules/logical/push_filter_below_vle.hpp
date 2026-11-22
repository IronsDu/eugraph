#pragma once
#include "query/planner/bound_logical_plan_fwd.hpp"
namespace eugraph {
namespace optimizer {
/// 把只引用 VLE src 的 Filter 移到 VLE 下方，避免先展开再过滤。
/// 不改变 VLE 方向/hop/edge variable；无法证明 src-only 时不动。
void pushFilterBelowVle(binder::BoundLogicalOperator& root);
} // namespace optimizer
} // namespace eugraph
