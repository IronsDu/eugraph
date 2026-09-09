#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

/// Hash join on equality of scalar columns. Builds a hash table from the
/// right child and probes with the left child. Emits left-row x matching
/// right-row; unmatched rows are dropped.
class HashJoinPhysicalOp : public PhysicalOperator {
public:
    HashJoinPhysicalOp(std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> right,
                       std::vector<uint32_t> left_key_cols, std::vector<uint32_t> right_key_cols,
                       std::vector<binder::BoundType> output_types, Schema output_schema)
        : left_(std::move(left)), right_(std::move(right)), left_key_cols_(std::move(left_key_cols)),
          right_key_cols_(std::move(right_key_cols)), output_types_(std::move(output_types)),
          output_schema_(std::move(output_schema)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "HashJoin";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {left_.get(), right_.get()};
    }

private:
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
    std::vector<uint32_t> left_key_cols_;
    std::vector<uint32_t> right_key_cols_;
    std::vector<binder::BoundType> output_types_;
    Schema output_schema_;
};

} // namespace compute
} // namespace eugraph
