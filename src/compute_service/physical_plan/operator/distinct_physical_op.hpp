#pragma once

#include "compute_service/executor/data_chunk.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <unordered_set>

namespace eugraph {
namespace compute {

class DistinctPhysicalOp : public PhysicalOperator {
public:
    DistinctPhysicalOp(std::unique_ptr<PhysicalOperator> child) : child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Distinct";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
