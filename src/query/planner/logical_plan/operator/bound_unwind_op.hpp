#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <string>

namespace eugraph {
namespace binder {

struct BoundUnwindOp {
    BoundExpression list_expr;
    std::string variable;
    uint32_t variable_column_index;
    /// Binder-assigned slot of the unwind variable. The physical layout must
    /// use this slot verbatim: a lowered list comprehension restores its binder
    /// scope before planning, so name-based slot lookup would allocate a fresh
    /// slot and break correlated PC wiring.
    SlotId variable_slot_id = INVALID_SLOT_ID;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
