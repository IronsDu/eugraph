#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/expression_compiler.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

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

    void compileExpressions(const TupleSlotLayout& input_layout) override {
        ExpressionCompiler compiler(input_layout);
        for (auto& item : sort_items_)
            compiler.compile(item.expr);
    }

private:
    std::vector<SortItem> sort_items_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
