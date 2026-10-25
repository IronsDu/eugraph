#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

/// Correlated vertex probe for pattern-comprehension anchors.
///
/// Replaces `CrossProduct(CorrelatedSource(r), LabelScan(a))` in a PC: the
/// right side (the anchor label scan) is correlation-invariant, so it is
/// materialized into `VertexId -> rows` once. For every left row this operator
/// probes the edge's `startNode` and `endNode` vertex ids and emits only the
/// matching right rows. The original anchor/semantic Filters stay above this
/// operator as validation.
class CorrelatedVertexProbePhysicalOp : public PhysicalOperator {
public:
    CorrelatedVertexProbePhysicalOp(std::unique_ptr<PhysicalOperator> left, std::unique_ptr<PhysicalOperator> right,
                                    uint32_t left_edge_col, uint32_t right_vertex_col, Schema left_schema,
                                    Schema right_schema, std::vector<binder::BoundType> output_types)
        : left_(std::move(left)), right_(std::move(right)), left_edge_col_(left_edge_col),
          right_vertex_col_(right_vertex_col), left_schema_(std::move(left_schema)),
          right_schema_(std::move(right_schema)), output_types_(std::move(output_types)) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "CorrelatedVertexProbe";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {left_.get(), right_.get()};
    }

private:
    static constexpr size_t kMaxCachedRows = 65536;

    std::unique_ptr<PhysicalOperator> left_;
    std::unique_ptr<PhysicalOperator> right_;
    uint32_t left_edge_col_;
    uint32_t right_vertex_col_;
    Schema left_schema_;
    Schema right_schema_;
    std::vector<binder::BoundType> output_types_;

    bool cache_built_ = false;
    bool cache_disabled_ = false;
    std::unordered_map<VertexId, std::vector<Row>> by_vertex_;
};

} // namespace compute
} // namespace eugraph
