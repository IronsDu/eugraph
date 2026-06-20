#include "query/optimizer/log_prop.hpp"

#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_correlated_source_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_edge_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_node_op.hpp"
#include "query/planner/logical_plan/operator/bound_delete_op.hpp"
#include "query/planner/logical_plan/operator/bound_distinct_op.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_limit_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_build_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"
#include "query/planner/logical_plan/operator/bound_remove_op.hpp"
#include "query/planner/logical_plan/operator/bound_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_semi_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_set_op.hpp"
#include "query/planner/logical_plan/operator/bound_singleton_op.hpp"
#include "query/planner/logical_plan/operator/bound_skip_op.hpp"
#include "query/planner/logical_plan/operator/bound_sort_op.hpp"
#include "query/planner/logical_plan/operator/bound_union_op.hpp"
#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

#include <algorithm>
#include <cmath>

namespace eugraph {
namespace optimizer {

// ============================================================
// Main derive dispatch — visits BoundLogicalOperator variant
// ============================================================
LogProp LogPropDeriver::derive(const binder::BoundLogicalOperator& op, const std::vector<LogProp>& input_lps) {
    return std::visit(
        [this, &input_lps](const auto& val) -> LogProp {
            using T = std::decay_t<decltype(val)>;

            // Leaf operators (held by value)
            if constexpr (std::is_same_v<T, binder::BoundSingletonOp>) {
                return deriveSingleton();
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                return deriveCorrelatedSource(val);
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                return deriveScan(val);
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                return deriveLabelScan(val);
            }
            // Unary operators (held by unique_ptr)
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                return deriveExpand(*val, input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                return deriveVarLenExpand(*val, input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                return deriveFilter(*val, input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                return deriveProject(*val, input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                return deriveAggregate(*val, input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                return deriveSort(input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                return deriveLimit(*val, input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                return deriveSkip(*val, input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                return deriveDistinct(input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                return derivePathBuild(*val, input_lps.at(0));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                return deriveUnwind(*val, input_lps.at(0));
            }
            // Binary operators
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                return deriveBinaryJoin(*val, input_lps.at(0), input_lps.at(1));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                return deriveLeftJoin(*val, input_lps.at(0), input_lps.at(1));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                return deriveSemiJoin(*val, input_lps.at(0), input_lps.at(1));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                return deriveUnion(*val, input_lps.at(0), input_lps.at(1));
            }
            // Write / mutation operators — passthrough cardinality and schema
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                // These ops may have 0 children (standalone CREATE) or 1 child
                if (input_lps.empty()) {
                    LogProp lp;
                    lp.cardinality = 1.0;
                    lp.valid = true;
                    return lp;
                }
                return derivePassthrough(input_lps.at(0));
            }
            // Fallback: should not reach here
            return LogProp{};
        },
        op);
}

// ============================================================
// Leaf operators
// ============================================================

LogProp LogPropDeriver::deriveSingleton() const {
    LogProp lp;
    lp.cardinality = 1.0;
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveCorrelatedSource(const binder::BoundCorrelatedSourceOp& op) const {
    LogProp lp;
    // Correlated source produces 1 row per outer row — estimate conservatively
    lp.cardinality = 1.0;
    for (size_t i = 0; i < op.variables.size() && i < op.column_indices.size(); ++i) {
        ColumnInfo ci;
        ci.variable = op.variables[i];
        ci.column_index = op.column_indices[i];
        ci.type_kind = i < op.types.size() ? op.types[i].kind : binder::BoundTypeKind::ANY;
        lp.columns.push_back(std::move(ci));
    }
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveScan(const binder::BoundScanOp& op) const {
    LogProp lp;
    // No label filter — scan all vertices. Use default count.
    lp.cardinality = kDefaultVertexCount;
    ColumnInfo ci;
    ci.variable = op.variable;
    ci.column_index = op.column_index;
    ci.type_kind = binder::BoundTypeKind::VERTEX_REF;
    lp.columns.push_back(std::move(ci));
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveLabelScan(const binder::BoundLabelScanOp& op) const {
    LogProp lp;
    // Sum vertex counts across all labels; default per-label if unknown
    double total = 0.0;
    for (size_t i = 0; i < op.label_ids.size(); ++i) {
        uint64_t cnt = catalog_ ? catalog_->getVertexCount(op.label_ids[i]) : 0;
        total += (cnt > 0) ? static_cast<double>(cnt) : kDefaultVertexCount;
    }
    if (op.label_ids.empty()) {
        total = kDefaultVertexCount;
    }
    lp.cardinality = total;

    ColumnInfo ci;
    ci.variable = op.variable;
    ci.column_index = op.column_index;
    ci.type_kind = binder::BoundTypeKind::VERTEX_REF;
    ci.label_ids = op.label_ids;
    lp.columns.push_back(std::move(ci));
    lp.valid = true;
    return lp;
}

// ============================================================
// Unary operators
// ============================================================

LogProp LogPropDeriver::deriveExpand(const binder::BoundExpandOp& op, const LogProp& input) const {
    LogProp lp;
    // avg out-degree: use max across edge_label_ids, or default
    double avgDeg = 0.0;
    for (const auto& elid : op.edge_label_ids) {
        double deg = catalog_ ? catalog_->getAvgOutDegree(elid) : 0.0;
        avgDeg = std::max(avgDeg, deg > 0 ? deg : kDefaultAvgOutDegree);
    }
    if (avgDeg == 0.0)
        avgDeg = kDefaultAvgOutDegree;

    lp.cardinality = input.cardinality * avgDeg;

    // Schema: input columns + edge + dst
    lp.columns = input.columns;

    ColumnInfo edgeCol;
    edgeCol.variable = op.edge_variable;
    edgeCol.column_index = op.edge_column_index;
    edgeCol.type_kind = binder::BoundTypeKind::EDGE_KEY;
    lp.columns.push_back(std::move(edgeCol));

    ColumnInfo dstCol;
    dstCol.variable = op.dst_variable;
    dstCol.column_index = op.dst_column_index;
    dstCol.type_kind = binder::BoundTypeKind::VERTEX_REF;
    lp.columns.push_back(std::move(dstCol));

    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveVarLenExpand(const binder::BoundVarLenExpandOp& op, const LogProp& input) const {
    LogProp lp;
    double avgDeg = 0.0;
    for (const auto& elid : op.edge_label_ids) {
        double deg = catalog_ ? catalog_->getAvgOutDegree(elid) : 0.0;
        avgDeg = std::max(avgDeg, deg > 0 ? deg : kDefaultAvgOutDegree);
    }
    if (avgDeg == 0.0)
        avgDeg = kDefaultAvgOutDegree;

    // Cap max_hops to avoid combinatorial explosion in estimate
    int64_t hops = std::min<int64_t>(op.max_hops, 4);
    double factor = 0.0;
    for (int64_t h = op.min_hops; h <= hops; ++h) {
        factor += std::pow(avgDeg, static_cast<double>(h));
    }
    lp.cardinality = input.cardinality * factor;

    lp.columns = input.columns;

    ColumnInfo edgeCol;
    edgeCol.variable = op.edge_variable;
    edgeCol.column_index = op.edge_column_index;
    edgeCol.type_kind = binder::BoundTypeKind::LIST;
    lp.columns.push_back(std::move(edgeCol));

    ColumnInfo dstCol;
    dstCol.variable = op.dst_variable;
    dstCol.column_index = op.dst_column_index;
    dstCol.type_kind = binder::BoundTypeKind::VERTEX_REF;
    lp.columns.push_back(std::move(dstCol));

    if (!op.path_variable.empty()) {
        ColumnInfo pathCol;
        pathCol.variable = op.path_variable;
        pathCol.column_index = op.path_column_index;
        pathCol.type_kind = binder::BoundTypeKind::PATH;
        lp.columns.push_back(std::move(pathCol));
    }

    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveFilter(const binder::BoundFilterOp& op, const LogProp& input) const {
    LogProp lp;
    double sel = estimateSelectivity(op.predicate);
    lp.cardinality = input.cardinality * sel;
    lp.columns = input.columns; // passthrough schema
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveProject(const binder::BoundProjectOp& op, const LogProp& input) const {
    LogProp lp;
    lp.cardinality = input.cardinality;
    // Schema: aliases become output columns
    for (const auto& item : op.items) {
        ColumnInfo ci;
        ci.variable = item.alias;
        ci.type_kind = item.result_type.kind;
        lp.columns.push_back(std::move(ci));
    }
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveAggregate(const binder::BoundAggregateOp& op, const LogProp& input) const {
    LogProp lp;
    if (op.group_keys.empty()) {
        lp.cardinality = 1.0;
    } else {
        // Estimate distinct groups as fraction of input
        lp.cardinality = std::max(1.0, input.cardinality * 0.1);
    }
    for (const auto& name : op.output_names) {
        ColumnInfo ci;
        ci.variable = name;
        lp.columns.push_back(std::move(ci));
    }
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveSort(const LogProp& input) const {
    LogProp lp;
    lp.cardinality = input.cardinality;
    lp.columns = input.columns;
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveLimit(const binder::BoundLimitOp& op, const LogProp& input) const {
    LogProp lp;
    lp.cardinality = std::min(static_cast<double>(op.count), input.cardinality);
    lp.columns = input.columns;
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveSkip(const binder::BoundSkipOp& op, const LogProp& input) const {
    LogProp lp;
    lp.cardinality = std::max(0.0, input.cardinality - static_cast<double>(op.count));
    lp.columns = input.columns;
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveDistinct(const LogProp& input) const {
    LogProp lp;
    lp.cardinality = input.cardinality * 0.5; // estimate
    lp.columns = input.columns;
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::derivePathBuild(const binder::BoundPathBuildOp& op, const LogProp& input) const {
    LogProp lp;
    lp.cardinality = input.cardinality;
    lp.columns = input.columns;

    ColumnInfo pathCol;
    pathCol.variable = op.path_variable;
    pathCol.column_index = op.path_column_index;
    pathCol.type_kind = binder::BoundTypeKind::PATH_TOPOLOGY;
    lp.columns.push_back(std::move(pathCol));

    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveUnwind(const binder::BoundUnwindOp& op, const LogProp& input) const {
    LogProp lp;
    // Estimate: each row produces ~3 rows (average list length)
    lp.cardinality = input.cardinality * 3.0;
    lp.columns = input.columns;

    ColumnInfo ci;
    ci.variable = op.variable;
    ci.column_index = op.variable_column_index;
    lp.columns.push_back(std::move(ci));

    lp.valid = true;
    return lp;
}

// ============================================================
// Binary operators
// ============================================================

LogProp LogPropDeriver::deriveBinaryJoin(const binder::BoundBinaryJoinOp& op, const LogProp& left,
                                         const LogProp& right) const {
    LogProp lp;
    if (op.join_type == binder::JoinType::Cross) {
        lp.cardinality = left.cardinality * right.cardinality;
    } else {
        // Inner join: left × right × selectivity
        lp.cardinality = left.cardinality * right.cardinality * kDefaultSelectivity;
    }
    // Schema: merge left + right
    lp.columns = left.columns;
    lp.columns.insert(lp.columns.end(), right.columns.begin(), right.columns.end());
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveLeftJoin(const binder::BoundLeftJoinOp& /*op*/, const LogProp& left,
                                       const LogProp& right) const {
    LogProp lp;
    // Left join preserves all left rows; right contributes matches
    lp.cardinality = left.cardinality * (1.0 + right.cardinality * kDefaultSelectivity);
    lp.columns = left.columns;
    lp.columns.insert(lp.columns.end(), right.columns.begin(), right.columns.end());
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveSemiJoin(const binder::BoundSemiJoinOp& op, const LogProp& left,
                                       const LogProp& /*right*/) const {
    LogProp lp;
    if (op.anti) {
        // Anti-semi-join: left rows with NO match
        lp.cardinality = left.cardinality * kUnknownSelectivity;
    } else {
        // Semi-join: left rows with at least one match
        lp.cardinality = left.cardinality * kDefaultSelectivity;
    }
    lp.columns = left.columns; // only left schema
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveUnion(const binder::BoundUnionOp& op, const LogProp& left, const LogProp& right) const {
    LogProp lp;
    if (op.all) {
        lp.cardinality = left.cardinality + right.cardinality;
    } else {
        // UNION (dedup): estimate 80% of sum
        lp.cardinality = (left.cardinality + right.cardinality) * 0.8;
    }
    lp.columns = left.columns; // assume same schema
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::derivePassthrough(const LogProp& input) const {
    LogProp lp;
    lp.cardinality = input.cardinality;
    lp.columns = input.columns;
    lp.valid = true;
    return lp;
}

// ============================================================
// Selectivity estimation
// ============================================================

double LogPropDeriver::estimateSelectivity(const binder::BoundExpression& pred) const {
    return std::visit(
        [this](const auto& val) -> double {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundLiteral>) {
                // Literal predicate: true → 1.0, false → 0.0
                if (std::holds_alternative<bool>(val.value)) {
                    return std::get<bool>(val.value) ? 1.0 : 0.0;
                }
                return kUnknownSelectivity;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                using cypher::BinaryOperator;
                switch (val->op) {
                case BinaryOperator::AND:
                    return estimateSelectivity(val->left) * estimateSelectivity(val->right);
                case BinaryOperator::OR: {
                    double s1 = estimateSelectivity(val->left);
                    double s2 = estimateSelectivity(val->right);
                    return 1.0 - (1.0 - s1) * (1.0 - s2);
                }
                case BinaryOperator::EQ:
                case BinaryOperator::NEQ:
                    // Check if LHS is a property ref with index
                    if (std::holds_alternative<std::unique_ptr<binder::BoundPropertyRef>>(val->left)) {
                        return estimatePropertyRefSelectivity(
                            *std::get<std::unique_ptr<binder::BoundPropertyRef>>(val->left));
                    }
                    return kDefaultSelectivity;
                case BinaryOperator::LT:
                case BinaryOperator::LTE:
                case BinaryOperator::GT:
                case BinaryOperator::GTE:
                    return kRangeSelectivity;
                default:
                    return kUnknownSelectivity;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                // NOT: 1 - child selectivity
                if (val->op == cypher::UnaryOperator::NOT) {
                    return 1.0 - estimateSelectivity(val->operand);
                }
                return kUnknownSelectivity;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                return estimatePropertyRefSelectivity(*val);
            }
            return kUnknownSelectivity;
        },
        pred);
}

double LogPropDeriver::estimatePropertyRefSelectivity(const binder::BoundPropertyRef& pref) const {
    // Check if any candidate has an index on this property
    if (catalog_) {
        for (const auto& cand : pref.candidates) {
            if (catalog_->hasIndex(cand.label_id, cand.prop_id)) {
                return kIndexedSelectivity;
            }
        }
    }
    return kDefaultSelectivity;
}

} // namespace optimizer
} // namespace eugraph
