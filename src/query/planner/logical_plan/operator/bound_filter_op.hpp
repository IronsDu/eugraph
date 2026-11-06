#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace binder {

struct BoundFilterOp {
    /// Internal relationship-uniqueness predicates are emitted by the binder
    /// and moved by the optimizer. The scope is semantic metadata: a filter
    /// created inside a correlated subplan must not be placed by rules that
    /// assume top-level cardinality.
    enum class UniquenessScope : uint8_t {
        None = 0,
        TopLevel = 1,
        CorrelatedSubplan = 2
    };

    BoundExpression predicate;
    BoundLogicalOperator child;
    UniquenessScope uniqueness_scope = UniquenessScope::None;
};

} // namespace binder
} // namespace eugraph
