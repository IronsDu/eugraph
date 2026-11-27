#include "query/optimizer/framework/log_prop.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_op.hpp"

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
#include "query/planner/logical_plan/operator/bound_foreach_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_limit_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_build_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_count_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_apply_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_fork_op.hpp"
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
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundIndexProbeOp>>) {
                if (!val)
                    return LogProp{};
                return deriveIndexProbe(*val);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundIndexProbeJoinOp>>) {
                if (!val || input_lps.size() < 2)
                    return LogProp{};
                return deriveIndexProbeJoin(input_lps[0], input_lps[1]);
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
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCallOp>>) {
                return deriveCall(*val);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathCountOp>>) {
                LogProp lp;
                lp.cardinality = 1.0;
                ColumnInfo column;
                column.variable = val->output_name;
                column.column_index = 0;
                column.type_kind = binder::BoundTypeKind::INT64;
                lp.columns.push_back(std::move(column));
                lp.valid = true;
                return lp;
            }
            // Binary operators
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                return deriveBinaryJoin(*val, input_lps.at(0), input_lps.at(1));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                return deriveLeftJoin(*val, input_lps.at(0), input_lps.at(1));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                return deriveSemiJoin(*val, input_lps.at(0), input_lps.at(1));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
                return derivePatternComprehensionApply(*val, input_lps.at(0), input_lps.at(1));
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionForkOp>>) {
                return derivePatternComprehensionFork(*val, input_lps);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                return deriveUnion(*val, input_lps.at(0), input_lps.at(1));
            }
            // Write / mutation operators — passthrough cardinality and schema
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>> ||
                               std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>> ||
                               // FOREACH runs side effects per element and hands its
                               // input row through untouched: same cardinality, and no
                               // column of its own (the body's columns stay local to
                               // the sub-plan).
                               std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>>) {
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

LogProp LogPropDeriver::deriveCall(const binder::BoundCallOp& op) const {
    LogProp lp;
    lp.cardinality = 1.0;
    for (size_t i = 0; i < op.output_names.size() && i < op.output_types.size(); ++i) {
        ColumnInfo ci;
        ci.variable = op.output_names[i];
        ci.column_index = static_cast<uint32_t>(i);
        ci.type_kind = op.output_types[i].kind;
        lp.columns.push_back(std::move(ci));
    }
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

double LogPropDeriver::estimateAvgOutDegree(const std::vector<EdgeLabelId>& edge_labels, const LogProp& input,
                                            const std::string& src_var) const {
    if (!catalog_ || edge_labels.empty())
        return 0.0;
    double source_vertices = 0.0;
    for (const auto& col : input.columns) {
        if (col.variable != src_var || col.label_ids.empty())
            continue;
        for (LabelId lid : col.label_ids)
            source_vertices += static_cast<double>(catalog_->getVertexCount(lid));
        break;
    }
    if (source_vertices <= 0.0)
        source_vertices = input.cardinality;
    if (source_vertices <= 0.0)
        return 0.0;
    double edges = 0.0;
    for (EdgeLabelId elid : edge_labels)
        edges += static_cast<double>(catalog_->getEdgeCount(elid));
    return edges > 0.0 ? edges / source_vertices : 0.0;
}

double LogPropDeriver::expandDegree(const std::vector<EdgeLabelId>& edge_labels, const LogProp& input,
                                    const std::string& src_var) const {
    // avg out-degree: use max across edge_label_ids, or default
    double avgDeg = 0.0;
    for (const auto& elid : edge_labels) {
        double deg = catalog_ ? catalog_->getAvgOutDegree(elid) : 0.0;
        if (deg > 0)
            avgDeg = std::max(avgDeg, deg);
    }
    if (avgDeg == 0.0)
        avgDeg = estimateAvgOutDegree(edge_labels, input, src_var);
    if (avgDeg == 0.0)
        avgDeg = kDefaultAvgOutDegree;
    return avgDeg;
}

LogProp LogPropDeriver::deriveExpand(const binder::BoundExpandOp& op, const LogProp& input) const {
    LogProp lp;
    const double avgDeg = expandDegree(op.edge_label_ids, input, op.src_variable);
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
        if (deg > 0)
            avgDeg = std::max(avgDeg, deg);
    }
    if (avgDeg == 0.0)
        avgDeg = estimateAvgOutDegree(op.edge_label_ids, input, op.src_variable);
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
    lp.cardinality =
        op.constant.has_value() ? std::min(static_cast<double>(*op.constant), input.cardinality) : input.cardinality;
    lp.columns = input.columns;
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveSkip(const binder::BoundSkipOp& op, const LogProp& input) const {
    LogProp lp;
    lp.cardinality = op.constant.has_value() ? std::max(0.0, input.cardinality - static_cast<double>(*op.constant))
                                             : input.cardinality;
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

LogProp LogPropDeriver::derivePatternComprehensionApply(const binder::BoundPatternComprehensionApplyOp& op,
                                                        const LogProp& left, const LogProp& /*right*/) const {
    // Mirror SemiJoin shape: one output row per left row, schema is left
    // columns plus one LIST column per `outputs` entry.
    LogProp lp;
    lp.cardinality = left.cardinality;
    lp.columns = left.columns;
    for (const auto& out : op.outputs) {
        ColumnInfo ci;
        ci.variable = out.name;
        ci.column_index = out.slot_id;
        ci.type_kind = binder::BoundTypeKind::LIST;
        lp.columns.push_back(std::move(ci));
    }
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::derivePatternComprehensionFork(const binder::BoundPatternComprehensionForkOp& op,
                                                       const std::vector<LogProp>& input_lps) const {
    // The fork is lateral: `left` drives the row count, and each branch
    // contributes exactly one aggregated LIST column. The anchor only feeds the
    // branches, so it never widens the output schema.
    LogProp lp;
    if (input_lps.empty()) {
        lp.valid = true;
        return lp;
    }
    lp.cardinality = input_lps.front().cardinality;
    lp.columns = input_lps.front().columns;
    for (const auto& out : op.outputs) {
        ColumnInfo ci;
        ci.variable = out.name;
        ci.column_index = out.slot_id;
        ci.type_kind = binder::BoundTypeKind::LIST;
        lp.columns.push_back(std::move(ci));
    }
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveIndexProbe(const binder::BoundIndexProbeOp& op) const {
    // Access path shape: index range scan over the target vertex label, then
    // one edge step from the target to the source variable (the join key).
    // The scan is over the *target* label; the edge degree multiplies rows.
    LogProp lp;
    double target_rows = 0.0;
    for (LabelId lid : op.target_label_ids) {
        uint64_t cnt = catalog_ ? catalog_->getVertexCount(lid) : 0;
        target_rows += (cnt > 0) ? static_cast<double>(cnt) : kDefaultVertexCount;
    }
    if (op.target_label_ids.empty())
        target_rows = kDefaultVertexCount; // label not resolved yet

    double selectivity = kDefaultSelectivity;
    if (op.equality)
        selectivity = kIndexedSelectivity;
    else if (op.lower || op.upper)
        selectivity = kIndexRangeSelectivity;

    double avg_deg = 1.0; // one edge per probe row is the common case
    for (EdgeLabelId elid : op.edge_label_ids) {
        double deg = catalog_ ? catalog_->getAvgOutDegree(elid) : 0.0;
        if (deg > 0.0) {
            avg_deg = deg;
            break;
        }
    }
    lp.cardinality = target_rows * selectivity * avg_deg;

    // Schema: scanned target vertex, traversed edge, source (join key) vertex.
    auto push_vertex = [&lp](const std::string& name, const std::vector<LabelId>& labels) {
        if (name.empty())
            return;
        ColumnInfo ci;
        ci.variable = name;
        ci.column_index = 0;
        ci.type_kind = binder::BoundTypeKind::VERTEX_REF;
        ci.label_ids = labels;
        lp.columns.push_back(std::move(ci));
    };
    push_vertex(op.target_variable, op.target_label_ids);
    if (!op.edge_variable.empty()) {
        ColumnInfo edge;
        edge.variable = op.edge_variable;
        edge.column_index = 0;
        edge.type_kind = binder::BoundTypeKind::EDGE_KEY;
        lp.columns.push_back(std::move(edge));
    }
    push_vertex(op.source_variable, {});
    lp.valid = true;
    return lp;
}

LogProp LogPropDeriver::deriveIndexProbeJoin(const LogProp& anchor, const LogProp& probe_input) const {
    LogProp lp;
    lp.columns = anchor.columns;
    auto append_missing = [&lp](const ColumnInfo& col) {
        for (const auto& existing : lp.columns)
            if (existing.variable == col.variable)
                return;
        lp.columns.push_back(col);
    };
    for (const auto& col : probe_input.columns)
        append_missing(col);

    // Hash join on a vertex key: every probe row matches at most one anchor row,
    // so the output cannot exceed the probe side. Both sides are estimated by
    // their own access paths; no re-derivation of the anchor happens here.
    lp.cardinality = probe_input.cardinality;
    lp.valid = true;
    return lp;
}

double LogPropDeriver::deriveCandidateCardinality(const binder::BoundLogicalOperator& op, const LogProp& group_lp,
                                                  const std::vector<double>& input_cards) const {
    auto card = [&](size_t i) -> double {
        if (i < input_cards.size() && input_cards[i] >= 0.0)
            return input_cards[i];
        return -1.0;
    };
    const double in0 = card(0);
    const double in1 = card(1);
    const double fallback = group_lp.cardinality;

    return std::visit(
        [&](const auto& val) -> double {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                double total = 0.0;
                for (LabelId lid : val.label_ids) {
                    uint64_t cnt = catalog_ ? catalog_->getVertexCount(lid) : 0;
                    total += (cnt > 0) ? static_cast<double>(cnt) : kDefaultVertexCount;
                }
                if (val.label_ids.empty())
                    total = kDefaultVertexCount;
                if (val.index_scan && val.index_id != 0) {
                    if (!val.index_eq_values.empty())
                        total *= kIndexedSelectivity;
                    else if (val.index_lower || val.index_upper)
                        total *= kIndexRangeSelectivity;
                }
                return total;
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp> ||
                                 std::is_same_v<T, binder::BoundSingletonOp> ||
                                 std::is_same_v<T, binder::BoundCorrelatedSourceOp> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundIndexProbeOp>>) {
                // Leaf access path: the group LogProp already derives from this
                // descriptor (probe label/range/degree).
                return fallback;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                return in0 >= 0.0 && val ? in0 * estimateSelectivity(val->predicate) : fallback;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (!val || in0 < 0.0)
                    return fallback;
                LogProp pseudo = group_lp;
                pseudo.cardinality = in0;
                return in0 * expandDegree(val->edge_label_ids, pseudo, val->src_variable);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (!val || in0 < 0.0)
                    return fallback;
                LogProp pseudo = group_lp;
                pseudo.cardinality = in0;
                const double avg_deg = expandDegree(val->edge_label_ids, pseudo, val->src_variable);
                const int64_t hops = std::min<int64_t>(val->max_hops, 4);
                double factor = 0.0;
                for (int64_t h = val->min_hops; h <= hops; ++h)
                    factor += std::pow(avg_deg, static_cast<double>(h));
                return in0 * factor;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                if (in0 < 0.0)
                    return fallback;
                if (val && val->constant)
                    return std::min<double>(in0, static_cast<double>(std::max<int64_t>(0, *val->constant)));
                return in0;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                if (in0 < 0.0)
                    return fallback;
                if (val && val->constant)
                    return std::max<double>(0.0, in0 - static_cast<double>(std::max<int64_t>(0, *val->constant)));
                return in0;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>>) {
                return in0 >= 0.0 ? in0 : fallback;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                if (!val)
                    return fallback;
                if (in0 < 0.0 || in1 < 0.0)
                    return fallback;
                if (val->join_type == binder::JoinType::Cross)
                    return in0 * in1;
                return in0 * in1 * kDefaultSelectivity;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundIndexProbeJoinOp>>) {
                // Hash join on a vertex key: bounded by the probe side.
                return in1 >= 0.0 ? in1 : fallback;
            } else {
                // Aggregate / semi / left / union / PC apply / PC fork / path
                // count / call: group-key or multi-input dependent; keep the
                // plan-independent estimate.
                return fallback;
            }
        },
        op);
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
