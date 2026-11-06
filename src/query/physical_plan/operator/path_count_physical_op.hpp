#pragma once

#include "query/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

/// Counts the number of extensions of a fixed OUT-going path without
/// materialising the final Expand rows.
///
/// Input is the prefix plan after N-1 hops (including its prefix uniqueness
/// filters). For each prefix row, the final hop has:
///
///   outdegree(current) - number of previous edges whose source == current
///
/// choices. The subtraction enforces relationship uniqueness for the final
/// edge. This operator is deliberately restricted by the physical planner to
/// untyped/all-label OUT expansions with no destination constraint.
class PathCountPhysicalOp : public PhysicalOperator {
public:
    PathCountPhysicalOp(std::unique_ptr<PhysicalOperator> child, IAsyncGraphDataStore& store, uint32_t source_column,
                        std::vector<uint32_t> previous_edge_columns, std::vector<EdgeLabelId> edge_labels)
        : child_(std::move(child)), store_(store), source_column_(source_column),
          previous_edge_columns_(std::move(previous_edge_columns)), edge_labels_(std::move(edge_labels)) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;

    std::string toString() const override {
        return "PathCount";
    }

    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::unique_ptr<PhysicalOperator> child_;
    IAsyncGraphDataStore& store_;
    uint32_t source_column_;
    std::vector<uint32_t> previous_edge_columns_;
    std::vector<EdgeLabelId> edge_labels_;
};

} // namespace compute
} // namespace eugraph
