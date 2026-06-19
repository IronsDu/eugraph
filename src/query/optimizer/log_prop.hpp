#pragma once

#include "common/types/graph_types.hpp"
#include "query/catalog/catalog.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace optimizer {

// ============================================================
// ColumnInfo — schema entry for one output column (Columbia: ATTRIBUTE)
//
// Tracks the variable name, column position, type kind, and (for node
// columns) the set of labels. Used for join-key detection and
// index-aware selectivity estimation.
// ============================================================
struct ColumnInfo {
    std::string variable;
    uint32_t column_index = 0;
    binder::BoundTypeKind type_kind = binder::BoundTypeKind::ANY;
    std::vector<LabelId> label_ids; // node labels; empty for edges/literals
};

// ============================================================
// LogProp — logical properties of a Group (Columbia: LOG_PROP)
//
// All expressions in a Group are logically equivalent, so they share
// one LogProp. Lazily derived from the first logical expression and
// cached on the Group.
// ============================================================
struct LogProp {
    double cardinality = 0.0;
    std::vector<ColumnInfo> columns;
    bool valid = false;
};

// ============================================================
// LogPropDeriver — derives LogProp from a BoundLogicalOperator
//
// Visits the operator, combines input LogProps (one per child group),
// and produces the output LogProp. Uses Catalog stats for cardinality;
// falls back to default constants when stats are unavailable.
// ============================================================
class LogPropDeriver {
public:
    explicit LogPropDeriver(const catalog::Catalog* catalog) : catalog_(catalog) {}

    LogProp derive(const binder::BoundLogicalOperator& op, const std::vector<LogProp>& input_lps);

    // Default constants used when catalog stats are unavailable (value 0)
    static constexpr double kDefaultVertexCount = 1000.0;
    static constexpr double kDefaultAvgOutDegree = 10.0;
    static constexpr double kDefaultSelectivity = 0.1;
    static constexpr double kIndexedSelectivity = 0.01;
    static constexpr double kRangeSelectivity = 0.3;
    static constexpr double kUnknownSelectivity = 0.5;

private:
    const catalog::Catalog* catalog_;

    // Per-operator derivation (called via std::visit)
    LogProp deriveLabelScan(const binder::BoundLabelScanOp& op) const;
    LogProp deriveScan(const binder::BoundScanOp& op) const;
    LogProp deriveSingleton() const;
    LogProp deriveCorrelatedSource(const binder::BoundCorrelatedSourceOp& op) const;
    LogProp deriveExpand(const binder::BoundExpandOp& op, const LogProp& input) const;
    LogProp deriveVarLenExpand(const binder::BoundVarLenExpandOp& op, const LogProp& input) const;
    LogProp deriveFilter(const binder::BoundFilterOp& op, const LogProp& input) const;
    LogProp deriveProject(const binder::BoundProjectOp& op, const LogProp& input) const;
    LogProp deriveAggregate(const binder::BoundAggregateOp& op, const LogProp& input) const;
    LogProp deriveSort(const LogProp& input) const;
    LogProp deriveLimit(const binder::BoundLimitOp& op, const LogProp& input) const;
    LogProp deriveSkip(const binder::BoundSkipOp& op, const LogProp& input) const;
    LogProp deriveDistinct(const LogProp& input) const;
    LogProp derivePathBuild(const binder::BoundPathBuildOp& op, const LogProp& input) const;
    LogProp deriveUnwind(const binder::BoundUnwindOp& op, const LogProp& input) const;
    LogProp deriveBinaryJoin(const binder::BoundBinaryJoinOp& op, const LogProp& left, const LogProp& right) const;
    LogProp deriveLeftJoin(const binder::BoundLeftJoinOp& op, const LogProp& left, const LogProp& right) const;
    LogProp deriveSemiJoin(const binder::BoundSemiJoinOp& op, const LogProp& left, const LogProp& right) const;
    LogProp deriveUnion(const binder::BoundUnionOp& op, const LogProp& left, const LogProp& right) const;
    LogProp derivePassthrough(const LogProp& input) const;

    // Selectivity estimation for filter predicates
    double estimateSelectivity(const binder::BoundExpression& pred) const;
    double estimatePropertyRefSelectivity(const binder::BoundPropertyRef& pref) const;
};

} // namespace optimizer
} // namespace eugraph
