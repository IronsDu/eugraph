#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"
#include "compute_service/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

class LabelScanPhysicalOp : public PhysicalOperator {
public:
    LabelScanPhysicalOp(std::string variable, LabelId label_id, std::vector<binder::BoundType> output_types,
                        IAsyncGraphDataStore& store, std::unordered_map<LabelId, LabelDef> label_defs)
        : variable_(std::move(variable)), label_id_(label_id), output_types_(std::move(output_types)), store_(store),
          label_defs_(std::move(label_defs)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        auto it = label_defs_.find(label_id_);
        std::string label_name = (it != label_defs_.end()) ? it->second.name : std::to_string(label_id_);
        return "LabelScan(variable=" + variable_ + ", label=" + label_name + ")";
    }

private:
    std::string variable_;
    LabelId label_id_;
    std::vector<binder::BoundType> output_types_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
};

} // namespace compute
} // namespace eugraph
