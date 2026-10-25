#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <vector>

namespace eugraph {
namespace compute {

/// Caches a re-executable, correlation-invariant child subtree.
///
/// Pattern-comprehension anchors are planned as
/// `CrossProduct(CorrelatedSource(r), LabelScan(Person))`: the right side is
/// invariant but CrossProduct restarts it for every outer row, so an anchor
/// label scan runs once per PC evaluation. Wrapping the invariant side in this
/// operator materializes it on first use and replays it from memory.
///
/// Only used for small scan subtrees; `kMaxCachedRows` bounds the cache. If
/// the child exceeds it, caching is disabled and the child is streamed
/// normally (the child is a read-only scan, so re-execution is safe).
class MaterializePhysicalOp : public PhysicalOperator {
public:
    MaterializePhysicalOp(std::unique_ptr<PhysicalOperator> child, Schema output_schema,
                          std::vector<binder::BoundType> output_types)
        : child_(std::move(child)), output_schema_(std::move(output_schema)), output_types_(std::move(output_types)) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Materialize(cached invariant scan)";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    static constexpr size_t kMaxCachedRows = 65536;

    std::unique_ptr<PhysicalOperator> child_;
    Schema output_schema_;
    std::vector<binder::BoundType> output_types_;
    bool cached_ = false;
    bool disabled_ = false;
    std::vector<Row> rows_;

    folly::coro::AsyncGenerator<DataChunk> replay();
};

} // namespace compute
} // namespace eugraph
