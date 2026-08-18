#pragma once

#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace eugraph {
namespace compute {

class SkipPhysicalOp : public PhysicalOperator {
public:
    SkipPhysicalOp(std::optional<int64_t> skip, std::optional<binder::BoundExpression> expr,
                   std::unique_ptr<PhysicalOperator> child)
        : skip_(skip), expr_(std::move(expr)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    void compileExpressions(const TupleSlotLayout& input_layout) override;
    std::string toString() const override {
        return skip_.has_value() ? "Skip(" + std::to_string(*skip_) + ")" : "Skip(runtime expr)";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::optional<int64_t> skip_;
    std::optional<binder::BoundExpression> expr_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
