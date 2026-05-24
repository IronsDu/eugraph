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

/// Left join: for each left row, inject correlated values into the right
/// sub-plan. If the right produces matches, emit left × right combinations.
/// If no match, emit the left row with NULLs for all right columns.
class LeftJoinPhysicalOp : public PhysicalOperator {
public:
    LeftJoinPhysicalOp(std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> right,
                       CorrelatedSourcePhysicalOp* correlated_source, std::vector<uint32_t> left_correlation_cols,
                       std::vector<binder::BoundType> right_output_types)
        : left_(std::move(left)), right_(std::move(right)), correlated_source_(correlated_source),
          left_correlation_cols_(std::move(left_correlation_cols)), right_output_types_(std::move(right_output_types)) {
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "LeftJoin";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {left_.get(), right_.get()};
    }

private:
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
    CorrelatedSourcePhysicalOp* correlated_source_;
    std::vector<uint32_t> left_correlation_cols_;
    std::vector<binder::BoundType> right_output_types_;
};

} // namespace compute
} // namespace eugraph
