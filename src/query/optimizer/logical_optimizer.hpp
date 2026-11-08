#pragma once

#include "query/catalog/catalog.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/optimizer/framework/opt_rule.hpp"
#include "query/optimizer/framework/task.hpp"

#include "query/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace optimizer {

class LogicalOptimizer {
public:
    /// Register an extension rule. Built-in rules are registered first; custom
    /// rules are appended when this optimizer instance runs. One optimizer is
    /// created per query, so custom rules are consumed once.
    void addRule(std::unique_ptr<OptRule> rule) {
        custom_rules_.push_back(std::move(rule));
    }

    /// Optimize the logical plan. The catalog provides cardinality statistics
    /// for LogProp derivation; nullptr uses default estimates.
    void optimize(binder::BoundLogicalPlan& plan, const catalog::Catalog* catalog = nullptr,
                  const std::unordered_map<LabelId, uint64_t>* label_vertex_counts = nullptr);

private:
    Memo memo_;
    RuleSet rules_;
    std::vector<std::unique_ptr<OptRule>> custom_rules_;
    void initRules();
};

} // namespace optimizer
} // namespace eugraph
