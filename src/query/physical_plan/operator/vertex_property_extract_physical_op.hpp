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

/// Unified vertex loader. Replaces VertexPropertyReadPhysicalOp and
/// VertexLabelReadPhysicalOp.  The request array determines what to load:
///
///   LabelRequest   → List<String> column (vertex labels)
///   PropRequest    → typed scalar column (single property)
///   VertexRequest  → VertexValue column (full vertex: labels + all properties)
///
/// Input:  upstream columns, some carrying VertexRef / VertexValue
/// Output: upstream columns + requested columns
class VertexPropertyExtractPhysicalOp : public PhysicalOperator {
public:
    struct LabelRequest {
        size_t source_col;
    };
    struct PropRequest {
        size_t source_col;
        LabelId label_id;
        uint16_t prop_id;
    };
    struct VertexRequest {
        size_t source_col;
    };

    VertexPropertyExtractPhysicalOp(std::string variable, std::vector<LabelRequest> label_requests,
                                    std::vector<PropRequest> prop_requests, std::vector<VertexRequest> vertex_requests,
                                    IAsyncGraphDataStore& store,
                                    std::unordered_map<LabelId, std::string> label_id_to_name, Schema input_schema,
                                    std::vector<binder::BoundType> output_types,
                                    std::unique_ptr<PhysicalOperator> child)
        : variable_(std::move(variable)), label_requests_(std::move(label_requests)),
          prop_requests_(std::move(prop_requests)), vertex_requests_(std::move(vertex_requests)), store_(store),
          label_id_to_name_(std::move(label_id_to_name)), input_schema_(std::move(input_schema)),
          output_types_(std::move(output_types)), child_(std::move(child)) {}

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
    std::vector<LabelRequest> label_requests_;
    std::vector<PropRequest> prop_requests_;
    std::vector<VertexRequest> vertex_requests_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, std::string> label_id_to_name_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
