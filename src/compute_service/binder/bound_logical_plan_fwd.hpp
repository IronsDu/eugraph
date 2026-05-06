#pragma once

#include "compute_service/binder/bind_context.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace eugraph {
namespace binder {

// Forward declarations for all bound logical operator types.
struct BoundScanOp;
struct BoundLabelScanOp;
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

using BoundLogicalOperator =
    std::variant<BoundScanOp, BoundLabelScanOp, std::unique_ptr<BoundExpandOp>, std::unique_ptr<BoundFilterOp>,
                 std::unique_ptr<BoundProjectOp>, std::unique_ptr<BoundAggregateOp>, std::unique_ptr<BoundSortOp>,
                 std::unique_ptr<BoundSkipOp>, std::unique_ptr<BoundLimitOp>, std::unique_ptr<BoundDistinctOp>,
                 std::unique_ptr<BoundCreateNodeOp>, std::unique_ptr<BoundCreateEdgeOp>, std::unique_ptr<BoundSetOp>,
                 std::unique_ptr<BoundRemoveOp>, std::unique_ptr<BoundPathBuildOp>>;

} // namespace binder
} // namespace eugraph
