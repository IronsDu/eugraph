#include "query/optimizer/rules/logical/push_filter_through_project.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_expression_walk.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace eugraph {
namespace optimizer {
namespace {

void collectExprNames(const binder::BoundExpression& expr, std::unordered_set<std::string>& out) {
    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                out.insert(val.name);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                out.insert(val.name);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (val)
                    collectExprNames(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (val)
                    collectExprNames(val->object, out);
            } else {
                binder::forEachSubExpr(expr, [&](const binder::BoundExpression& c) { collectExprNames(c, out); });
            }
        },
        expr);
}

void replaceAliases(binder::BoundExpression& expr,
                    const std::unordered_map<std::string, binder::BoundExpression>& aliases) {
    std::visit(
        [&](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                auto it = aliases.find(val.name);
                if (it != aliases.end())
                    expr = cloneBoundExpression(it->second);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                auto it = aliases.find(val.name);
                if (it != aliases.end())
                    expr = cloneBoundExpression(it->second);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (val)
                    replaceAliases(val->object, aliases);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                if (val)
                    replaceAliases(val->object, aliases);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (val)
                    replaceAliases(val->object, aliases);
            } else {
                binder::forEachSubExpr(expr, [&](binder::BoundExpression& c) { replaceAliases(c, aliases); });
            }
        },
        expr);
}

void rewriteOne(binder::BoundLogicalOperator& op) {
    forEachLogicalChild(op, [&](binder::BoundLogicalOperator& child) { rewriteOne(child); });

    auto* fp = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&op);
    if (!fp || !*fp)
        return;
    auto& filter = **fp;
    auto* pp = std::get_if<std::unique_ptr<binder::BoundProjectOp>>(&filter.child);
    if (!pp || !*pp)
        return;
    auto& project = **pp;

    std::unordered_set<std::string> filter_names;
    collectExprNames(filter.predicate, filter_names);
    if (filter_names.empty())
        return;

    std::unordered_map<std::string, binder::BoundExpression> aliases;
    for (const auto& item : project.items) {
        if (item.alias.empty() || !filter_names.contains(item.alias))
            continue;
        // Project 只重命名/计算，不改变行数；谓词替换成 item.expr 后下推到
        // Project 输入仍然语义等价。property/函数表达式也允许。
        aliases.emplace(item.alias, cloneBoundExpression(item.expr));
    }
    if (aliases.size() != filter_names.size())
        return;
    for (const auto& name : filter_names)
        if (!aliases.contains(name))
            return;

    auto new_filter = std::make_unique<binder::BoundFilterOp>();
    new_filter->predicate = cloneBoundExpression(filter.predicate);
    replaceAliases(new_filter->predicate, aliases);
    new_filter->child = std::move(project.child);
    project.child = binder::BoundLogicalOperator(std::move(new_filter));

    binder::BoundLogicalOperator moved = std::move(filter.child);
    op = std::move(moved);
}

} // namespace

void pushFilterThroughProject(binder::BoundLogicalOperator& root) {
    rewriteOne(root);
}

} // namespace optimizer
} // namespace eugraph
