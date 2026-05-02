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

class AggregatePhysicalOp : public PhysicalOperator {
public:
    struct GroupKey {
        cypher::Expression expr;
        std::string name;
    };

    struct AggregateExpr {
        std::string func_name; // "count", "sum", "avg", "min", "max"
        cypher::Expression arg;
        bool distinct;
        std::string name;
    };

    AggregatePhysicalOp(std::vector<GroupKey> group_keys, std::vector<AggregateExpr> aggregates,
                        std::unique_ptr<PhysicalOperator> child, Schema input_schema,
                        const std::unordered_map<LabelId, LabelDef>& label_defs)
        : group_keys_(std::move(group_keys)), aggregates_(std::move(aggregates)), child_(std::move(child)),
          input_schema_(std::move(input_schema)), evaluator_(label_defs) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
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
    Schema input_schema_;
    ExpressionEvaluator evaluator_;
};

} // namespace compute
} // namespace eugraph
