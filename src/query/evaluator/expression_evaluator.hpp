#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/function/function_def.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

#include <folly/coro/Task.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eugraph {
namespace compute {

namespace detail {
struct RowIndex;
} // namespace detail

enum class QuantifierKind {
    ALL,
    ANY,
    NONE,
    SINGLE
};

/// Evaluates BoundExpression trees against DataChunk input,
/// producing columnar output without string-based dispatch.
///
/// With per-Column DICTIONARY support, the evaluator reads logical rows
/// directly via Column::getValue(logical_row) — Column handles its own
/// FLAT/CONSTANT/DICTIONARY mapping internally.
class ExpressionEvaluator {
public:
    ExpressionEvaluator() = default;
    explicit ExpressionEvaluator(const function::EvalContext& eval_ctx) : eval_ctx_(eval_ctx) {}

    /// Evaluate a bound expression for all logical rows in the chunk.
    /// Result is written into `result` column (must be FLAT, pre-sized).
    void evaluate(const binder::BoundExpression& expr, const DataChunk& input, Column& result);

    /// Evaluate a predicate expression. Result is written into a bool vector
    /// (true = row passes). Used by Filter operator.
    void evaluatePredicate(const binder::BoundExpression& expr, const DataChunk& input, std::vector<bool>& result);

    /// Optional substitution map for aggregate finalization: when set, BoundFunctionCall
    /// nodes whose func_def matches a key are replaced with the pre-computed Value.
    const std::unordered_map<const function::FunctionDef*, Value>* aggregate_substitutions = nullptr;

    /// Load the lazy graph data (labels/properties of topology-form vertices,
    /// runtime label defs) needed by `exprs` on this chunk, *before* the
    /// synchronous evaluate()/evaluatePredicate(). This is the only place in
    /// the evaluator allowed to touch storage: evaluation itself must be pure
    /// so it never blocks a compute thread on IO.
    ///
    /// No-op when none of the expressions contains a property reference.
    folly::coro::Task<void> prefetch(const std::vector<const binder::BoundExpression*>& exprs, const DataChunk& input);

    /// Diagnostics: property accesses that hit a bare vertex absent from the
    /// prefetch cache. Must stay 0 in tests/TCK; a non-zero value means an
    /// operator forgot to prefetch (the access then yields NULL, never IO).
    static uint64_t missingPrefetchCount() noexcept;
    static void resetMissingPrefetchCount() noexcept;

private:
    // Temporary storage for recursive sub-expression evaluation.
    std::deque<Column> temp_columns_;
    function::EvalContext eval_ctx_;
    // Cache for LabelDefs loaded from meta store at runtime for dynamically
    // created labels (e.g. __anon__) that are not in the static snapshot.
    mutable std::vector<std::unique_ptr<LabelDef>> label_def_cache_;
    mutable std::vector<std::unique_ptr<EdgeLabelDef>> edge_label_def_cache_;
    mutable LabelId anon_label_id_cached_ = INVALID_LABEL_ID;
    mutable bool anon_label_resolved_ = false;

    /// Per-chunk lazy data, filled by prefetch() and read-only during eval.
    struct PrefetchedVertexData {
        LabelIdSet labels;
        std::unordered_map<LabelId, Properties> properties;
    };
    std::unordered_map<VertexId, PrefetchedVertexData> prefetched_vertex_data_;
    /// Dry-pass state: while prefetching, a bare vertex missing from the cache
    /// is recorded (never loaded inline) so prefetch() can batch it.
    mutable bool prefetching_ = false;
    mutable std::vector<VertexId> prefetch_pending_ids_;

    const LabelDef* findLabelDef(LabelId label_id) const;
    const EdgeLabelDef* findEdgeLabelDef(EdgeLabelId label_id) const;
    folly::coro::Task<void> ensureLabelDefs(const std::unordered_set<LabelId>& label_ids,
                                            const std::unordered_set<EdgeLabelId>& edge_label_ids);
    static bool expressionNeedsPrefetch(const binder::BoundExpression& expr);
    static bool expressionMaySynthesizeEntity(const binder::BoundExpression& expr);
    folly::coro::Task<void> loadVertexDataAndDefs(const std::vector<VertexId>& ids);

