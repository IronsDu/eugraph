#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

class CreateNodePhysicalOp : public PhysicalOperator {
public:
    CreateNodePhysicalOp(std::string variable, std::vector<LabelId> label_ids,
                         std::vector<std::pair<LabelId, Properties>> label_props, IAsyncGraphDataStore& store,
                         VertexId assigned_vid, std::unique_ptr<PhysicalOperator> child = nullptr,
                         std::unordered_map<LabelId, LabelDef> label_defs = {})
        : variable_(std::move(variable)), label_ids_(std::move(label_ids)), label_props_(std::move(label_props)),
          store_(store), assigned_vid_(assigned_vid), child_(std::move(child)), label_defs_(std::move(label_defs)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        if (child_)
            return {child_.get()};
        return {};
    }

private:
    std::string variable_;
    std::vector<LabelId> label_ids_;
    std::vector<std::pair<LabelId, Properties>> label_props_;
    IAsyncGraphDataStore& store_;
    VertexId assigned_vid_;
    std::unique_ptr<PhysicalOperator> child_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
};

} // namespace compute
} // namespace eugraph
