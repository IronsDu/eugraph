#pragma once

#include "query/executor/data_chunk.hpp"
#include "query/executor/row.hpp"
#include "query/function/function_def.hpp"
#include "query/planner/bound_type.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

// ==================== Physical Operator Base ====================

class PhysicalOperator {
public:
    virtual ~PhysicalOperator() = default;

    /// Legacy execute interface: yields row-based batches.
    /// Operators override this for backward compatibility during migration.
    virtual folly::coro::AsyncGenerator<RowBatch> execute() = 0;

    /// Columnar execute interface: yields DataChunk batches.
    /// Default implementation bridges from execute() (RowBatch→DataChunk).
    /// Operators override this to produce DataChunk natively.
    virtual folly::coro::AsyncGenerator<DataChunk> executeChunk();

    virtual std::string toString() const = 0;
    virtual std::vector<const PhysicalOperator*> children() const {
        return {};
    }

    void setOutputSchema(Schema schema, std::vector<binder::BoundType> types) {
        output_schema_ = std::move(schema);
        output_types_ = std::move(types);
    }

    const Schema& outputSchema() const {
        return output_schema_;
    }
    const std::vector<binder::BoundType>& outputTypes() const {
        return output_types_;
    }

    void setEvalContext(const function::EvalContext& ctx) {
        eval_ctx_ = ctx;
    }

protected:
    function::EvalContext eval_ctx_;

    /// Bridge for upgraded operators: wraps executeChunk() output as RowBatch.
    /// Use for the legacy execute() override:
    ///   folly::coro::AsyncGenerator<RowBatch> execute() override { return executeViaChunk(); }
    folly::coro::AsyncGenerator<RowBatch> executeViaChunk();

private:
    Schema output_schema_;
    std::vector<binder::BoundType> output_types_;
};

// ── Conversion utilities (used by default bridge and DDL/EXPLAIN paths) ──

/// Convert a RowBatch to DataChunk. Infers column types from first non-null value.
DataChunk rowBatchToDataChunk(const RowBatch& batch);

/// Convert DataChunk to RowBatch (delegates to DataChunk::toRows()).
RowBatch dataChunkToRowBatch(const DataChunk& chunk);

/// Wrap an AsyncGenerator<RowBatch> as AsyncGenerator<DataChunk>.
folly::coro::AsyncGenerator<DataChunk> wrapRowBatchToChunkGenerator(folly::coro::AsyncGenerator<RowBatch> gen);

} // namespace compute
} // namespace eugraph
