#pragma once

#include "compute_service/binder/bind_context.hpp"
#include "compute_service/binder/bound_expression/bound_expression.hpp"
#include "compute_service/binder/bound_logical_plan.hpp"
#include "compute_service/catalog/catalog.hpp"
#include "compute_service/function/function_registry.hpp"
#include "compute_service/parser/ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

/// Semantic analyzer: binds AST → BoundStatement with type resolution.
///
/// Responsibilities:
/// 1. Resolve all symbol references (variables, labels, properties, functions)
/// 2. Infer result types for every expression
/// 3. Type-check operations and report errors
/// 4. Collect property requirements for projection pushdown
/// 5. Produce a BoundLogicalPlan with typed output schema
class Binder {
public:
    Binder(const catalog::Catalog& catalog, const function::FunctionRegistry& func_registry)
        : catalog_(catalog), func_registry_(func_registry) {}

    /// Bind a top-level Cypher statement. Returns errors on failure.
    std::optional<BoundStatement> bind(const cypher::Statement& stmt);

    /// Accumulated binding errors.
    const std::vector<std::string>& errors() const {
        return errors_;
    }

    /// Bind a single expression to a BoundExpression.
    /// Requires the BindContext to be populated with the input schema columns
    /// (via registerColumn) before calling.
    std::optional<BoundExpression> bindExpression(const cypher::Expression& expr);

    /// Register an input schema column for expression variable resolution.
    void registerColumn(const std::string& name, BoundType type);

private:
    const catalog::Catalog& catalog_;
    const function::FunctionRegistry& func_registry_;
    BindContext ctx_;
    std::vector<std::string> errors_;

    void error(const std::string& msg) {
        errors_.push_back(msg);
    }

    // ── Statement ──
    bool bindRegularQuery(const cypher::RegularQuery& query, BoundStatement& result);
    bool bindSingleQuery(const cypher::SingleQuery& query, BoundLogicalPlan& plan);

    // ── Clause binding ──
    std::optional<BoundLogicalOperator> bindMatch(const cypher::MatchClause& match);
    std::optional<BoundLogicalOperator> bindReturn(const cypher::ReturnClause& ret, BoundLogicalOperator child);
    std::optional<BoundLogicalOperator>
    bindWhere(const cypher::Expression& pred, BoundLogicalOperator child,
              std::optional<BoundLogicalOperator> extra_filter_child = std::nullopt);
    std::optional<BoundLogicalOperator> bindWith(const cypher::WithClause& with_clause, BoundLogicalOperator child);
    std::optional<BoundLogicalOperator> bindCreate(const cypher::CreateClause& create,
                                                   std::optional<BoundLogicalOperator> child);
    std::optional<BoundLogicalOperator> bindSet(const cypher::SetClause& set, BoundLogicalOperator child);
    std::optional<BoundLogicalOperator> bindRemove(const cypher::RemoveClause& rem, BoundLogicalOperator child);

    // ── Expression binding helpers ──
    BoundType inferBinaryOpType(cypher::BinaryOperator op, const BoundType& left_type, const BoundType& right_type,
                                std::string& error_msg);
    BoundType inferUnaryOpType(cypher::UnaryOperator op, const BoundType& operand_type, std::string& error_msg);

    // ── Pattern binding ──
    bool bindNodePattern(const cypher::NodePattern& node, std::string& var_name, uint32_t& col_idx,
                         std::optional<LabelId>& label_id, std::vector<uint16_t>& default_prop_ids);
    bool bindRelationshipPattern(const cypher::RelationshipPattern& rel, std::string& var_name, uint32_t& col_idx,
                                 std::vector<EdgeLabelId>& edge_label_ids, std::vector<uint16_t>& default_prop_ids);

    // ── Helpers ──
    uint32_t nextColumnIndex() {
        return ctx_.next_column_index++;
    }
    BoundType propertyTypeToBoundType(PropertyType pt);
    ColumnInfo makeColumnInfo(const std::string& name, BoundType type,
                              std::optional<LabelId> source_label = std::nullopt,
                              std::optional<uint16_t> source_prop_id = std::nullopt, bool strong_typed = false);
};

} // namespace binder
} // namespace eugraph
