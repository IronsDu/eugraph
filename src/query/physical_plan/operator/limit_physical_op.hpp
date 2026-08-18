#pragma once

#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace eugraph {
namespace compute {

class LimitPhysicalOp : public PhysicalOperator {
public:
    LimitPhysicalOp(std::optional<int64_t> limit, std::optional<binder::BoundExpression> expr,
                    std::unique_ptr<PhysicalOperator> child)
        : limit_(limit), expr_(std::move(expr)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    void compileExpressions(const TupleSlotLayout& input_layout) override;
    std::string toString() const override {
        return limit_.has_value() ? "Limit(" + std::to_string(*limit_) + ")" : "Limit(runtime expr)";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::optional<int64_t> limit_;
    std::optional<binder::BoundExpression> expr_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
