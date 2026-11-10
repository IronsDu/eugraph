#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/operator/correlated_source_physical_op.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

/// Shared-anchor pattern comprehension fork.
///
/// For every outer batch this operator
///   1. injects the batch's correlation values into the anchor's
///      `CorrelatedSourcePhysicalOp`,
///   2. executes the anchor **once** and materialises its output chunks,
///   3. replays those chunks into every branch (each branch starts with its own
///      `CorrelatedSourcePhysicalOp`), and
///   4. appends one collected LIST column per branch to the outer row.
///
/// This is the plan-level equivalent of two `PatternComprehensionApplyPhysicalOp`
/// operators that would each re-execute the same prefix: the anchor rows are a
/// single explicit plan edge (produced once, consumed N times), not an
/// operator-local cache.
class PatternComprehensionForkPhysicalOp : public PhysicalOperator {
public:
    PatternComprehensionForkPhysicalOp(std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> anchor,
                                       CorrelatedSourcePhysicalOp* anchor_source,
                                       std::vector<std::unique_ptr<PhysicalOperator>> branches,
                                       std::vector<CorrelatedSourcePhysicalOp*> branch_sources,
                                       std::vector<int32_t> anchor_value_columns,
                                       std::vector<binder::BoundType> list_element_types,
                                       std::vector<int> branch_batch_columns, std::vector<size_t> branch_list_columns)
        : left_(std::move(left)), anchor_(std::move(anchor)), anchor_source_(anchor_source),
          branches_(std::move(branches)), branch_sources_(std::move(branch_sources)),
          anchor_value_columns_(std::move(anchor_value_columns)), list_element_types_(std::move(list_element_types)),
          branch_batch_columns_(std::move(branch_batch_columns)), branch_list_columns_(std::move(branch_list_columns)) {
    }

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "PatternComprehensionFork";
    }
    std::vector<const PhysicalOperator*> children() const override;

    void deriveOutputLayout(const TupleSlotLayout& parent_layout) override;

    /// Slot ids of the appended LIST columns, in branch order. Appended after
    /// the left layout so downstream BoundColumnRefs resolve through slots.
    void setOutputSlots(std::vector<binder::SlotId> slots) {
        output_slots_ = std::move(slots);
    }

    /// Number of times the anchor pipeline has been executed. Diagnostics for
    /// tests and PROFILE: a shared anchor must run once per outer batch, not
    /// once per branch.
    size_t anchorExecutions() const {
        return anchor_executions_;
    }

private:
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> anchor_;
    CorrelatedSourcePhysicalOp* anchor_source_;
    std::vector<std::unique_ptr<PhysicalOperator>> branches_;
    std::vector<CorrelatedSourcePhysicalOp*> branch_sources_;
    /// Per anchor correlated-source column: the left output column that feeds
    /// it, or -1 for the batch key (filled with the row's index in the batch).
    /// Derived from the anchor's source schema, so column order can never drift
    /// from the operator's expectation.
    std::vector<int32_t> anchor_value_columns_;
    std::vector<binder::BoundType> list_element_types_;
    /// Branch output position of the batch key (-1 when the branch is
    /// unbucketed) and of the collected list column.
    std::vector<int> branch_batch_columns_;
    std::vector<size_t> branch_list_columns_;
    std::vector<binder::SlotId> output_slots_;
    size_t anchor_executions_ = 0;
    static constexpr size_t kBatchSize = 256;
};

} // namespace compute
} // namespace eugraph
