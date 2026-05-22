#pragma once

#include "query/planner/logical_plan/operator/bound_correlated_source_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_singleton_op.hpp"

#include <memory>
#include <variant>

namespace eugraph {
namespace binder {

struct BoundExpandOp;
struct BoundFilterOp;
struct BoundProjectOp;
struct BoundAggregateOp;
struct BoundSortOp;
struct BoundSkipOp;
struct BoundLimitOp;
struct BoundDistinctOp;
struct BoundCreateNodeOp;
struct BoundCreateEdgeOp;
struct BoundSetOp;
struct BoundRemoveOp;
struct BoundPathBuildOp;
struct BoundVarLenExpandOp;
struct BoundBinaryJoinOp;
struct BoundSemiJoinOp;
struct BoundUnwindOp;

using BoundLogicalOperator =
    std::variant<BoundSingletonOp, BoundCorrelatedSourceOp, BoundScanOp, BoundLabelScanOp,
                 std::unique_ptr<BoundExpandOp>, std::unique_ptr<BoundFilterOp>, std::unique_ptr<BoundProjectOp>,
                 std::unique_ptr<BoundAggregateOp>, std::unique_ptr<BoundSortOp>, std::unique_ptr<BoundSkipOp>,
                 std::unique_ptr<BoundLimitOp>, std::unique_ptr<BoundDistinctOp>, std::unique_ptr<BoundCreateNodeOp>,
                 std::unique_ptr<BoundCreateEdgeOp>, std::unique_ptr<BoundSetOp>, std::unique_ptr<BoundRemoveOp>,
                 std::unique_ptr<BoundPathBuildOp>, std::unique_ptr<BoundVarLenExpandOp>,
                 std::unique_ptr<BoundBinaryJoinOp>, std::unique_ptr<BoundSemiJoinOp>, std::unique_ptr<BoundUnwindOp>>;

struct BoundLogicalPlan;

} // namespace binder
} // namespace eugraph
