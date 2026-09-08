#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/operator/expand_physical_op.hpp"
#include "query/physical_plan/operator/index_scan_values_physical_op.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Correlated join used for `right.x.prop IN left.list` filters.
///
/// It executes the left child first, extracts the list column from each left
/// row, injects the list into the right child's filtered Expand, and emits
/// left-row x right-rows. The right child's Expand uses index probes for the
/// allowed destination values instead of scanning every neighbour.
class ListIndexJoinPhysicalOp : public PhysicalOperator {
public:
    ListIndexJoinPhysicalOp(std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> right,
                            ExpandPhysicalOp* filtered_expand, uint32_t left_list_col,
                            std::vector<binder::BoundType> output_types, Schema output_schema)
        : left_(std::move(left)), right_(std::move(right)), filtered_expand_(filtered_expand),
          left_list_col_(left_list_col), output_types_(std::move(output_types)),
          output_schema_(std::move(output_schema)) {}

    void setIndexScanSource(IndexScanValuesPhysicalOp* source) {
        filtered_source_ = source;
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "ListIndexJoin";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {left_.get(), right_.get()};
    }

private:
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
    ExpandPhysicalOp* filtered_expand_;
    IndexScanValuesPhysicalOp* filtered_source_ = nullptr;
    uint32_t left_list_col_;
    std::vector<binder::BoundType> output_types_;
    Schema output_schema_;
};

} // namespace compute
} // namespace eugraph
