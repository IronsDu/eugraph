#pragma once

#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace eugraph {
namespace optimizer {

/// Whole-plan reference index over a bound logical plan.
///
/// `complete` is false when the walk meets an operator whose expression fields
/// it does not enumerate (write ops, CALL, ...); callers that must not miss a
/// consumer (e.g. renaming an operator field) then refuse to proceed.
struct PlanRefIndex {
    std::unordered_map<std::string, int> expr_refs;
    /// References that are a direct `size(<ref>)` argument: they observe only
    /// the list length, never an element.
    std::unordered_map<std::string, int> size_arg_refs;
    std::unordered_set<std::string> path_elements;
    bool complete = true;
};

PlanRefIndex collectPlanRefs(const binder::BoundLogicalOperator& root);

/// Count variable names referenced by operator *fields* (Expand src/dst/edge,
/// VarLen endpoints, Unwind variable, path elements, correlated columns). These
/// are not expressions, so they do not appear in PlanRefIndex::expr_refs.
void countPlanFieldRefs(const binder::BoundLogicalOperator& root, std::unordered_map<std::string, int>& refs);

/// Whether `name` is provided by a BoundCorrelatedSourceOp in the subtree.
bool subtreeProvidesCorrelatedVariable(const binder::BoundLogicalOperator& root, const std::string& name);

/// `id(<ref>)` argument matcher for binder-generated endpoint equality
/// predicates. Generated predicates use VERTEX_REF columns on purpose (see
/// bindExistsSubPlan's SavedCorr); a user-written id() comparison compares
/// VERTEX columns and must not match here.
const binder::BoundColumnRef* vertexRefIdArgument(const binder::BoundExpression& expr);

/// A single bare reference (already-lowered column or variable), or nullptr.
const binder::BoundColumnRef* exprRef(const binder::BoundExpression& expr);
const binder::BoundVariableRef* exprVarRef(const binder::BoundExpression& expr);
/// Name of a bare reference, or empty when the expression is not one.
std::string bareRefName(const binder::BoundExpression& expr);

/// Whether a bound expression references `name` (bare ref or property object).
bool expressionReferences(const binder::BoundExpression& expr, const std::string& name);

/// Whether the plan contains a write operator (Set/Remove/Delete/Merge/
/// CreateNode/CreateEdge), including inside a FOREACH body.
bool planHasWriteOperator(const binder::BoundLogicalOperator& root);

} // namespace optimizer
} // namespace eugraph
