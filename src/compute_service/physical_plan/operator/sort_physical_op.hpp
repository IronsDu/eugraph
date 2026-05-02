#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/expression_evaluator.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/parser/ast.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

class SortPhysicalOp : public PhysicalOperator {
public:
    struct SortItem {
        cypher::Expression expr;
        cypher::OrderBy::Direction direction;
    };

    SortPhysicalOp(std::vector<SortItem> sort_items, Schema input_schema, std::unique_ptr<PhysicalOperator> child,
                   const std::unordered_map<LabelId, LabelDef>& label_defs)
        : sort_items_(std::move(sort_items)), input_schema_(std::move(input_schema)), child_(std::move(child)),
          evaluator_(label_defs) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "Sort(items=" + std::to_string(sort_items_.size()) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::vector<SortItem> sort_items_;
    Schema input_schema_;
    std::unique_ptr<PhysicalOperator> child_;
    ExpressionEvaluator evaluator_;
};

} // namespace compute
} // namespace eugraph
