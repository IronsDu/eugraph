#pragma once

#include "query/catalog/catalog.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_registry.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bind_context.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <optional>
#include <string>
#include <unordered_map>
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
    Binder(const catalog::Catalog& catalog, const function::FunctionRegistry& func_registry,
           const std::unordered_map<std::string, Value>& params = {})
        : catalog_(catalog), func_registry_(func_registry), params_(params) {}

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
    const std::unordered_map<std::string, Value>& params_;
    BindContext ctx_;
    std::vector<std::string> errors_;
    uint32_t anon_id_ = 0;

    void error(const std::string& msg) {
        errors_.push_back(msg);
    }

    // ── Statement ──
    bool bindRegularQuery(const cypher::RegularQuery& query, BoundStatement& result);
    bool bindSingleQuery(const cypher::SingleQuery& query, BoundLogicalPlan& plan);

    // ── Clause binding ──
    std::optional<BoundLogicalOperator> bindMatch(const cypher::MatchClause& match,
                                                  std::optional<BoundLogicalOperator> parent = std::nullopt,
                                                  bool skip_where = false);
    std::optional<BoundLogicalOperator> bindReturn(const cypher::ReturnClause& ret, BoundLogicalOperator child);
    std::optional<BoundLogicalOperator>
    bindWhere(const cypher::Expression& pred, BoundLogicalOperator child,
              std::optional<BoundLogicalOperator> extra_filter_child = std::nullopt);
    std::optional<BoundLogicalOperator> bindWith(const cypher::WithClause& with_clause, BoundLogicalOperator child);
    std::optional<BoundLogicalOperator> bindCreate(const cypher::CreateClause& create,
                                                   std::optional<BoundLogicalOperator> child);
    std::optional<BoundLogicalOperator> bindSet(const cypher::SetClause& set, BoundLogicalOperator child);
    std::optional<BoundLogicalOperator> bindRemove(const cypher::RemoveClause& rem, BoundLogicalOperator child);
    std::optional<BoundLogicalOperator> bindDelete(const cypher::DeleteClause& del, BoundLogicalOperator child);
    std::optional<BoundLogicalOperator> bindUnwind(const cypher::UnwindClause& unwind,
                                                   std::optional<BoundLogicalOperator> child);

    // ── Expression binding helpers ──
    BoundType inferBinaryOpType(cypher::BinaryOperator op, const BoundType& left_type, const BoundType& right_type,
                                std::string& error_msg);
    BoundType inferUnaryOpType(cypher::UnaryOperator op, const BoundType& operand_type, std::string& error_msg);

    // ── Pattern binding ──
    bool bindNodePattern(const cypher::NodePattern& node, std::string& var_name, uint32_t& col_idx,
                         std::optional<LabelId>& label_id, std::vector<uint16_t>& default_prop_ids,
                         bool skip_register = false);
    bool bindRelationshipPattern(const cypher::RelationshipPattern& rel, std::string& var_name, uint32_t& col_idx,
                                 std::vector<EdgeLabelId>& edge_label_ids, std::vector<uint16_t>& default_prop_ids,
                                 bool for_create = false);

    // ── OPTIONAL MATCH binding ──
    std::optional<BoundLogicalOperator> bindOptionalMatch(const cypher::MatchClause& match,
                                                          BoundLogicalOperator current);

    // ── EXISTS subquery binding ──
    /// Bind an EXISTS pattern as a SemiJoin operator. Returns the SemiJoin wrapping the given child.
    std::optional<BoundLogicalOperator> bindExistsAsSemiJoin(const cypher::ExistsExpr& exists,
                                                             BoundLogicalOperator child, bool anti = false);
    /// Bind the pattern inside an EXISTS subquery, producing a logical sub-plan.
    /// The sub-plan uses an independent column space; correlated variables are registered
    /// as columns 0, 1, ... in a BoundCorrelatedSourceOp leaf.
    std::optional<BoundLogicalOperator> bindExistsSubPlan(const cypher::ExistsExpr& exists,
                                                          std::vector<std::pair<uint32_t, uint32_t>>& correlation);
    /// Collect EXISTS expressions from the top-level AND chain of a WHERE predicate.
    /// Each entry is (ExistsExpr pointer, is_anti). is_anti=true means the EXISTS
    /// was wrapped in a NOT, i.e. NOT EXISTS { ... }.
    static void collectExistsFromAnd(const cypher::Expression& expr,
                                     std::vector<std::pair<const cypher::ExistsExpr*, bool>>& out);
    /// Remove EXISTS expressions (and their wrapping NOT, if any) from the top-level
    /// AND chain. Returns nullopt if the entire expression was EXISTS-related.
    static std::optional<cypher::Expression> removeExistsFromWhere(const cypher::Expression& expr);

    // ── Helpers ──
    uint32_t nextColumnIndex() {
        return ctx_.next_column_index++;
    }
    uint32_t nextAnonId() {
        return anon_id_++;
    }
    BoundType propertyTypeToBoundType(PropertyType pt);
    ColumnInfo makeColumnInfo(const std::string& name, BoundType type,
                              std::optional<LabelId> source_label = std::nullopt,
                              std::optional<uint16_t> source_prop_id = std::nullopt, bool strong_typed = false);

    // ── Projection pushdown ──
    void addAllPropertiesForVariable(const std::string& var_name);
    void applyProjectionPushdown(BoundLogicalOperator& op);
    void collectLabelPropIds(const std::string& var_name, std::unordered_map<LabelId, std::vector<uint16_t>>& out);
};

} // namespace binder
} // namespace eugraph
