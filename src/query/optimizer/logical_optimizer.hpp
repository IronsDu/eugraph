#pragma once

#include "query/optimizer/memo.hpp"
#include "query/optimizer/opt_rule.hpp"
#include "query/optimizer/task.hpp"

#include "query/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace optimizer {

class LogicalOptimizer {
public:
    void optimize(binder::BoundLogicalPlan& plan);

private:
    Memo memo_;
    RuleSet rules_;
    void initRules();
};

} // namespace optimizer
} // namespace eugraph
