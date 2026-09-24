#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/evaluator/expression_evaluator.hpp"
#include "query/physical_plan/expression_compiler.hpp"
#include "query/physical_plan/operator/correlated_source_physical_op.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

/// FOREACH (variable IN list | body...)
///
/// For every input row this evaluates the list, then runs the body sub-plan once
/// per element with the correlated values injected into its CorrelatedSource leaf.
/// The body's output rows are discarded -- only its side effects matter -- and the
/// input row is handed through untouched, so cardinality is preserved (the one
/// thing UNWIND cannot express: UNWIND multiplies rows).
///
/// Element handling follows neo4j:
///   * a list iterates its elements in order;
///   * null is a no-op;
///   * any other value behaves like a one-element list (so `FOREACH (x IN 1 | ...)`
///     runs once with x = 1 rather than failing).
///
/// The correlated source's columns are ordered "outer variables..., then the
/// element", so `input_columns_[i]` feeds correlated column i and the element is
/// appended as the last one (`element_column_` is that last index).
class ForeachPhysicalOp : public PhysicalOperator {
public:
    ForeachPhysicalOp(binder::BoundExpression list_expr, std::vector<uint32_t> input_columns, uint32_t element_column,
                      std::unique_ptr<PhysicalOperator> body, CorrelatedSourcePhysicalOp* correlated_source,
                      std::unique_ptr<PhysicalOperator> child)
        : list_expr_(std::move(list_expr)), input_columns_(std::move(input_columns)), element_column_(element_column),
          body_(std::move(body)), correlated_source_(correlated_source), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "Foreach(" + variable_ + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get(), body_.get()};
    }

    /// Output layout is the input's: FOREACH adds no column of its own.
    void deriveOutputLayout(const TupleSlotLayout&) override {
        slot_layout_ = child_->slotLayout();
    }

    void compileExpressions(const TupleSlotLayout& input_layout) override {
        ExpressionCompiler compiler(input_layout);
        compiler.compile(list_expr_);
    }

    /// Name of the iteration variable, for EXPLAIN. Set by the planner.
    void setVariable(std::string name) {
        variable_ = std::move(name);
    }

private:
    /// Inject one element's row into the body and drain it: the side effects happen
    /// while pulling, and the produced rows are dropped. Entity values published by
    /// the body (SET/REMOVE/DELETE return the updated entity) are collected into
    /// `updated` so the caller can refresh the outer row.
    folly::coro::Task<void> runBodyOnce(const DataChunk& chunk, size_t row, const Value& element,
                                        std::vector<Value>& updated);

    /// Refresh the outer row's entity columns from the entities the body published.
    ///
    /// The outer row can already hold a materialised copy of an entity: when the
    /// body reads an outer variable's properties, the enforcer inserts a
    /// ProjectionExtract *below* FOREACH, and that copy is a snapshot taken before
    /// the body ran. Reads later in the same query would then see the pre-write
    /// state -- `MATCH (p:P) FOREACH (x IN [1] | SET p.n = 1) RETURN p.n` returned
    /// null while the store already held 1. Mirroring the body's values back into
    /// every outer column that references the same entity id is the same
    /// cross-column mirror SetPhysicalOp performs within one chunk.
    void refreshOuterEntities(DataChunk& chunk, size_t row, const std::vector<Value>& updated);

    binder::BoundExpression list_expr_;
    std::vector<uint32_t> input_columns_;
    uint32_t element_column_;
    std::unique_ptr<PhysicalOperator> body_;
    CorrelatedSourcePhysicalOp* correlated_source_;
    std::unique_ptr<PhysicalOperator> child_;
    std::string variable_;
};

} // namespace compute
} // namespace eugraph
