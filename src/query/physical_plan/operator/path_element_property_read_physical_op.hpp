#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>

namespace eugraph {
namespace compute {

class PathElementPropertyReadPhysicalOp : public PhysicalOperator {
public:
    PathElementPropertyReadPhysicalOp(std::string path_variable, size_t path_col_idx, IAsyncGraphDataStore& store,
                                      Schema input_schema, std::vector<binder::BoundType> output_types,
                                      std::unique_ptr<PhysicalOperator> child)
        : path_variable_(std::move(path_variable)), path_col_idx_(path_col_idx), store_(store),
          input_schema_(std::move(input_schema)), output_types_(std::move(output_types)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string path_variable_;
    size_t path_col_idx_;
    IAsyncGraphDataStore& store_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
