#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <string>
#include <vector>

namespace eugraph {
class IAsyncGraphDataStore;
namespace function {
class FunctionRegistry;
}
namespace compute {

class CallPhysicalOp : public PhysicalOperator {
public:
    CallPhysicalOp(std::string procedure_name, std::vector<std::string> output_names,
                   std::vector<binder::BoundType> output_types, IAsyncGraphDataStore* data_store = nullptr,
                   IAsyncGraphMetaStore* meta = nullptr, const function::FunctionRegistry* func_registry = nullptr)
        : procedure_name_(std::move(procedure_name)), output_names_(std::move(output_names)),
          output_types_(std::move(output_types)), data_store_(data_store), meta_(meta), func_registry_(func_registry) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;

    std::string toString() const override {
        return "Call(" + procedure_name_ + ")";
    }

private:
    std::string procedure_name_;
    std::vector<std::string> output_names_;
    std::vector<binder::BoundType> output_types_;
    IAsyncGraphDataStore* data_store_ = nullptr;
    IAsyncGraphMetaStore* meta_ = nullptr;
    const function::FunctionRegistry* func_registry_ = nullptr;
};

} // namespace compute
} // namespace eugraph
