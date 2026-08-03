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

/// Pattern comprehension apply: per outer row, executes the right (correlated)
/// sub-plan once and collects the right's first column into a ListValue. The
/// list is emitted as a new column appended to the left's row. Output layout
/// = left columns + one list column per registered output.
///
/// Mirrors SemiJoinPhysicalOp's correlation wiring but emits all left rows
/// (no filtering) and extends the schema instead of pruning it.
class PatternComprehensionApplyPhysicalOp : public PhysicalOperator {
public:
    PatternComprehensionApplyPhysicalOp(std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> right,
                                        CorrelatedSourcePhysicalOp* correlated_source,
                                        std::vector<uint32_t> left_correlation_cols,
                                        std::vector<binder::BoundType> list_element_types)
        : left_(std::move(left)), right_(std::move(right)), correlated_source_(correlated_source),
          left_correlation_cols_(std::move(left_correlation_cols)), list_element_types_(std::move(list_element_types)) {
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "PatternComprehensionApply";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {left_.get(), right_.get()};
    }

    void deriveOutputLayout(const TupleSlotLayout& parent_layout) override;

private:
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
    CorrelatedSourcePhysicalOp* correlated_source_;
    std::vector<uint32_t> left_correlation_cols_;
    std::vector<binder::BoundType> list_element_types_;
};

} // namespace compute
} // namespace eugraph
