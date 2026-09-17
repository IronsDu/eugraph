#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"
#include "query/physical_plan/query_context.hpp"
#include "query/physical_plan/slot_layout.hpp"
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

    /// Whether this subtree can perform writes/DDL. LIMIT drains its child
    /// only when this returns true; pure reads can stop early.
    virtual bool mayHaveSideEffects() const {
        for (const auto* child : children()) {
            if (child && child->mayHaveSideEffects())
                return true;
        }
        return false;
    }

    /// Whether this operator preserves row count exactly (no filtering or
    /// deduplication), so an ancestor LIMIT can be pushed through it.
    virtual bool supportsLimitPushdown() const {
        return false;
    }

    /// Push a row bound from an ancestor LIMIT into this pure-read subtree.
    /// Only operators that preserve row count propagate the hint.
    virtual void setLimitHint(size_t limit) {
        if (!supportsLimitPushdown())
            return;
        for (const auto* child : children()) {
            if (child)
                const_cast<PhysicalOperator*>(child)->setLimitHint(limit);
        }
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

    void setSlotLayout(TupleSlotLayout layout) {
        slot_layout_ = std::move(layout);
    }
    const TupleSlotLayout& slotLayout() const {
        return slot_layout_;
    }

    /// Attach the statement's execution context to this operator and its whole
    /// subtree. Held by shared_ptr, so an operator can never observe a destroyed
    /// context no matter which order the tree is torn down in; the planner attaches
    /// it once, on the root.
    void setQueryContext(std::shared_ptr<QueryContext> ctx) {
        query_ctx_ = std::move(ctx);
        for (const auto* child : children()) {
            if (child)
                const_cast<PhysicalOperator*>(child)->setQueryContext(query_ctx_);
        }
    }
    const std::shared_ptr<QueryContext>& queryContext() const {
        return query_ctx_;
    }

    /// Compile expressions using the input layout from the child operator.
    /// Called after the physical tree is built, bottom-up (child before
    /// parent).  Each operator resolves its BoundColumnRef slot_ids to
    /// physical column_indices using the child's output layout.
    virtual void compileExpressions(const TupleSlotLayout& /*input_layout*/) {}
    /// After compilation, derive and store this operator's output layout.
    /// Default: same as input (passthrough).  Project / Aggregate override.
    virtual void deriveOutputLayout(const TupleSlotLayout& input_layout) {
        slot_layout_ = input_layout;
    }

protected:
    function::EvalContext eval_ctx_;
    TupleSlotLayout slot_layout_;

    /// True when this statement has been cancelled (client gone, deadline, ...).
    /// Operators check it as they process each batch of upstream chunks and each
    /// input row of an expansion; a cancelled operator just co_returns and the
    /// pull-based tree unwinds by itself.
    bool cancelled() const {
        return query_ctx_ && query_ctx_->cancelled();
    }

    /// Wrap a store generator so it stops as soon as the statement is cancelled:
    /// the one place cancellation is observed on the storage side of an operator, so
    /// no scan loop has to remember the check.
    ///
    /// Cancellation is checked here rather than inside the store on purpose -- a
    /// store instance is shared by every query of a graph, and query state does not
    /// belong on it. The cost is at most one more batch: the chunk already in
    /// flight is produced and dropped, then the operator returns and its generator
    /// is destroyed. (Two store generators do all their work inside the first
    /// next() -- scanAllVertices and the index scans, which collect their whole
    /// match set in one dispatch -- so those can overshoot by more than a batch;
    /// with a selective index that is still small, and it is the accepted price of
    /// keeping the store query-agnostic.)
    template <typename T> folly::coro::AsyncGenerator<T> cancellable(folly::coro::AsyncGenerator<T> gen) {
        while (auto item = co_await gen.next()) {
            if (cancelled())
                co_return;
            co_yield std::move(*item);
        }
    }

    /// Bridge for upgraded operators: wraps executeChunk() output as RowBatch.
    /// Use for the legacy execute() override:
    ///   folly::coro::AsyncGenerator<RowBatch> execute() override { return executeViaChunk(); }
    folly::coro::AsyncGenerator<RowBatch> executeViaChunk();

private:
    Schema output_schema_;
    std::vector<binder::BoundType> output_types_;
    /// Per-statement execution state; null for operators built outside a statement
    /// (unit tests), where cancelled() is then simply false.
    std::shared_ptr<QueryContext> query_ctx_;
};

// ── Conversion utilities (used by default bridge and DDL/EXPLAIN paths) ──

/// Convert a RowBatch to DataChunk. Infers column types from first non-null value.
DataChunk rowBatchToDataChunk(const RowBatch& batch);

/// Convert DataChunk to RowBatch (legacy execute() bridge).
RowBatch dataChunkToRowBatch(const DataChunk& chunk);

/// Wrap an AsyncGenerator<RowBatch> as AsyncGenerator<DataChunk>.
folly::coro::AsyncGenerator<DataChunk> wrapRowBatchToChunkGenerator(folly::coro::AsyncGenerator<RowBatch> gen);

} // namespace compute
} // namespace eugraph
