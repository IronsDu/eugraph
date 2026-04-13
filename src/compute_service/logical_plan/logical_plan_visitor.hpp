#pragma once

#include "compute_service/logical_plan/logical_operator.hpp"

#include <string>

namespace eugraph {
namespace compute {

/// Visitor interface for logical operators. Provides an extension point for
/// optimizers and plan transformations.
class LogicalPlanVisitor {
public:
    virtual ~LogicalPlanVisitor() = default;

    virtual void visit(const AllNodeScanOp& op) = 0;
    virtual void visit(const LabelScanOp& op) = 0;
    virtual void visit(const ExpandOp& op) = 0;
    virtual void visit(const FilterOp& op) = 0;
    virtual void visit(const ProjectOp& op) = 0;
    virtual void visit(const LimitOp& op) = 0;
    virtual void visit(const CreateNodeOp& op) = 0;
    virtual void visit(const CreateEdgeOp& op) = 0;
};

} // namespace compute
} // namespace eugraph
