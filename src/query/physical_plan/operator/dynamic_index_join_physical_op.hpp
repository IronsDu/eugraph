#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/explain/explain.hpp"
#include "query/physical_plan/operator/index_scan_values_physical_op.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Dynamic IndexJoin: consumes its source exactly once, using that pass to
/// build the source hash table and collect the distinct index keys. The reverse
/// index chain then probes the same hash table. This replaces the former
/// ReexecuteSource + Apply + HashJoin composition, which called the source a
/// second time and had no explicit re-executability contract.
///
/// Current implementation is in-memory bounded by max_build_rows; the
/// SpillableRowStore follow-up will replace the payload store without changing
/// this operator's external contract.
class DynamicIndexJoinPhysicalOp final : public PhysicalOperator {
public:
    DynamicIndexJoinPhysicalOp(std::unique_ptr<PhysicalOperator> source, std::unique_ptr<PhysicalOperator> index_chain,
                               IndexScanValuesPhysicalOp* index_scan, uint32_t source_key_col, uint32_t dynamic_key_col,
                               uint32_t match_key_col, Schema source_schema, Schema match_schema,
                               std::vector<binder::BoundType> output_types, Schema output_schema,
                               size_t max_build_rows);

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "DynamicIndexJoin";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {source_.get(), index_chain_.get()};
    }
    void explain(ExplainNode& node, const ExplainContext& ctx) const override;

private:
    std::unique_ptr<PhysicalOperator> source_;
    std::unique_ptr<PhysicalOperator> index_chain_;
    IndexScanValuesPhysicalOp* index_scan_ = nullptr;
    uint32_t source_key_col_ = 0;
    uint32_t dynamic_key_col_ = 0;
    uint32_t match_key_col_ = 0;
    Schema source_schema_;
    Schema match_schema_;
    std::vector<binder::BoundType> output_types_;
    Schema output_schema_;
    size_t max_build_rows_ = 0;
};

} // namespace compute
} // namespace eugraph
