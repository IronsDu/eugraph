#pragma once

#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace eugraph {
namespace optimizer {

/// Plan-wide reference/guard facts over a bound logical plan: whole-tree
/// reference counts, correlated-variable provenance and write-operator
/// presence. Consumed by pre-CBO normalization and by lowering guards; nothing
/// here decides a plan.
///
/// `complete` is false when the walk meets an operator whose expression fields
/// it does not enumerate (write ops, CALL, ...); a caller that must not miss a
/// consumer then refuses to proceed.
struct PlanRefIndex {
    std::unordered_map<std::string, int> expr_refs;
    /// References that are a direct `size(<ref>)` argument: they observe only
    /// the list length, never an element.
    std::unordered_map<std::string, int> size_arg_refs;
    std::unordered_set<std::string> path_elements;
    bool complete = true;
};

PlanRefIndex collectPlanRefs(const binder::BoundLogicalOperator& root);

/// Whether `name` is provided by a BoundCorrelatedSourceOp in the subtree.
bool subtreeProvidesCorrelatedVariable(const binder::BoundLogicalOperator& root, const std::string& name);

/// A single bare reference (already-lowered column or variable), or nullptr.
const binder::BoundColumnRef* exprRef(const binder::BoundExpression& expr);
const binder::BoundVariableRef* exprVarRef(const binder::BoundExpression& expr);
/// Name of a bare reference, or empty when the expression is not one.
std::string bareRefName(const binder::BoundExpression& expr);

/// Whether the plan contains a write operator (Set/Remove/Delete/Merge/
/// CreateNode/CreateEdge), including inside a FOREACH body.
bool planHasWriteOperator(const binder::BoundLogicalOperator& root);

} // namespace optimizer
} // namespace eugraph
