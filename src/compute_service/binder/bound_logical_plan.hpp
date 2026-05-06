#pragma once

#include "compute_service/binder/bound_expression/bound_expression.hpp"
#include "compute_service/binder/bound_logical_plan_fwd.hpp"
#include "compute_service/binder/bound_type.hpp"
#include "compute_service/parser/ast.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

// ==================== Bound Logical Operators ====================

/// Scan all vertices (no label filter).
struct BoundScanOp {
    std::string variable;           // bound variable name
    uint32_t column_index;          // column index in output
    std::vector<uint16_t> prop_ids; // properties to fetch (empty = fetch all)
};

/// Scan vertices of a specific label.
struct BoundLabelScanOp {
    std::string variable;
    uint32_t column_index;
    LabelId label_id;
    std::string label_name;
    std::vector<uint16_t> prop_ids; // properties to fetch
};

/// Expand from source vertex to neighbors.
struct BoundExpandOp {
    std::string src_variable;
    uint32_t src_column_index;
    std::string edge_variable;
    uint32_t edge_column_index;
    std::string dst_variable;
    uint32_t dst_column_index;
    std::vector<EdgeLabelId> edge_label_ids;
    cypher::RelationshipDirection direction;
    std::vector<uint16_t> edge_prop_ids; // edge properties to fetch
    std::vector<uint16_t> dst_prop_ids;  // dst vertex properties to fetch
    BoundLogicalOperator child;
};

/// Filter rows by a predicate.
struct BoundFilterOp {
    BoundExpression predicate;
    BoundLogicalOperator child;
};

/// Project (transform) rows — RETURN or WITH.
struct BoundProjectOp {
    struct ProjectItem {
        BoundExpression expr;
        std::string alias; // column name
        BoundType result_type;
    };
    std::vector<ProjectItem> items;
    BoundLogicalOperator child;
};

/// Aggregation with optional group-by.
struct BoundAggregateOp {
    struct AggregateItem {
        std::string function_name;
        BoundExpression argument;
        std::string alias;
        BoundType result_type;
        const function::FunctionDef* func_def;
        bool distinct = false;
    };
    std::vector<BoundExpression> group_keys;
    std::vector<AggregateItem> aggregates;
    std::vector<std::string> output_names; // column names in output order
    BoundLogicalOperator child;
};

/// Sort rows.
struct BoundSortOp {
    struct SortItem {
        BoundExpression expr;
        cypher::OrderBy::Direction direction;
    };
    std::vector<SortItem> items;
    BoundLogicalOperator child;
};

/// Skip first N rows.
struct BoundSkipOp {
    int64_t count;
    BoundLogicalOperator child;
};

/// Limit to N rows.
struct BoundLimitOp {
    int64_t count;
    BoundLogicalOperator child;
};

/// Deduplicate rows.
struct BoundDistinctOp {
    BoundLogicalOperator child;
};

/// Create a vertex.
struct BoundCreateNodeOp {
    std::string variable;
    LabelId label_id;
    std::vector<std::pair<uint16_t, BoundExpression>> properties; // prop_id → value expr
    std::optional<BoundLogicalOperator> child;
};

/// Create an edge.
struct BoundCreateEdgeOp {
    std::string variable;
    EdgeLabelId label_id;
    std::string src_variable;
    std::string dst_variable;
    std::vector<std::pair<uint16_t, BoundExpression>> properties;
    BoundLogicalOperator child;
};

/// SET properties/labels.
struct BoundSetOp {
    enum class ItemKind {
        SET_PROPERTY,
        SET_PROPERTIES,
        SET_LABELS
    };
    struct SetItem {
        ItemKind kind;
        std::string target_variable;
        std::optional<uint16_t> prop_id;
        std::optional<BoundExpression> value_expr;
        std::optional<LabelId> label_id;
    };
    std::vector<SetItem> items;
    BoundLogicalOperator child;
};

/// REMOVE properties/labels.
struct BoundRemoveOp {
    enum class ItemKind {
        REMOVE_PROPERTY,
        REMOVE_LABEL
    };
    struct RemoveItem {
        ItemKind kind;
        std::string target_variable;
        uint16_t prop_id;
        std::optional<LabelId> label_id;
    };
    std::vector<RemoveItem> items;
    BoundLogicalOperator child;
};

/// Build a path value from named columns.
struct BoundPathBuildOp {
    std::string path_variable;
    uint32_t path_column_index;
    std::vector<std::string> element_variables; // alternating [node, edge, node, ...]
    BoundLogicalOperator child;
};

// ==================== Bound Logical Plan ====================

struct BoundLogicalPlan {
    BoundLogicalOperator root;
    std::vector<ColumnInfo> output_schema;
};

// ==================== Binder Result ====================

struct BoundStatement {
    BoundLogicalPlan plan;
    BindContext context; // includes property requirements
};

} // namespace binder
} // namespace eugraph
