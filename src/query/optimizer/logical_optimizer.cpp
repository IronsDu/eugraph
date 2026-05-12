#include "query/optimizer/logical_optimizer.hpp"

#include "query/optimizer/rules/filter_pushdown.hpp"

#include "query/planner/bound_logical_plan.hpp"

namespace eugraph {
namespace optimizer {

void LogicalOptimizer::optimize(binder::BoundLogicalPlan& plan) {
    initRules();

    auto root_gid = memo_.copyIn(plan.root);

    TaskQueue queue;
    queue.push(std::make_unique<OGroupTask>(root_gid));

    while (!queue.empty()) {
        auto task = queue.pop();
        task->perform(memo_, rules_, queue);
    }

    plan.root = memo_.copyOut(root_gid);
}

void LogicalOptimizer::initRules() {
    rules_.addRule(std::make_unique<FilterPushdownRule>());
}

} // namespace optimizer
} // namespace eugraph
