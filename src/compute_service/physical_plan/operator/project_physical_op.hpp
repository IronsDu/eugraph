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

class ProjectPhysicalOp : public PhysicalOperator {
public:
    struct ProjectItem {
        cypher::Expression expr;
        std::string name; // output column name
    };

    ProjectPhysicalOp(std::vector<ProjectItem> items, Schema input_schema, std::unique_ptr<PhysicalOperator> child,
                      const std::unordered_map<LabelId, LabelDef>& label_defs)
        : items_(std::move(items)), input_schema_(std::move(input_schema)), child_(std::move(child)),
          evaluator_(label_defs) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }
    const std::vector<ProjectItem>& items() const {
        return items_;
    }

private:
    std::vector<ProjectItem> items_;
    Schema input_schema_;
    std::unique_ptr<PhysicalOperator> child_;
    ExpressionEvaluator evaluator_;
};

} // namespace compute
} // namespace eugraph
