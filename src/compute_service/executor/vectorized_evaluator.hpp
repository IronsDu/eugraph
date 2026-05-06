#pragma once

#include "compute_service/binder/bound_expression/bound_expression.hpp"
#include "compute_service/executor/data_chunk.hpp"

#include <cstdint>
#include <vector>

namespace eugraph {
namespace compute {

/// Evaluates BoundExpression trees against DataChunk input,
/// producing columnar output without string-based dispatch.
class VectorizedEvaluator {
public:
    VectorizedEvaluator() = default;

    /// Evaluate a bound expression for all rows in the chunk.
    /// Result is written into `result` column (must be pre-sized to chunk.count).
    void evaluate(const binder::BoundExpression& expr, const DataChunk& input, Column& result,
                  const SelectionVector* sel = nullptr);

    /// Evaluate a predicate expression. Result is written into a bool vector
    /// (true = row passes). Used by Filter operator.
    void evaluatePredicate(const binder::BoundExpression& expr, const DataChunk& input, std::vector<bool>& result,
                           const SelectionVector* sel = nullptr);

private:
    // Temporary storage for recursive sub-expression evaluation.
    // Each sub-expression result is placed in a temporary column.
    std::vector<Column> temp_columns_;

    /// Evaluate an expression and return a reference to the result column.
    /// The result column may be a temp column (valid until next call) or
    /// a direct reference to an input column.
    struct EvalResult {
        const Column* column = nullptr; // pointer to result data
        bool is_temp = false;           // true if this is a temp column
    };

    EvalResult evaluateInternal(const binder::BoundExpression& expr, const DataChunk& input,
                                const SelectionVector* sel);

    Column& acquireTempColumn(binder::BoundTypeKind type, size_t capacity);

    // Per-type evaluation helpers
    void evalLiteral(const binder::BoundLiteral& lit, Column& result, size_t count);
    void evalColumnRef(const binder::BoundColumnRef& ref, const DataChunk& input, Column& result, size_t count,
                       const SelectionVector* sel);
    void evalBinaryOp(const binder::BoundBinaryOp& op, const DataChunk& input, Column& result, size_t count,
                      const SelectionVector* sel);
    void evalUnaryOp(const binder::BoundUnaryOp& op, const DataChunk& input, Column& result, size_t count,
                     const SelectionVector* sel);
    void evalPropertyRef(const binder::BoundPropertyRef& ref, const DataChunk& input, Column& result, size_t count,
                         const SelectionVector* sel);
    void evalFunctionCall(const binder::BoundFunctionCall& fc, const DataChunk& input, Column& result, size_t count,
                          const SelectionVector* sel);
};

} // namespace compute
} // namespace eugraph
