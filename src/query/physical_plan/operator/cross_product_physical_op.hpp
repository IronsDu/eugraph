#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class CrossProductPhysicalOp : public PhysicalOperator {
public:
    CrossProductPhysicalOp(std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> right,
                           Schema left_schema, Schema right_schema, std::vector<binder::BoundType> output_types)
        : left_(std::move(left)), right_(std::move(right)), left_schema_(std::move(left_schema)),
          right_schema_(std::move(right_schema)), output_types_(std::move(output_types)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;

    std::string toString() const override {
        return "CrossProduct";
    }

    std::vector<const PhysicalOperator*> children() const override {
        return {left_.get(), right_.get()};
    }

private:
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
    Schema left_schema_;
    Schema right_schema_;
    std::vector<binder::BoundType> output_types_;
};

} // namespace compute
} // namespace eugraph
