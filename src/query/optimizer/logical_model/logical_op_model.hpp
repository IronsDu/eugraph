#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <memory>

namespace eugraph {
namespace optimizer {

enum class OptNodeType;

/// Polymorphic adapter for the logical-operator model.
///
/// The Memo/search framework only talks to this interface.  Concrete
/// Bound*Op types are implementation details of the logical model and are
/// not included by framework/memo.cpp.
class LogicalOpModel {
public:
    virtual ~LogicalOpModel() = default;

    virtual OptNodeType type(const binder::BoundLogicalOperator& op) const = 0;

    /// Clone operator data; children are intentionally not cloned.
    virtual binder::BoundLogicalOperator clone(const binder::BoundLogicalOperator& op) const = 0;

    virtual bool equals(const binder::BoundLogicalOperator& a, const binder::BoundLogicalOperator& b) const = 0;
    virtual uint64_t hash(const binder::BoundLogicalOperator& op) const = 0;

    virtual int childCount(const binder::BoundLogicalOperator& op) const = 0;

    /// Move child `index` out of `op`, leaving a BoundScanOp placeholder.
    virtual binder::BoundLogicalOperator takeChild(binder::BoundLogicalOperator& op, int index) const = 0;

    /// Install `child` at `index`; binary operators use 0=left, 1=right.
    virtual void setChild(binder::BoundLogicalOperator& op, int index, binder::BoundLogicalOperator child) const = 0;
};

const LogicalOpModel& logicalOpModel();

} // namespace optimizer
} // namespace eugraph
