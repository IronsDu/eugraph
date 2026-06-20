#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

/// Batch property extractor for vertex variables. Takes a VertexRef column
/// and appends flat typed columns (one per requested property) without
/// constructing a VertexValue object.
class VertexPropertyExtractPhysicalOp : public PhysicalOperator {
public:
    VertexPropertyExtractPhysicalOp(std::string variable, size_t col_idx,
                                    std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids,
                                    IAsyncGraphDataStore& store, Schema input_schema,
                                    std::vector<binder::BoundType> output_types,
                                    std::unique_ptr<PhysicalOperator> child)
        : variable_(std::move(variable)), col_idx_(col_idx), label_prop_ids_(std::move(label_prop_ids)), store_(store),
          input_schema_(std::move(input_schema)), output_types_(std::move(output_types)), child_(std::move(child)) {
        // Build a flattened prop list: (label_id, prop_id) for each extracted property.
        for (const auto& [lid, pids] : label_prop_ids_) {
            for (uint16_t pid : pids)
                prop_list_.emplace_back(lid, pid);
        }
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string variable_;
    size_t col_idx_;
    std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids_;
    std::vector<std::pair<LabelId, uint16_t>> prop_list_;
    IAsyncGraphDataStore& store_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
