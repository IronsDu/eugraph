#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/expression_evaluator.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/parser/ast.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <unordered_map>

namespace eugraph {
namespace compute {

class FilterPhysicalOp : public PhysicalOperator {
public:
    FilterPhysicalOp(cypher::Expression predicate, Schema schema, std::unique_ptr<PhysicalOperator> child,
                     const std::unordered_map<LabelId, LabelDef>& label_defs)
        : predicate_(std::move(predicate)), schema_(std::move(schema)), child_(std::move(child)),
          evaluator_(label_defs) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "Filter";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    cypher::Expression predicate_;
    Schema schema_;
    std::unique_ptr<PhysicalOperator> child_;
    ExpressionEvaluator evaluator_;
};

} // namespace compute
} // namespace eugraph
