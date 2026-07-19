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
#include <vector>

namespace eugraph {
namespace compute {

class ProjectPhysicalOp : public PhysicalOperator {
public:
    struct ProjectItem {
        binder::BoundExpression expr;
        std::string name; // output column name
    };

    ProjectPhysicalOp(std::vector<ProjectItem> items, Schema input_schema, std::unique_ptr<PhysicalOperator> child)
        : items_(std::move(items)), input_schema_(std::move(input_schema)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }
    const std::vector<ProjectItem>& items() const {
        return items_;
    }
    std::vector<ProjectItem>& items() {
        return items_;
    }

    void compileExpressions(const TupleSlotLayout& input_layout) override {
        ExpressionCompiler compiler(input_layout);
        for (auto& item : items_)
            compiler.compile(item.expr);
    }
    void deriveOutputLayout(const TupleSlotLayout& input_layout) override {
        // For WITH/RETURN: output slots come from the projected expressions.
        // Each item contributes its output column's slot.
        // For now, use the input layout unmodified (passthrough).
        // TODO: build output layout from items' BoundColumnRef slot_ids.
        slot_layout_ = input_layout;
    }

private:
    std::vector<ProjectItem> items_;
    Schema input_schema_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
