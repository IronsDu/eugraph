#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/function/function_def.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class AggregatePhysicalOp : public PhysicalOperator {
public:
    struct GroupKey {
        binder::BoundExpression expr;
        std::string name;
    };

    struct AggregateExpr {
        const function::FunctionDef* func_def = nullptr;
        binder::BoundExpression arg;
        bool distinct;
        std::string name;
    };

    AggregatePhysicalOp(std::vector<GroupKey> group_keys, std::vector<AggregateExpr> aggregates,
                        std::unique_ptr<PhysicalOperator> child,
                        std::vector<binder::BoundTypeKind> output_type_kinds = {})
        : group_keys_(std::move(group_keys)), aggregates_(std::move(aggregates)), child_(std::move(child)),
          output_type_kinds_(std::move(output_type_kinds)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;

    std::string toString() const override {
        return "Aggregate(keys=" + std::to_string(group_keys_.size()) + ", aggs=" + std::to_string(aggregates_.size()) +
               ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::vector<GroupKey> group_keys_;
    std::vector<AggregateExpr> aggregates_;
    std::unique_ptr<PhysicalOperator> child_;
    std::vector<binder::BoundTypeKind> output_type_kinds_;
};

} // namespace compute
} // namespace eugraph
