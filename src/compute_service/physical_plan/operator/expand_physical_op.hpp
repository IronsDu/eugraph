#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/data_chunk.hpp"
#include "compute_service/parser/ast.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"
#include "compute_service/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class ExpandPhysicalOp : public PhysicalOperator {
public:
    ExpandPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var,
                     std::optional<std::vector<EdgeLabelId>> label_filters, cypher::RelationshipDirection direction,
                     IAsyncGraphDataStore& store, Schema input_schema, std::vector<binder::BoundType> output_types,
                     std::unique_ptr<PhysicalOperator> child)
        : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), edge_var_(std::move(edge_var)),
          label_filters_(std::move(label_filters)), direction_(direction), store_(store),
          input_schema_(std::move(input_schema)), output_types_(std::move(output_types)), child_(std::move(child)) {
        for (size_t i = 0; i < input_schema_.size(); ++i) {
            if (input_schema_[i] == src_var_) {
                src_col_idx_ = static_cast<int>(i);
                break;
            }
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
    std::string src_var_;
    std::string dst_var_;
    std::string edge_var_;
    std::optional<std::vector<EdgeLabelId>> label_filters_;
    cypher::RelationshipDirection direction_;
    IAsyncGraphDataStore& store_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    int src_col_idx_ = -1;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
