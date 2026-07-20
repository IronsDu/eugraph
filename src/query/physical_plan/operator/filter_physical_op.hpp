#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/physical_plan/expression_compiler.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

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

    void compileExpressions(const TupleSlotLayout& input_layout) override {
        ExpressionCompiler compiler(input_layout);
        compiler.compile(predicate_);
    }

private:
    binder::BoundExpression predicate_;
    Schema schema_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
