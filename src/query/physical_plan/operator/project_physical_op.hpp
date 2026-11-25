#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/evaluator/expression_evaluator.hpp"
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
        for (auto& item : items_) {
            if (auto* ref = std::get_if<binder::BoundColumnRef>(&item.expr)) {
                if (ref->column_index >= input_layout.size()) {
                    auto pos = input_layout.getColumnIndex(ref->slot_id);
                    if (pos < 0) {
                        for (size_t ci = 0; ci < input_schema_.size(); ++ci) {
                            if (input_schema_[ci] == ref->name) {
                                pos = static_cast<int>(ci);
                                break;
                            }
                        }
                    }
                    if (pos >= 0)
                        ref->column_index = static_cast<uint32_t>(pos);
                }
            }
            compiler.compile(item.expr);
        }
    }
    void deriveOutputLayout(const TupleSlotLayout& input_layout) override {
        // For WITH/RETURN: output slots come from the projected expressions.
        // Each item contributes its output column's slot.
        // For now, use the input layout unmodified (passthrough).
        // TODO: build output layout from items' BoundColumnRef slot_ids.
        slot_layout_ = input_layout;
    }

    void explain(ExplainNode& node, const ExplainContext& ctx) const override {
        node.operator_name = "Project";
        std::vector<std::string> items;
        for (const auto& item : items_) {
            const bool internal = explainIsInternalAlias(item.name);
            std::string expr = formatExplainExpression(item.expr, ctx);
            // Property reads are already lowered to internal refs by PE. Keep the
            // user-facing alias when it names a property, instead of showing an
            // opaque input column such as "p AS p.id".
            if (std::holds_alternative<binder::BoundColumnRef>(item.expr) && !item.name.empty() &&
                item.name.find('.') != std::string::npos)
                expr = item.name;
            items.push_back(expr + " AS " + explainVariableName(item.name) + (internal ? " [internal]" : ""));
        }
        std::string mapping;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0)
                mapping += ", ";
            mapping += items[i];
        }
        node.details.push_back("mapping=[" + mapping + "]");
    }

private:
    std::vector<ProjectItem> items_;
    Schema input_schema_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
