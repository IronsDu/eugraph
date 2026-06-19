#pragma once

#include "query/catalog/catalog.hpp"
#include "query/optimizer/memo.hpp"
#include "query/optimizer/opt_rule.hpp"
#include "query/optimizer/task.hpp"

#include "query/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace optimizer {

class LogicalOptimizer {
public:
    /// Optimize the logical plan. The catalog provides cardinality statistics
    /// for LogProp derivation; nullptr uses default estimates.
    void optimize(binder::BoundLogicalPlan& plan, const catalog::Catalog* catalog = nullptr);

private:
    Memo memo_;
    RuleSet rules_;
    void initRules();
};

} // namespace optimizer
} // namespace eugraph
