#pragma once
#include "query/planner/bound_expression/bound_expression.hpp"
#include <type_traits>
#include <unordered_set>
#include <variant>
namespace eugraph {
namespace optimizer {
namespace detail {
void collectExprSlots(const binder::BoundExpression& expr, std::unordered_set<binder::SlotId>& out) {
    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                if (val.slot_id != binder::INVALID_SLOT_ID)
                    out.insert(val.slot_id);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (val)
                    collectExprSlots(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (val)
                    collectExprSlots(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (val)
                    collectExprSlots(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (val) {
                    collectExprSlots(val->left, out);
                    collectExprSlots(val->right, out);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (val)
                    collectExprSlots(val->operand, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (val)
                    for (const auto& a : val->args)
                        collectExprSlots(a, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (val)
                    for (const auto& e : val->elements)
                        collectExprSlots(e, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                if (val)
                    for (const auto& [k, e] : val->entries) {
                        (void)k;
                        collectExprSlots(e, out);
                    }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (val) {
                    if (val->subject)
                        collectExprSlots(*val->subject, out);
                    for (const auto& [w, t] : val->when_thens) {
                        collectExprSlots(w, out);
                        collectExprSlots(t, out);
                    }
                    if (val->else_expr)
                        collectExprSlots(*val->else_expr, out);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                if (val) {
                    collectExprSlots(val->list, out);
                    collectExprSlots(val->index, out);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                if (val) {
                    collectExprSlots(val->list, out);
                    if (val->from)
                        collectExprSlots(*val->from, out);
                    if (val->to)
                        collectExprSlots(*val->to, out);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                if (val) {
                    collectExprSlots(val->list_expr, out);
                    if (val->where_pred)
                        collectExprSlots(*val->where_pred, out);
                    collectExprSlots(val->projection, out);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundReduceExpr>>) {
                if (val) {
                    collectExprSlots(val->init, out);
                    collectExprSlots(val->list_expr, out);
                    collectExprSlots(val->expr, out);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                if (val) {
                    collectExprSlots(val->list_expr, out);
                    if (val->where_pred)
                        collectExprSlots(*val->where_pred, out);
                }
            } else if constexpr (std::is_same_v<T, binder::BoundPatternComprehension>) {
                if (val.output_slot != binder::INVALID_SLOT_ID)
                    out.insert(val.output_slot);
            }
        },
        expr);
}

/// Collect slots consumed anywhere in a logical subtree (expressions plus
} // namespace detail
} // namespace optimizer
} // namespace eugraph
