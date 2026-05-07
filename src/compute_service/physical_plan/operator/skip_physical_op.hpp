#pragma once

#include "compute_service/physical_plan/physical_operator_base.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>

namespace eugraph {
namespace compute {

class SkipPhysicalOp : public PhysicalOperator {
public:
    SkipPhysicalOp(int64_t skip, std::unique_ptr<PhysicalOperator> child) : skip_(skip), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override { return executeViaChunk(); }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Skip(" + std::to_string(skip_) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    int64_t skip_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
