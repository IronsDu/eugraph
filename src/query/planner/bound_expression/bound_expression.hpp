#pragma once

#include "query/planner/bound_expression/bound_binary_op.hpp"
#include "query/planner/bound_expression/bound_case.hpp"
#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_expression/bound_function_call.hpp"
#include "query/planner/bound_expression/bound_label_cast.hpp"
#include "query/planner/bound_expression/bound_list.hpp"
#include "query/planner/bound_expression/bound_literal.hpp"
#include "query/planner/bound_expression/bound_map.hpp"
#include "query/planner/bound_expression/bound_parameter.hpp"
#include "query/planner/bound_expression/bound_property_ref.hpp"
#include "query/planner/bound_expression/bound_quantifier_expr.hpp"
#include "query/planner/bound_expression/bound_slice.hpp"
#include "query/planner/bound_expression/bound_subscript.hpp"
#include "query/planner/bound_expression/bound_unary_op.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"

#include <string>

namespace eugraph {
namespace binder {

/// Get the result type of any bound expression.
inline const BoundType& getBoundExprType(const BoundExpression& expr) {
    return std::visit(
        [](const auto& val) -> const BoundType& {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, BoundLiteral>) {
                return val.type;
            } else if constexpr (std::is_same_v<T, BoundColumnRef>) {
                return val.type;
            } else if constexpr (std::is_same_v<T, BoundVariableRef>) {
                return val.type;
            } else if constexpr (std::is_same_v<T, BoundParameter>) {
                return val.expected_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundPropertyRef>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundLabelCast>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundBinaryOp>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundUnaryOp>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundFunctionCall>>) {
                return val->return_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundList>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundCase>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSubscript>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSlice>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundAllExpr>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundAnyExpr>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundNoneExpr>>) {
                return val->result_type;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSingleExpr>>) {
                return val->result_type;
            } else {
                // BoundMap has no meaningful result type
                static const BoundType any = BoundType::Any();
                return any;
            }
        },
        expr);
}

/// Debug: get a string representation of a bound expression.
std::string boundExprToString(const BoundExpression& expr);

} // namespace binder
} // namespace eugraph
