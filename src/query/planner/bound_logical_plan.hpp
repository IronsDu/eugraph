#pragma once

#include "query/planner/bind_context.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_correlated_source_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_edge_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_node_op.hpp"
#include "query/planner/logical_plan/operator/bound_delete_op.hpp"
#include "query/planner/logical_plan/operator/bound_distinct_op.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_limit_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_build_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"
#include "query/planner/logical_plan/operator/bound_remove_op.hpp"
#include "query/planner/logical_plan/operator/bound_semi_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_set_op.hpp"
#include "query/planner/logical_plan/operator/bound_skip_op.hpp"
#include "query/planner/logical_plan/operator/bound_sort_op.hpp"
#include "query/planner/logical_plan/operator/bound_union_op.hpp"
#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

#include <memory>
#include <vector>

namespace eugraph {
namespace optimizer {
struct ChosenPlan; // forward declaration — PIMPL to break circular dependency
}

namespace binder {

struct BoundLogicalPlan {
    BoundLogicalOperator root;
    std::vector<ColumnInfo> output_schema;
    // Phase 4: CBO-chosen physical plan. Null when CBO didn't produce a winner
    // (e.g. no implementation rules fired); PhysicalPlanner falls back to planBound.
    std::unique_ptr<optimizer::ChosenPlan> chosen;

    // Out-of-line special members — ChosenPlan is forward-declared (PIMPL pattern)
    BoundLogicalPlan();
    ~BoundLogicalPlan();
    BoundLogicalPlan(BoundLogicalPlan&&);
    BoundLogicalPlan& operator=(BoundLogicalPlan&&);
    BoundLogicalPlan(const BoundLogicalPlan&) = delete;
    BoundLogicalPlan& operator=(const BoundLogicalPlan&) = delete;
};

struct BoundStatement {
    BoundLogicalPlan plan;
    BindContext context;
};

} // namespace binder
} // namespace eugraph
