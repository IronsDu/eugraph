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

/// Batch property extractor for path variables. Takes a PathTopology column
/// and extracts specified properties from vertices and/or edges along the
/// path, appending flat List columns (one per property group) without
/// constructing PathValue objects.
class PathPropertyExtractPhysicalOp : public PhysicalOperator {
public:
    /// vertex_props: per-label property IDs to extract from ALL vertices in the path.
    /// edge_props: per-label property IDs to extract from ALL edges in the path.
    PathPropertyExtractPhysicalOp(std::string path_variable, size_t path_col_idx,
                                  std::unordered_map<LabelId, std::vector<uint16_t>> vertex_props,
                                  std::unordered_map<LabelId, std::vector<uint16_t>> edge_props,
                                  IAsyncGraphDataStore& store, Schema input_schema,
                                  std::vector<binder::BoundType> output_types, std::unique_ptr<PhysicalOperator> child)
        : path_variable_(std::move(path_variable)), path_col_idx_(path_col_idx), vertex_props_(std::move(vertex_props)),
          edge_props_(std::move(edge_props)), store_(store), input_schema_(std::move(input_schema)),
          output_types_(std::move(output_types)), child_(std::move(child)) {
        for (const auto& [lid, pids] : vertex_props_)
            for (uint16_t pid : pids)
                vertex_prop_list_.emplace_back(lid, pid);
        for (const auto& [lid, pids] : edge_props_)
            for (uint16_t pid : pids)
                edge_prop_list_.emplace_back(lid, pid);
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
    std::string path_variable_;
    size_t path_col_idx_;
    std::unordered_map<LabelId, std::vector<uint16_t>> vertex_props_;
    std::unordered_map<LabelId, std::vector<uint16_t>> edge_props_;
    std::vector<std::pair<LabelId, uint16_t>> vertex_prop_list_;
    std::vector<std::pair<LabelId, uint16_t>> edge_prop_list_;
    IAsyncGraphDataStore& store_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
