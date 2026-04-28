#pragma once

#include "compute_service/parser/ast.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace eugraph {
namespace compute {

// ==================== Forward declarations ====================

struct AllNodeScanOp;
struct LabelScanOp;
struct ExpandOp;
struct FilterOp;
struct ProjectOp;
struct AggregateOp;
struct SortOp;
struct SkipOp;
struct DistinctOp;
struct LimitOp;
struct CreateNodeOp;
struct CreateEdgeOp;

// ==================== Logical Operator ====================

using LogicalOperator =
    std::variant<std::unique_ptr<AllNodeScanOp>, std::unique_ptr<LabelScanOp>, std::unique_ptr<ExpandOp>,
                 std::unique_ptr<FilterOp>, std::unique_ptr<ProjectOp>, std::unique_ptr<AggregateOp>,
                 std::unique_ptr<SortOp>, std::unique_ptr<SkipOp>, std::unique_ptr<DistinctOp>,
                 std::unique_ptr<LimitOp>, std::unique_ptr<CreateNodeOp>, std::unique_ptr<CreateEdgeOp>>;

// ==================== Scan Operators ====================

/// Scan all vertices (no label filter).
struct AllNodeScanOp {
    std::string variable; // variable name bound to each vertex
    std::vector<LogicalOperator> children;
};

/// Scan vertices by label.
struct LabelScanOp {
    std::string variable;
    std::string label; // label name (string, resolved to ID later)
    std::vector<LogicalOperator> children;
};

// ==================== Expand ====================

/// Expand from a vertex variable to find neighbors via edges.
struct ExpandOp {
    std::string src_variable;           // source vertex variable
    std::string dst_variable;           // destination vertex variable (new binding)
    std::string edge_variable;          // optional edge variable
    std::vector<std::string> rel_types; // relationship type names
    cypher::RelationshipDirection direction;
    std::optional<std::pair<cypher::Expression, cypher::Expression>> range; // *min..max
    std::vector<LogicalOperator> children;
};

// ==================== Filter ====================

/// Filter rows by predicate.
struct FilterOp {
    cypher::Expression predicate;
    std::vector<LogicalOperator> children;
};

// ==================== Project ====================

/// Project / re-map columns.
struct ProjectOp {
    struct ProjectItem {
        cypher::Expression expr;
        std::optional<std::string> alias;
    };
    std::vector<ProjectItem> items;
    bool distinct = false;
    std::vector<LogicalOperator> children;
};

// ==================== Aggregate ====================

struct AggregateFunc {
    std::string func_name; // "count", "sum", "avg", "min", "max"
    cypher::Expression arg;
    bool distinct = false;
};

struct AggregateOp {
    std::vector<cypher::Expression> group_keys;
    std::vector<AggregateFunc> aggregates;
    std::vector<std::string> output_names;
    std::vector<LogicalOperator> children;
};

// ==================== Sort ====================

struct SortOp {
    std::vector<cypher::OrderBy::SortItem> sort_items;
    std::vector<LogicalOperator> children;
};

// ==================== Skip ====================

struct SkipOp {
    int64_t skip = 0;
    std::vector<LogicalOperator> children;
};

// ==================== Distinct ====================

struct DistinctOp {
    std::vector<LogicalOperator> children;
};

// ==================== Limit ====================

struct LimitOp {
    int64_t limit = 0;
    std::vector<LogicalOperator> children;
};

// ==================== Create Operators ====================

/// Create a node (vertex).
struct CreateNodeOp {
    std::string variable;                            // variable name
    std::vector<std::string> labels;                 // label names
    std::optional<cypher::PropertiesMap> properties; // initial properties
    std::vector<LogicalOperator> children;
};

/// Create an edge (relationship).
struct CreateEdgeOp {
    std::string variable;
    std::string src_variable;
    std::string dst_variable;
    std::vector<std::string> rel_types;
    cypher::RelationshipDirection direction;
    std::optional<cypher::PropertiesMap> properties;
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
