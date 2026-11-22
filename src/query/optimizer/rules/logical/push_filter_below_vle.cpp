#include "query/optimizer/rules/logical/push_filter_below_vle.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_expression_walk.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

#include <memory>
#include <string>
#include <unordered_set>

namespace eugraph {
namespace optimizer {
namespace {

void collectNames(const binder::BoundExpression& expr, std::unordered_set<std::string>& out) {
    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                out.insert(val.name);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                out.insert(val.name);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (val)
                    collectNames(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (val)
                    collectNames(val->object, out);
            } else {
                binder::forEachSubExpr(expr, [&](const binder::BoundExpression& c) { collectNames(c, out); });
            }
        },
        expr);
}

void rewriteOne(binder::BoundLogicalOperator& op) {
    forEachLogicalChild(op, [&](binder::BoundLogicalOperator& c) { rewriteOne(c); });
    auto* fp = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&op);
    if (!fp || !*fp)
        return;
    auto& filter = **fp;
    auto* vp = std::get_if<std::unique_ptr<binder::BoundVarLenExpandOp>>(&filter.child);
    if (!vp || !*vp)
        return;
    auto& vle = **vp;
    std::unordered_set<std::string> names;
    collectNames(filter.predicate, names);
    if (names.empty())
        return;
    for (const auto& n : names)
        if (n != vle.src_variable)
            return;

    auto pushed = std::make_unique<binder::BoundFilterOp>();
    pushed->predicate = optimizer::cloneBoundExpression(filter.predicate);
    pushed->child = std::move(vle.child);
    vle.child = binder::BoundLogicalOperator(std::move(pushed));
    binder::BoundLogicalOperator moved = std::move(filter.child);
    op = std::move(moved);
}

} // namespace

void pushFilterBelowVle(binder::BoundLogicalOperator& root) {
    rewriteOne(root);
}

} // namespace optimizer
} // namespace eugraph
