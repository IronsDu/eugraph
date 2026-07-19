#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class DeletePhysicalOp : public PhysicalOperator {
public:
    enum class TargetKind {
        VERTEX,
        EDGE
    };
    struct DeleteTarget {
        TargetKind kind;
        std::string var_name;
        /// Physical column holding the target's constructed VertexValue /
        /// EdgeValue, resolved at plan time from the child's slot layout +
        /// PEPlan (append-only ProjectionExtract puts the object in a separate
        /// column from the source VertexRef/EdgeKey). -1 → fall back to
        /// name-based lookup.
        int object_col = -1;
    };

    DeletePhysicalOp(std::vector<DeleteTarget> targets, bool detach, Schema input_schema, IAsyncGraphDataStore& store,
                     std::unique_ptr<PhysicalOperator> child)
        : targets_(std::move(targets)), detach_(detach), input_schema_(std::move(input_schema)), store_(store),
          child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Delete(targets=" + std::to_string(targets_.size()) + (detach_ ? ", detach" : "") + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::vector<DeleteTarget> targets_;
    bool detach_;
    Schema input_schema_;
    IAsyncGraphDataStore& store_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
