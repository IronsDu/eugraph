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

/// Semi-join: emits rows from the left child that have at least one matching
/// row in the right (EXISTS) sub-plan. Correlated variable values are extracted
/// from left rows and injected into the right sub-plan's CorrelatedSource leaf.
class SemiJoinPhysicalOp : public PhysicalOperator {
public:
    SemiJoinPhysicalOp(std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> right,
                       CorrelatedSourcePhysicalOp* correlated_source, std::vector<uint32_t> left_correlation_cols,
                       bool anti = false)
        : left_(std::move(left)), right_(std::move(right)), correlated_source_(correlated_source),
          left_correlation_cols_(std::move(left_correlation_cols)), anti_(anti) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return anti_ ? "AntiSemiJoin" : "SemiJoin";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {left_.get(), right_.get()};
    }

    void deriveOutputLayout(const TupleSlotLayout&) override {
        // SemiJoin output = left columns only
        slot_layout_ = left_->slotLayout();
    }

private:
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
    CorrelatedSourcePhysicalOp* correlated_source_;
    std::vector<uint32_t> left_correlation_cols_;
    bool anti_ = false;
};

} // namespace compute
} // namespace eugraph
