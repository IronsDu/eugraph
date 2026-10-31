#pragma once

#include "query/planner/bound_expression/bound_expression.hpp"

#include <type_traits>
#include <utility>

namespace eugraph {
namespace binder {

/// Visit each direct sub-expression of `expr` in source order. The visitor is
/// called once per child with a `BoundExpression&` (or `const BoundExpression&`
/// if `expr` is const). Leaves (BoundLiteral / BoundColumnRef / BoundVariableRef
/// / BoundParameter) and the property-reference family (BoundPropertyRef /
/// BoundDynamicPropertyRef) have NO children visited here: property refs carry
/// per-visitor semantics around their `object` child, so each visitor handles
/// those explicitly.
///
/// Scope-changing variants (BoundListComprehension / All / Any / None / Single)
/// ARE visited here. Visitors that need to thread per-scope context capture it
/// by reference and update it before invoking this helper.
///
/// This is shared by the optimizer's requirement/rewrite visitors and the
/// evaluator's prefetch analysis so that "which children does variant X have"
/// lives in one place.
template <typename ExprRef, typename Visit> void forEachSubExpr(ExprRef&& expr, Visit&& visit) {
    std::visit(
        [&](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<BoundBinaryOp>>) {
                if (val) {
                    visit(val->left);
                    visit(val->right);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundUnaryOp>>) {
                if (val)
                    visit(val->operand);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundFunctionCall>>) {
                if (val)
                    for (auto& a : val->args)
                        visit(a);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundList>>) {
                if (val)
                    for (auto& e : val->elements)
                        visit(e);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundMap>>) {
                if (val)
                    for (auto& [k, v] : val->entries)
                        visit(v);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundCase>>) {
                if (val) {
                    if (val->subject)
                        visit(*val->subject);
                    for (auto& [w, t] : val->when_thens) {
                        visit(w);
                        visit(t);
                    }
                    if (val->else_expr)
                        visit(*val->else_expr);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundLabelCast>>) {
                if (val)
                    visit(val->object);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSubscript>>) {
                if (val) {
                    visit(val->list);
                    visit(val->index);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSlice>>) {
                if (val) {
                    visit(val->list);
                    if (val->from)
                        visit(*val->from);
                    if (val->to)
                        visit(*val->to);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundReduceExpr>>) {
                if (val) {
                    visit(val->init);
                    visit(val->list_expr);
                    visit(val->expr);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundListComprehension>> ||
                                 std::is_same_v<T, std::unique_ptr<BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<BoundSingleExpr>>) {
                if (val) {
                    visit(val->list_expr);
                    if constexpr (std::is_same_v<T, std::unique_ptr<BoundListComprehension>>)
                        visit(val->projection);
                    if (val->where_pred)
                        visit(*val->where_pred);
                }
            }
            // Out of scope: BoundPropertyRef / BoundDynamicPropertyRef — visitors
            // handle their `object` explicitly. Leaves: BoundLiteral /
            // BoundColumnRef / BoundVariableRef / BoundParameter — no children.
        },
        std::forward<ExprRef>(expr));
}

} // namespace binder
} // namespace eugraph
