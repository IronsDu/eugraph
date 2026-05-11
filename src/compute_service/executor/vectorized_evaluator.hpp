#pragma once

#include "compute_service/executor/data_chunk.hpp"
#include "compute_service/planner/bound_expression/bound_expression.hpp"

#include <cstdint>
#include <deque>
#include <vector>

namespace eugraph {
namespace compute {

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
class VectorizedEvaluator {
public:
    VectorizedEvaluator() = default;

    /// Evaluate a bound expression for all logical rows in the chunk.
    /// Result is written into `result` column (must be FLAT, pre-sized).
    void evaluate(const binder::BoundExpression& expr, const DataChunk& input, Column& result);

    /// Evaluate a predicate expression. Result is written into a bool vector
    /// (true = row passes). Used by Filter operator.
    void evaluatePredicate(const binder::BoundExpression& expr, const DataChunk& input, std::vector<bool>& result);

private:
    // Temporary storage for recursive sub-expression evaluation.
    std::deque<Column> temp_columns_;

    /// Evaluate an expression and return a reference to the result column.
    struct EvalResult {
        const Column* column = nullptr;
        bool is_temp = false;
    };

    EvalResult evaluateInternal(const binder::BoundExpression& expr, const DataChunk& input);

    Column& acquireTempColumn(binder::BoundTypeKind type, size_t capacity);

    void evalLiteral(const binder::BoundLiteral& lit, Column& result, size_t count);
    void evalBinaryOp(const binder::BoundBinaryOp& op, const DataChunk& input, Column& result, size_t count);
    void evalUnaryOp(const binder::BoundUnaryOp& op, const DataChunk& input, Column& result, size_t count);
    void evalPropertyRef(const binder::BoundPropertyRef& ref, const DataChunk& input, Column& result, size_t count);
    void evalFunctionCall(const binder::BoundFunctionCall& fc, const DataChunk& input, Column& result, size_t count);

    /// Evaluate a quantifier expression (ALL/ANY/NONE/SINGLE).
    void evalQuantifierExpr(QuantifierKind kind, uint32_t loop_column_index, const binder::BoundExpression& list_expr,
                            const std::optional<binder::BoundExpression>& where_pred, const DataChunk& input,
                            Column& result, size_t count);
};

} // namespace compute
} // namespace eugraph
