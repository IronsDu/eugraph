#pragma once

#include "query/physical_plan/physical_operator_base.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>

namespace eugraph {
namespace compute {

class LimitPhysicalOp : public PhysicalOperator {
public:
    LimitPhysicalOp(int64_t limit, std::unique_ptr<PhysicalOperator> child) : limit_(limit), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Limit(" + std::to_string(limit_) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    int64_t limit_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
