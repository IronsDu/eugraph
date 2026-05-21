#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/parser/ast.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

class SetPhysicalOp : public PhysicalOperator {
public:
    struct BoundSetItem {
        cypher::SetItemKind kind;
        std::string var_name;
        std::string prop_name;
        std::string label;
        std::optional<binder::BoundExpression> value;
    };

    SetPhysicalOp(std::vector<BoundSetItem> items, Schema input_schema, IAsyncGraphDataStore& store,
                  const std::unordered_map<LabelId, LabelDef>& label_defs,
                  const std::unordered_map<std::string, LabelId>& label_name_to_id,
                  std::unique_ptr<PhysicalOperator> child)
        : items_(std::move(items)), input_schema_(std::move(input_schema)), store_(store), label_defs_(label_defs),
          label_name_to_id_(label_name_to_id), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Set(items=" + std::to_string(items_.size()) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::vector<BoundSetItem> items_;
    Schema input_schema_;
    IAsyncGraphDataStore& store_;
    const std::unordered_map<LabelId, LabelDef>& label_defs_;
    const std::unordered_map<std::string, LabelId>& label_name_to_id_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
