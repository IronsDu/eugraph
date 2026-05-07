#pragma once

#include "compute_service/binder/bind_context.hpp"
#include "compute_service/binder/bound_expression/bound_expression.hpp"
#include "compute_service/binder/bound_type.hpp"
#include "compute_service/catalog/catalog.hpp"
#include "compute_service/function/function_registry.hpp"
#include "compute_service/logical_plan/logical_plan.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace binder {

/// Result of binding a logical plan's expressions.
struct BoundPlanResult {
    /// Map from logical operator address to its bound expressions.
    /// PhysicalPlanner looks up bound expressions here instead of using raw AST.
    std::unordered_map<const void*, BoundExpression> bound_predicates;               // FilterOp predicates
    std::unordered_map<const void*, std::vector<BoundExpression>> bound_projections; // ProjectOp items
    std::unordered_map<const void*, std::vector<BoundExpression>> bound_sort_keys;   // SortOp keys
    std::unordered_map<const void*, BoundExpression> bound_properties;               // Create/Set property values

    /// Property requirements collected during binding (for projection pushdown).
    std::vector<PropertyRequirement> property_requirements;

    /// Errors encountered during binding.
    std::vector<std::string> errors;

    /// The bind context used (symbol table, etc.).
    BindContext context;
};

/// Post-processing pass: binds all expressions in a LogicalPlan.
///
/// Uses Catalog and FunctionRegistry to resolve symbols, infer types,
/// and detect errors before physical planning.
class PlanBinder {
public:
    PlanBinder(const catalog::Catalog& catalog, const function::FunctionRegistry& func_registry)
        : catalog_(catalog), func_registry_(func_registry) {}

    /// Bind all expressions in a logical plan.
    BoundPlanResult bind(const compute::LogicalPlan& plan);

    const std::vector<std::string>& errors() const {
        return errors_;
    }

    /// Register an input column for expression variable resolution.
    void registerColumn(const std::string& name, BoundType type);

    /// Bind a single AST expression. Requires registerColumn to have been called first.
    std::optional<BoundExpression> bindExpression(const cypher::Expression& expr);

private:
    const catalog::Catalog& catalog_;
    const function::FunctionRegistry& func_registry_;
    std::vector<std::string> errors_;
    BindContext ctx_;

    void error(const std::string& msg) {
        errors_.push_back(msg);
    }

    /// Walk the logical operator tree and bind expressions.
    void walkAndBind(const compute::LogicalOperator& op, BoundPlanResult& result);

    // ── Type inference ──
    BoundType inferBinaryOpType(cypher::BinaryOperator op, const BoundType& left, const BoundType& right,
                                std::string& err);
    BoundType inferUnaryOpType(cypher::UnaryOperator op, const BoundType& operand, std::string& err);
    BoundType propertyTypeToBoundType(PropertyType pt);

    // ── Symbol table management ──
    void registerVariable(const std::string& name, BoundType type, uint32_t col_idx);
    const ColumnInfo* lookupVariable(const std::string& name) const;
};

} // namespace binder
} // namespace eugraph
