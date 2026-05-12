#pragma once

#include "query/planner/bind_context.hpp"
#include "query/planner/bound_logical_plan.hpp"

namespace eugraph {
namespace binder {

/// Resolves BoundVariableRef → BoundColumnRef in a BoundLogicalPlan.
///
/// After the Binder produces a logical plan with symbolic variable references,
/// ColumnResolver walks the entire plan tree and replaces every
/// BoundVariableRef with a BoundColumnRef based on the column layout in
/// BindContext. This is run before PhysicalPlanner::planBound().
///
/// Future: the Optimizer sits between Binder and ColumnResolver, so that
/// column indices are finalized only after optimization is complete.
class ColumnResolver {
public:
    /// Resolve all BoundVariableRefs in the logical plan.
    /// On success returns true. On error, adds messages to `errors` and returns false.
    bool resolve(BoundLogicalPlan& plan, const BindContext& ctx, std::vector<std::string>& errors);

private:
    bool resolveOperator(BoundLogicalOperator& op, const BindContext& ctx, std::vector<std::string>& errors);
    bool resolveExpression(BoundExpression& expr, const BindContext& ctx, std::vector<std::string>& errors);
};

} // namespace binder
} // namespace eugraph
