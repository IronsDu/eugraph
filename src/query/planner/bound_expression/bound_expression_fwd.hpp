#pragma once

#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_literal.hpp"
#include "query/planner/bound_expression/bound_parameter.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundDynamicPropertyRef;
struct BoundPropertyRef;
struct BoundLabelCast;
struct BoundBinaryOp;
struct BoundUnaryOp;
struct BoundFunctionCall;
struct BoundList;
struct BoundMap;
struct BoundCase;
struct BoundSubscript;
struct BoundSlice;
struct BoundAllExpr;
struct BoundAnyExpr;
struct BoundNoneExpr;
struct BoundSingleExpr;

using BoundExpression =
    std::variant<BoundLiteral, BoundColumnRef, BoundVariableRef, BoundParameter, std::unique_ptr<BoundDynamicPropertyRef>,
                 std::unique_ptr<BoundPropertyRef>,
                 std::unique_ptr<BoundLabelCast>, std::unique_ptr<BoundBinaryOp>, std::unique_ptr<BoundUnaryOp>,
                 std::unique_ptr<BoundFunctionCall>, std::unique_ptr<BoundList>, std::unique_ptr<BoundMap>,
                 std::unique_ptr<BoundCase>, std::unique_ptr<BoundSubscript>, std::unique_ptr<BoundSlice>,
                 std::unique_ptr<BoundAllExpr>, std::unique_ptr<BoundAnyExpr>, std::unique_ptr<BoundNoneExpr>,
                 std::unique_ptr<BoundSingleExpr>>;

} // namespace binder
} // namespace eugraph