    /// Evaluate an expression and return a reference to the result column.
    struct EvalResult {
        const Column* column = nullptr;
        bool is_temp = false;
    };

    EvalResult evaluateInternal(const binder::BoundExpression& expr, const DataChunk& input);

    Column& acquireTempColumn(binder::BoundTypeKind type, size_t capacity);

    Column& acquireTempConstant(binder::BoundTypeKind type, Value value);
    bool tryEvaluateTypedBinaryColumns(const binder::BoundBinaryOp& op, const Column& lhs, const Column& rhs,
                                       const detail::RowIndex& rows, Column& result);
    bool tryEvaluateTypedUnaryColumn(const binder::BoundUnaryOp& op, const Column& operand,
                                     const detail::RowIndex& rows, Column& result);
    void evalBinaryOp(const binder::BoundBinaryOp& op, const DataChunk& input, Column& result, size_t count);
    void evalUnaryOp(const binder::BoundUnaryOp& op, const DataChunk& input, Column& result, size_t count);
    void evalPropertyRef(const binder::BoundPropertyRef& ref, const DataChunk& input, Column& result, size_t count);
    void evalDynamicPropertyRef(const binder::BoundDynamicPropertyRef& ref, const DataChunk& input, Column& result,
                                size_t count);
    std::unordered_map<LabelId, std::unordered_map<std::string, uint16_t>> dyn_prop_index_cache_;
    /// Temporary column remapping for reduce bodies: binder column indices for
    /// acc/loop/index are keys, runtime positions in the temp chunk are values.
    std::unordered_map<uint32_t, uint32_t> temp_column_overrides_; // SlotId -> temp position
    void evalFunctionCall(const binder::BoundFunctionCall& fc, const DataChunk& input, Column& result, size_t count);

    /// Return a reference to an input column (no real evaluation). Falls back
    /// to an empty temp column if the index is out of range.
    EvalResult evalColumnRef(const binder::BoundColumnRef& ref, const DataChunk& input);

    /// Construct a literal list, evaluating each element sub-expression per row.
    EvalResult evalList(const binder::BoundList& list, const DataChunk& input);

    /// Re-cast a value to its label-typed vertex form (copies the inner column).
    EvalResult evalLabelCast(const binder::BoundLabelCast& cast, const DataChunk& input);

    /// Evaluate a quantifier expression (ALL/ANY/NONE/SINGLE).
    void evalQuantifierExpr(QuantifierKind kind, uint32_t loop_column_index, const binder::BoundExpression& list_expr,
                            const std::optional<binder::BoundExpression>& where_pred, const DataChunk& input,
                            Column& result, size_t count);

    /// Evaluate a CASE WHEN expression.
    void evalCase(const binder::BoundCase& case_expr, const DataChunk& input, Column& result, size_t count);

    /// Evaluate a BoundMap literal expression.
    void evalMap(const binder::BoundMap& map, const DataChunk& input, Column& result, size_t count);

    /// Evaluate a subscript expression: list[index] or map[key].
    void evalSubscript(const binder::BoundSubscript& sub, const DataChunk& input, Column& result, size_t count);

    /// Evaluate a slice expression: list[from..to].
    void evalSlice(const binder::BoundSlice& slice, const DataChunk& input, Column& result, size_t count);

    /// Evaluate a list comprehension: [x IN list | projection].
    void evalListComprehension(const binder::BoundListComprehension& lc, const DataChunk& input, Column& result,
                               size_t count);
    void evalReduce(const binder::BoundReduceExpr& reduce, const DataChunk& input, Column& result, size_t count);

    inline static std::atomic<uint64_t> missing_prefetch_count_{0};
};

} // namespace compute
} // namespace eugraph
