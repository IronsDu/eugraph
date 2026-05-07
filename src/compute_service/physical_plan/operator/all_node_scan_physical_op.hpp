#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/binder/bound_type.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

class AllNodeScanPhysicalOp : public PhysicalOperator {
public:
    AllNodeScanPhysicalOp(std::string variable, std::vector<binder::BoundType> output_types,
                          IAsyncGraphDataStore& store, const std::unordered_map<std::string, LabelId>& label_map,
                          std::unordered_map<LabelId, LabelDef> label_defs)
        : variable_(std::move(variable)), output_types_(std::move(output_types)), store_(store), label_map_(label_map),
          label_defs_(std::move(label_defs)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "AllNodeScan(variable=" + variable_ + ")";
    }

private:
    std::string variable_;
    std::vector<binder::BoundType> output_types_;
    IAsyncGraphDataStore& store_;
    const std::unordered_map<std::string, LabelId>& label_map_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
};

} // namespace compute
} // namespace eugraph
