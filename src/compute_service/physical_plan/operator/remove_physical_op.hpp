#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/data_chunk.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

class RemovePhysicalOp : public PhysicalOperator {
public:
    struct BoundRemoveItem {
        enum class Kind {
            PROPERTY,
            LABEL
        };
        Kind kind;
        std::string var_name;
        std::string name;
    };

    RemovePhysicalOp(std::vector<BoundRemoveItem> items, Schema input_schema, IAsyncGraphDataStore& store,
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
        return "Remove(items=" + std::to_string(items_.size()) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::vector<BoundRemoveItem> items_;
    Schema input_schema_;
    IAsyncGraphDataStore& store_;
    const std::unordered_map<LabelId, LabelDef>& label_defs_;
    const std::unordered_map<std::string, LabelId>& label_name_to_id_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
