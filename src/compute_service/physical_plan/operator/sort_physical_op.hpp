#pragma once

#include "compute_service/executor/data_chunk.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"
#include "compute_service/planner/bound_expression/bound_expression.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class SortPhysicalOp : public PhysicalOperator {
public:
    struct SortItem {
        binder::BoundExpression expr;
        bool ascending = true;
    };

    SortPhysicalOp(std::vector<SortItem> sort_items, std::unique_ptr<PhysicalOperator> child)
        : sort_items_(std::move(sort_items)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;

    std::string toString() const override {
        return "Sort(items=" + std::to_string(sort_items_.size()) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::vector<SortItem> sort_items_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
