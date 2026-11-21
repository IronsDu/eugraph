#pragma once
#include "query/planner/bound_logical_plan_fwd.hpp"
namespace eugraph {
namespace optimizer {
/// 安全版 Filter(Project) 下推：只有 Filter 引用的名字都能在 Project item 中
/// 找到“简单列/变量”别名时，才把谓词替换后推到 Project 下方。
void pushFilterThroughProject(binder::BoundLogicalOperator& root);
} // namespace optimizer
} // namespace eugraph
