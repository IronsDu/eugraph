#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/binder/bound_expression/bound_expression.hpp"
#include "compute_service/executor/data_chunk.hpp"
#include "compute_service/executor/vectorized_evaluator.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>

namespace eugraph {
namespace compute {

class FilterPhysicalOp : public PhysicalOperator {
public:
    FilterPhysicalOp(binder::BoundExpression predicate, Schema schema, std::unique_ptr<PhysicalOperator> child)
        : predicate_(std::move(predicate)), schema_(std::move(schema)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Filter";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    binder::BoundExpression predicate_;
    Schema schema_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
