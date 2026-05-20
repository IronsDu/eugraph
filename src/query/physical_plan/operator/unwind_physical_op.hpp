#pragma once

#include "query/executor/data_chunk.hpp"
#include "query/executor/vectorized_evaluator.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class UnwindPhysicalOp : public PhysicalOperator {
public:
    UnwindPhysicalOp(binder::BoundExpression list_expr, uint32_t output_col_index, binder::BoundType elem_type,
                     Schema input_schema, std::vector<binder::BoundType> output_types,
                     std::unique_ptr<PhysicalOperator> child)
        : list_expr_(std::move(list_expr)), output_col_index_(output_col_index), elem_type_(std::move(elem_type)),
          input_schema_(std::move(input_schema)), output_types_(std::move(output_types)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Unwind";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    binder::BoundExpression list_expr_;
    uint32_t output_col_index_;
    binder::BoundType elem_type_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
