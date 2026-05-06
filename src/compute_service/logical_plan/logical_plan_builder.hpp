#pragma once

#include "compute_service/logical_plan/logical_plan.hpp"
#include "compute_service/parser/ast.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace eugraph {
namespace compute {

/// Symbol table: maps variable names to column indices in the current row schema.
using SymbolTable = std::unordered_map<std::string, uint32_t>;

/// Result of building a PatternElement: the root operator plus the ordered list of
/// element variables that form the path (v1, e1, v2, e2, …).
struct PatternElementResult {
    LogicalOperator root;
    std::vector<std::string> path_elements;
};

/// Builds a LogicalPlan from a Cypher AST (Statement).
class LogicalPlanBuilder {
public:
    /// Build a logical plan from a parsed Statement (takes ownership).
    /// Returns the plan on success, or an error string on failure.
    std::variant<LogicalPlan, std::string> build(cypher::Statement stmt);

private:
    // Symbol table tracks variable → column index mappings
    SymbolTable symbols_;
    uint32_t next_column_ = 0;

    // Helper: ensure a variable has a column index, allocate if new.
    uint32_t ensureVariable(const std::string& name);

    // Build operators for a SingleQuery.
    std::variant<LogicalOperator, std::string> buildSingleQuery(cypher::SingleQuery& query);

    // Build operators for a MATCH clause.
    std::variant<LogicalOperator, std::string> buildMatch(cypher::MatchClause& match);

    // Build a scan+expand chain from a PatternElement.
    std::variant<PatternElementResult, std::string> buildPatternElement(cypher::PatternElement& elem);

    // Build operators for a RETURN clause.
    std::variant<LogicalOperator, std::string> buildReturn(cypher::ReturnClause& ret, LogicalOperator input);

    // Build operators for a CREATE clause.
    std::variant<LogicalOperator, std::string> buildCreate(cypher::CreateClause& create);

    // Build operators for SET / REMOVE clauses (wrap input pipeline).
    std::variant<LogicalOperator, std::string> buildSet(cypher::SetClause& set, LogicalOperator input);
    std::variant<LogicalOperator, std::string> buildRemove(cypher::RemoveClause& rem, LogicalOperator input);

    // Build operators for a WHERE predicate (wraps input in FilterOp).
    std::variant<LogicalOperator, std::string> buildFilter(cypher::Expression predicate, LogicalOperator input);

    // Build a filter expression from a PropertiesMap (conjunction of var.key = value).
    cypher::Expression buildPropertiesFilter(const std::string& varName, cypher::PropertiesMap& properties);

    // Get the last (topmost) operator in a chain (the rightmost child).
    static LogicalOperator& topOperator(LogicalOperator& op);

    // Counter for auto-generated anonymous variable names.
    uint32_t anon_counter_ = 0;
};

} // namespace compute
} // namespace eugraph
