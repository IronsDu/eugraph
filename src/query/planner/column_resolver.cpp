#include "query/planner/column_resolver.hpp"

#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_semi_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

namespace eugraph {
namespace binder {

bool ColumnResolver::resolve(BoundLogicalPlan& plan, const BindContext& ctx, std::vector<std::string>& errors) {
    return resolveOperator(plan.root, ctx, errors);
}

bool ColumnResolver::resolveOperator(BoundLogicalOperator& op, const BindContext& ctx,
                                     std::vector<std::string>& errors) {
    return std::visit(
        [this, &ctx, &errors](auto& val) -> bool {
            using T = std::decay_t<decltype(val)>;

            // Resolve expressions that are directly stored in operators
            if constexpr (std::is_same_v<T, std::unique_ptr<BoundFilterOp>>) {
                if (!resolveExpression(val->predicate, ctx, errors))
                    return false;
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundProjectOp>>) {
                for (auto& item : val->items) {
                    if (!resolveExpression(item.expr, ctx, errors))
                        return false;
                }
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundAggregateOp>>) {
                for (auto& gk : val->group_keys) {
                    if (!resolveExpression(gk, ctx, errors))
                        return false;
                }
                for (auto& agg : val->aggregates) {
                    if (!resolveExpression(agg.argument, ctx, errors))
                        return false;
                }
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSortOp>>) {
                for (auto& item : val->items) {
                    if (!resolveExpression(item.expr, ctx, errors))
                        return false;
                }
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundCreateNodeOp>>) {
                for (auto& [prop_id, expr] : val->properties) {
                    if (!resolveExpression(expr, ctx, errors))
                        return false;
                }
                if (val->child)
                    return resolveOperator(*val->child, ctx, errors);
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundCreateEdgeOp>>) {
                for (auto& [prop_id, expr] : val->properties) {
                    if (!resolveExpression(expr, ctx, errors))
                        return false;
                }
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSetOp>>) {
                for (auto& item : val->items) {
                    if (item.value_expr) {
                        if (!resolveExpression(*item.value_expr, ctx, errors))
                            return false;
                    }
                }
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundExpandOp>>) {
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundVarLenExpandOp>>) {
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSkipOp>>) {
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundLimitOp>>) {
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundDistinctOp>>) {
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundRemoveOp>>) {
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundPathBuildOp>>) {
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundBinaryJoinOp>>) {
                return resolveOperator(val->left, ctx, errors) && resolveOperator(val->right, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSemiJoinOp>>) {
                return resolveOperator(val->left, ctx, errors) && resolveOperator(val->right, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundUnwindOp>>) {
                if (!resolveExpression(val->list_expr, ctx, errors))
                    return false;
                return resolveOperator(val->child, ctx, errors);
            } else if constexpr (std::is_same_v<T, BoundSingletonOp>) {
                // No expressions or children
                return true;
            } else {
                // BoundScanOp, BoundLabelScanOp — no expressions or children
                return true;
            }
        },
        op);
}

bool ColumnResolver::resolveExpression(BoundExpression& expr, const BindContext& ctx,
                                       std::vector<std::string>& errors) {
    return std::visit(
        [this, &ctx, &errors, &expr](auto& val) -> bool {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, BoundVariableRef>) {
                auto* col = ctx.lookup(val.name);
                if (!col) {
                    errors.push_back("ColumnResolver: variable '" + val.name + "' not found in BindContext");
                    return false;
                }
                // Replace BoundVariableRef with BoundColumnRef in-place
                BoundColumnRef cfr(col->column_index, val.type, val.name);
                expr = std::move(cfr);
                return true;
            } else if constexpr (std::is_same_v<T, BoundColumnRef>) {
                // Already resolved — nothing to do
                return true;
            } else if constexpr (std::is_same_v<T, BoundLiteral>) {
                return true;
            } else if constexpr (std::is_same_v<T, BoundParameter>) {
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundBinaryOp>>) {
                if (!resolveExpression(val->left, ctx, errors))
                    return false;
                return resolveExpression(val->right, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundUnaryOp>>) {
                return resolveExpression(val->operand, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundPropertyRef>>) {
                return resolveExpression(val->object, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundDynamicPropertyRef>>) {
                return resolveExpression(val->object, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundFunctionCall>>) {
                for (auto& arg : val->args) {
                    if (!resolveExpression(arg, ctx, errors))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundLabelCast>>) {
                return resolveExpression(val->object, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundList>>) {
                for (auto& elem : val->elements) {
                    if (!resolveExpression(elem, ctx, errors))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundCase>>) {
                if (val->subject) {
                    if (!resolveExpression(*val->subject, ctx, errors))
                        return false;
                }
                for (auto& [when, then] : val->when_thens) {
                    if (!resolveExpression(when, ctx, errors))
                        return false;
                    if (!resolveExpression(then, ctx, errors))
                        return false;
                }
                if (val->else_expr) {
                    if (!resolveExpression(*val->else_expr, ctx, errors))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSubscript>>) {
                if (!resolveExpression(val->list, ctx, errors))
                    return false;
                return resolveExpression(val->index, ctx, errors);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSlice>>) {
                if (!resolveExpression(val->list, ctx, errors))
                    return false;
                if (val->from) {
                    if (!resolveExpression(*val->from, ctx, errors))
                        return false;
                }
                if (val->to) {
                    if (!resolveExpression(*val->to, ctx, errors))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundAllExpr>>) {
                if (!resolveExpression(val->list_expr, ctx, errors))
                    return false;
                if (val->where_pred) {
                    if (!resolveExpression(*val->where_pred, ctx, errors))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundAnyExpr>>) {
                if (!resolveExpression(val->list_expr, ctx, errors))
                    return false;
                if (val->where_pred) {
                    if (!resolveExpression(*val->where_pred, ctx, errors))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundNoneExpr>>) {
                if (!resolveExpression(val->list_expr, ctx, errors))
                    return false;
                if (val->where_pred) {
                    if (!resolveExpression(*val->where_pred, ctx, errors))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundSingleExpr>>) {
                if (!resolveExpression(val->list_expr, ctx, errors))
                    return false;
                if (val->where_pred) {
                    if (!resolveExpression(*val->where_pred, ctx, errors))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundMap>>) {
                for (auto& [key, entry] : val->entries) {
                    if (!resolveExpression(entry, ctx, errors))
                        return false;
                }
                return true;
            } else {
                return true;
            }
        },
        expr);
}

} // namespace binder
} // namespace eugraph
