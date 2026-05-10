#pragma once

#include "compute_service/optimizer/memo.hpp"
#include "compute_service/optimizer/opt_rule.hpp"
#include "compute_service/optimizer/task.hpp"

#include "compute_service/planner/bound_logical_plan_fwd.hpp"

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
