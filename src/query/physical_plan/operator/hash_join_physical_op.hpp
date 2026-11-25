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
                       std::vector<binder::BoundType> output_types, Schema output_schema, bool build_left = false,
                       size_t max_build_rows = 0)
        : left_(std::move(left)), right_(std::move(right)), left_key_cols_(std::move(left_key_cols)),
          right_key_cols_(std::move(right_key_cols)), output_types_(std::move(output_types)),
          output_schema_(std::move(output_schema)), build_left_(build_left), max_build_rows_(max_build_rows) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "HashJoin";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {left_.get(), right_.get()};
    }

    void explain(ExplainNode& node, const ExplainContext&) const override {
        node.operator_name = "HashJoin";
        node.details.push_back(std::string("build=") + (build_left_ ? "left" : "right"));
        std::string keys;
        const auto& left_names = left_->outputSchema();
        const auto& right_names = right_->outputSchema();
        for (size_t i = 0; i < left_key_cols_.size() && i < right_key_cols_.size(); ++i) {
            if (i > 0)
                keys += " AND ";
            auto l = left_key_cols_[i] < left_names.size() ? explainVariableName(left_names[left_key_cols_[i]])
                                                           : "#" + std::to_string(left_key_cols_[i]);
            auto r = right_key_cols_[i] < right_names.size() ? explainVariableName(right_names[right_key_cols_[i]])
                                                             : "#" + std::to_string(right_key_cols_[i]);
            keys += l + " = " + r;
        }
        if (!keys.empty())
            node.details.push_back("keys=[" + keys + "]");
        if (max_build_rows_ > 0)
            node.details.push_back("max_build_rows=" + std::to_string(max_build_rows_));
    }

private:
    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
    std::vector<uint32_t> left_key_cols_;
    std::vector<uint32_t> right_key_cols_;
    std::vector<binder::BoundType> output_types_;
    Schema output_schema_;
    /// true: 物化 left（小表）并探测 right；默认物化 right。
    bool build_left_ = false;
    /// 0 = 不限；>0 时构建行数超限直接失败，避免无界内存（planner 应先用估算拦截）。
    size_t max_build_rows_ = 0;
};

} // namespace compute
} // namespace eugraph
