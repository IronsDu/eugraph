#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class IndexScanValuesPhysicalOp : public PhysicalOperator {
public:
    IndexScanValuesPhysicalOp(std::string variable, uint32_t index_id, IAsyncGraphDataStore& store,
                              std::vector<binder::BoundType> output_types, Schema output_schema)
        : variable_(std::move(variable)), index_id_(index_id), store_(store), output_types_(std::move(output_types)),
          output_schema_(std::move(output_schema)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "IndexScanValues(variable=" + variable_ + ", index_id=" + std::to_string(index_id_) + ")";
    }
    void setValues(std::shared_ptr<std::vector<PropertyValue>> values) {
        values_ = std::move(values);
    }

private:
    std::string variable_;
    uint32_t index_id_;
    IAsyncGraphDataStore& store_;
    std::vector<binder::BoundType> output_types_;
    Schema output_schema_;
    std::shared_ptr<std::vector<PropertyValue>> values_;
};

} // namespace compute
} // namespace eugraph
