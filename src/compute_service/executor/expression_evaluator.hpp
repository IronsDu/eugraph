#pragma once

#include "compute_service/executor/row.hpp"
#include "compute_service/parser/ast.hpp"

#include <string>

namespace eugraph {
namespace compute {

/// Evaluates Cypher AST expressions against a Row context.
class ExpressionEvaluator {
public:
    /// Evaluate an expression given the current row and its schema (column names).
    Value evaluate(const cypher::Expression& expr, const Row& row, const Schema& schema);

private:
    Value evalLiteral(const cypher::Literal& lit);
    Value evalVariable(const cypher::Variable& var, const Row& row, const Schema& schema);
    Value evalBinaryOp(const cypher::BinaryOp& op, const Row& row, const Schema& schema);
    Value evalUnaryOp(const cypher::UnaryOp& op, const Row& row, const Schema& schema);
    Value evalPropertyAccess(const cypher::PropertyAccess& pa, const Row& row, const Schema& schema);
    Value evalFunctionCall(const cypher::FunctionCall& fc, const Row& row, const Schema& schema);
};

} // namespace compute
} // namespace eugraph
