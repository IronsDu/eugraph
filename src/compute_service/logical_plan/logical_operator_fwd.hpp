#pragma once

#include "compute_service/parser/ast.hpp"

#include <memory>
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

} // namespace compute
} // namespace eugraph
