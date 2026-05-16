#pragma once

#include "query/executor/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class SingletonPhysicalOp : public PhysicalOperator {
public:
    explicit SingletonPhysicalOp(std::vector<binder::BoundType> output_types)
        : output_types_(std::move(output_types)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;

    std::string toString() const override {
        return "Singleton";
    }

private:
    std::vector<binder::BoundType> output_types_;
};

} // namespace compute
} // namespace eugraph
