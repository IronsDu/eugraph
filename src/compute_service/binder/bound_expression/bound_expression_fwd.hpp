#pragma once

#include <memory>
#include <variant>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundLiteral;
struct BoundColumnRef;
struct BoundParameter;
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

using BoundExpression =
    std::variant<BoundLiteral, BoundColumnRef, BoundParameter, std::unique_ptr<BoundPropertyRef>,
                 std::unique_ptr<BoundLabelCast>, std::unique_ptr<BoundBinaryOp>, std::unique_ptr<BoundUnaryOp>,
                 std::unique_ptr<BoundFunctionCall>, std::unique_ptr<BoundList>, std::unique_ptr<BoundMap>,
                 std::unique_ptr<BoundCase>, std::unique_ptr<BoundSubscript>, std::unique_ptr<BoundSlice>>;

} // namespace binder
} // namespace eugraph
