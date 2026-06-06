#include "query/planner/binder.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace binder {

// ==================== SKIP/LIMIT Helper ====================

std::optional<int64_t> Binder::bindSkipLimit(const cypher::Expression& expr, const char* clause_name) {
    auto bound = bindExpression(expr);
    if (!bound)
        return std::nullopt;
    if (!std::holds_alternative<BoundLiteral>(*bound)) {
        error(std::string("SemanticError: ") + clause_name + " must be a constant expression");
        return std::nullopt;
    }
    auto& lit = std::get<BoundLiteral>(*bound);
    if (!std::holds_alternative<int64_t>(lit.value)) {
        error(std::string("SemanticError: ") + clause_name + " must be an integer");
        return std::nullopt;
    }
    auto val = std::get<int64_t>(lit.value);
    if (val < 0) {
        error(std::string("SemanticError: ") + clause_name + " must be a non-negative integer");
        return std::nullopt;
    }
    return val;
}

// ==================== RETURN Binding ====================

std::optional<BoundLogicalOperator> Binder::bindReturn(const cypher::ReturnClause& ret, BoundLogicalOperator child) {
    // Handle RETURN *: build projection items from all variables in scope
    // We cannot modify the const ret, so we build the projection inline
    // and then fall through to the normal bindReturn logic.
    // Instead, we build the items vector here and use it directly.
    if (ret.return_all && ret.items.empty()) {
        // Build a simple projection over all symbols
        auto proj = std::make_unique<BoundProjectOp>();
        for (const auto& [name, col_info] : ctx_.symbols) {
            auto bound_expr =
                std::make_optional<BoundExpression>(BoundColumnRef(col_info.column_index, col_info.type, name));
            BoundProjectOp::ProjectItem proj_item;
            proj_item.expr = std::move(*bound_expr);
            proj_item.alias = name;
            proj_item.result_type = getBoundExprType(proj_item.expr);
            proj->items.push_back(std::move(proj_item));

            ColumnInfo out_info;
            out_info.name = name;
            out_info.type = proj_item.result_type;
            out_info.column_index = static_cast<uint32_t>(proj->items.size() - 1);
            ctx_.return_columns.push_back(std::move(out_info));
        }
        proj->child = std::move(child);

        BoundLogicalOperator current = std::move(proj);

        // ORDER BY
        if (ret.order_by) {
            auto sort = std::make_unique<BoundSortOp>();
            for (const auto& si : ret.order_by->items) {
                auto bound_key = bindExpression(si.expr);
                if (!bound_key)
                    continue;
                BoundSortOp::SortItem sort_item;
                sort_item.expr = std::move(*bound_key);
                sort_item.direction = si.direction;
                sort->items.push_back(std::move(sort_item));
            }
            sort->child = std::move(current);
            current = std::move(sort);
        }
        if (ret.skip) {
            auto count = bindSkipLimit(*ret.skip, "SKIP");
            if (!count)
                return std::nullopt;
            auto skip = std::make_unique<BoundSkipOp>();
            skip->count = *count;
            skip->child = std::move(current);
            current = std::move(skip);
        }
        if (ret.limit) {
            auto count = bindSkipLimit(*ret.limit, "LIMIT");
            if (!count)
                return std::nullopt;
            auto limit = std::make_unique<BoundLimitOp>();
            limit->count = *count;
            limit->child = std::move(current);
            current = std::move(limit);
        }
        if (ret.distinct) {
            auto distinct = std::make_unique<BoundDistinctOp>();
            distinct->child = std::move(current);
            current = std::move(distinct);
        }
        return current;
    }

    // Check for aggregate functions in return items
    bool has_aggregate = false;
    for (const auto& item : ret.items) {
        std::visit(
            [&has_aggregate](const auto& ptr) {
                using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                    if (ptr->name == "count" || ptr->name == "sum" || ptr->name == "avg" || ptr->name == "min" ||
                        ptr->name == "max") {
                        has_aggregate = true;
                    }
                }
            },
            item.expr);
    }

    if (has_aggregate) {
        // Build aggregate operator
        auto agg = std::make_unique<BoundAggregateOp>();

        for (const auto& item : ret.items) {
            bool is_agg = false;
            std::visit(
                [&is_agg](const auto& ptr) {
                    using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                    if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                        if (ptr->name == "count" || ptr->name == "sum" || ptr->name == "avg" || ptr->name == "min" ||
                            ptr->name == "max") {
                            is_agg = true;
                        }
                    }
                },
                item.expr);

            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            auto bound_expr = bindExpression(item.expr);
            if (!bound_expr)
                continue;

            // Detect whole-variable return (e.g., RETURN n) to add all property requirements
            if (!is_agg) {
                if (std::holds_alternative<BoundColumnRef>(*bound_expr)) {
                    auto& col_ref = std::get<BoundColumnRef>(*bound_expr);
                    if (col_ref.type.kind == BoundTypeKind::VERTEX) {
                        addAllPropertiesForVariable(col_ref.name);
                    }
                } else if (std::holds_alternative<std::unique_ptr<BoundLabelCast>>(*bound_expr)) {
                    auto& lc = std::get<std::unique_ptr<BoundLabelCast>>(*bound_expr);
                    if (std::holds_alternative<BoundColumnRef>(lc->object)) {
                        auto var_name = std::get<BoundColumnRef>(lc->object).name;
                        const auto* ldef = catalog_.lookupLabel(lc->label_id);
                        if (ldef) {
                            for (const auto& pd : ldef->properties)
                                ctx_.addPropertyRequirement(var_name, lc->label_id, pd.id);
                        }
                    }
                }
            }

            if (is_agg) {
                // Extract function name and argument from the expression
                BoundAggregateOp::AggregateItem agg_item;
                agg_item.alias = alias;
                agg_item.result_type = getBoundExprType(*bound_expr);

                // Walk the bound expression to find the function call
                if (std::holds_alternative<std::unique_ptr<BoundFunctionCall>>(*bound_expr)) {
                    auto& fc = std::get<std::unique_ptr<BoundFunctionCall>>(*bound_expr);
                    agg_item.function_name = fc->func_def->name;
                    agg_item.func_def = fc->func_def;
                    agg_item.distinct = fc->distinct;
                    if (!fc->args.empty())
                        agg_item.argument = std::move(fc->args[0]);
                }
                agg->aggregates.push_back(std::move(agg_item));
            } else {
                agg->group_keys.push_back(std::move(*bound_expr));
            }
            agg->output_names.push_back(std::move(alias));
        }

        agg->child = std::move(child);

        // Register RETURN aliases so ORDER BY can reference them.
        // Column index = aggregate output position (group_keys then aggregates).
        for (size_t i = 0; i < agg->output_names.size(); ++i) {
            if (ctx_.symbols.find(agg->output_names[i]) == ctx_.symbols.end()) {
                BoundType col_type = (i < agg->group_keys.size())
                                         ? getBoundExprType(agg->group_keys[i])
                                         : BoundType::clone(agg->aggregates[i - agg->group_keys.size()].result_type);
                ColumnInfo info;
                info.name = agg->output_names[i];
                info.type = std::move(col_type);
                info.column_index = static_cast<uint32_t>(i);
                ctx_.symbols[agg->output_names[i]] = std::move(info);
            }
            // Populate return_columns for output_schema
            ColumnInfo out_info;
            out_info.name = agg->output_names[i];
            out_info.type = (i < agg->group_keys.size())
                                ? getBoundExprType(agg->group_keys[i])
                                : BoundType::clone(agg->aggregates[i - agg->group_keys.size()].result_type);
            out_info.column_index = static_cast<uint32_t>(i);
            ctx_.return_columns.push_back(std::move(out_info));
        }

        auto result = std::make_unique<BoundProjectOp>(); // Agg is a kind of project
        // Actually, aggregate is its own operator
        BoundLogicalOperator agg_op = std::move(agg);

        // ORDER BY, SKIP, LIMIT, DISTINCT
        BoundLogicalOperator current = std::move(agg_op);

        if (ret.order_by) {
            auto sort = std::make_unique<BoundSortOp>();
            for (const auto& si : ret.order_by->items) {
                auto bound_key = bindExpression(si.expr);
                if (!bound_key)
                    continue;
                BoundSortOp::SortItem sort_item;
                sort_item.expr = std::move(*bound_key);
                sort_item.direction = si.direction;
                sort->items.push_back(std::move(sort_item));
            }
            sort->child = std::move(current);
            current = std::move(sort);
        }
        if (ret.skip) {
            auto count = bindSkipLimit(*ret.skip, "SKIP");
            if (!count)
                return std::nullopt;
            auto skip = std::make_unique<BoundSkipOp>();
            skip->count = *count;
            skip->child = std::move(current);
            current = std::move(skip);
        }
        if (ret.limit) {
            auto count = bindSkipLimit(*ret.limit, "LIMIT");
            if (!count)
                return std::nullopt;
            auto limit = std::make_unique<BoundLimitOp>();
            limit->count = *count;
            limit->child = std::move(current);
            current = std::move(limit);
        }
        if (ret.distinct) {
            auto distinct = std::make_unique<BoundDistinctOp>();
            distinct->child = std::move(current);
            current = std::move(distinct);
        }
        return current;
    }

    // Non-aggregate: simple projection
    auto proj = std::make_unique<BoundProjectOp>();
    for (const auto& item : ret.items) {
        auto bound_expr = bindExpression(item.expr);
        if (!bound_expr)
            continue;

        // Detect whole-variable return (e.g., RETURN n) to add all property requirements
        if (std::holds_alternative<BoundColumnRef>(*bound_expr)) {
            auto& col_ref = std::get<BoundColumnRef>(*bound_expr);
            if (col_ref.type.kind == BoundTypeKind::VERTEX) {
                addAllPropertiesForVariable(col_ref.name);
            }
        } else if (std::holds_alternative<std::unique_ptr<BoundLabelCast>>(*bound_expr)) {
            auto& lc = std::get<std::unique_ptr<BoundLabelCast>>(*bound_expr);
            if (std::holds_alternative<BoundColumnRef>(lc->object)) {
                auto var_name = std::get<BoundColumnRef>(lc->object).name;
                const auto* ldef = catalog_.lookupLabel(lc->label_id);
                if (ldef) {
                    for (const auto& pd : ldef->properties)
                        ctx_.addPropertyRequirement(var_name, lc->label_id, pd.id);
                }
            }
        }

        std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
        BoundProjectOp::ProjectItem proj_item;
        proj_item.expr = std::move(*bound_expr);
        proj_item.alias = std::move(alias);
        proj_item.result_type = getBoundExprType(proj_item.expr);
        proj->items.push_back(std::move(proj_item));
    }
    proj->child = std::move(child);

    // Register RETURN aliases so ORDER BY can reference them.
    // Build alias → original expression map for ORDER BY resolution.
    // (Sort is placed before projection, so aliases need to be resolved
    // to the original expression, not the projected column index.)
    std::unordered_map<std::string, size_t> alias_to_proj_idx;
    for (size_t i = 0; i < proj->items.size(); ++i) {
        const auto& proj_item = proj->items[i];
        alias_to_proj_idx[proj_item.alias] = i;
        if (ctx_.symbols.find(proj_item.alias) == ctx_.symbols.end()) {
            ColumnInfo info;
            info.name = proj_item.alias;
            info.type = proj_item.result_type;
            info.column_index = static_cast<uint32_t>(i);
            ctx_.symbols[proj_item.alias] = std::move(info);
        }
        // Populate return_columns for output_schema
        ColumnInfo out_info;
        out_info.name = proj_item.alias;
        out_info.type = proj_item.result_type;
        out_info.column_index = static_cast<uint32_t>(i);
        ctx_.return_columns.push_back(std::move(out_info));
    }

    // Build child pipeline: sort before projection so ORDER BY can reference
    // original columns (e.g., r.id where r is an EdgeValue from MATCH).
    BoundLogicalOperator child_op = std::move(proj->child);

    // ORDER BY
    if (ret.order_by) {
        auto sort = std::make_unique<BoundSortOp>();
        for (const auto& si : ret.order_by->items) {
            // If the ORDER BY expression is a simple variable that matches a
            // RETURN alias, re-bind the original RETURN expression.
            auto* var = std::get_if<std::unique_ptr<cypher::Variable>>(&si.expr);
            if (var && *var && alias_to_proj_idx.count((*var)->name)) {
                size_t idx = alias_to_proj_idx[(*var)->name];
                // Re-bind the original expression from the RETURN item.
                // This works because the child columns are still available
                // (sort is before projection).
                auto bound_key = bindExpression(ret.items[idx].expr);
                if (bound_key) {
                    BoundSortOp::SortItem sort_item;
                    sort_item.expr = std::move(*bound_key);
                    sort_item.direction = si.direction;
                    sort->items.push_back(std::move(sort_item));
                }
            } else {
                auto bound_key = bindExpression(si.expr);
                if (!bound_key)
                    continue;
                BoundSortOp::SortItem sort_item;
                sort_item.expr = std::move(*bound_key);
                sort_item.direction = si.direction;
                sort->items.push_back(std::move(sort_item));
            }
        }
        sort->child = std::move(child_op);
        child_op = std::move(sort);
    }

    // Projection after sort
    proj->child = std::move(child_op);
    BoundLogicalOperator current = std::move(proj);

    // SKIP, LIMIT, DISTINCT
    if (ret.skip) {
        auto count = bindSkipLimit(*ret.skip, "SKIP");
        if (!count)
            return std::nullopt;
        auto skip = std::make_unique<BoundSkipOp>();
        skip->count = *count;
        skip->child = std::move(current);
        current = std::move(skip);
    }
    if (ret.limit) {
        auto count = bindSkipLimit(*ret.limit, "LIMIT");
        if (!count)
            return std::nullopt;
        auto limit = std::make_unique<BoundLimitOp>();
        limit->count = *count;
        limit->child = std::move(current);
        current = std::move(limit);
    }
    if (ret.distinct) {
        auto distinct = std::make_unique<BoundDistinctOp>();
        distinct->child = std::move(current);
        current = std::move(distinct);
    }

    return current;
}

// ==================== WITH Binding ====================

std::optional<BoundLogicalOperator> Binder::bindWith(const cypher::WithClause& wc, BoundLogicalOperator child) {
    // Check for aggregate functions in WITH items
    bool has_aggregate = false;
    for (const auto& item : wc.items) {
        std::visit(
            [&has_aggregate](const auto& ptr) {
                using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                    if (ptr->name == "count" || ptr->name == "sum" || ptr->name == "avg" || ptr->name == "min" ||
                        ptr->name == "max") {
                        has_aggregate = true;
                    }
                }
            },
            item.expr);
    }

    // Collect output names and types for scope reset
    std::vector<std::pair<std::string, BoundType>> with_outputs;

    BoundLogicalOperator current;

    if (has_aggregate) {
        auto agg = std::make_unique<BoundAggregateOp>();

        for (const auto& item : wc.items) {
            bool is_agg = false;
            std::visit(
                [&is_agg](const auto& ptr) {
                    using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                    if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                        if (ptr->name == "count" || ptr->name == "sum" || ptr->name == "avg" || ptr->name == "min" ||
                            ptr->name == "max") {
                            is_agg = true;
                        }
                    }
                },
                item.expr);

            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            auto bound_expr = bindExpression(item.expr);
            if (!bound_expr)
                continue;

            if (is_agg) {
                BoundAggregateOp::AggregateItem agg_item;
                agg_item.alias = alias;
                agg_item.result_type = getBoundExprType(*bound_expr);

                if (std::holds_alternative<std::unique_ptr<BoundFunctionCall>>(*bound_expr)) {
                    auto& fc = std::get<std::unique_ptr<BoundFunctionCall>>(*bound_expr);
                    agg_item.function_name = fc->func_def->name;
                    agg_item.func_def = fc->func_def;
                    agg_item.distinct = fc->distinct;
                    if (!fc->args.empty())
                        agg_item.argument = std::move(fc->args[0]);
                }
                agg->aggregates.push_back(std::move(agg_item));
                with_outputs.emplace_back(alias, BoundType::clone(agg->aggregates.back().result_type));
            } else {
                with_outputs.emplace_back(alias, getBoundExprType(*bound_expr));
                agg->group_keys.push_back(std::move(*bound_expr));
            }
            agg->output_names.push_back(std::move(alias));
        }

        agg->child = std::move(child);
        current = std::move(agg);
    } else {
        // Simple projection
        auto proj = std::make_unique<BoundProjectOp>();
        for (const auto& item : wc.items) {
            auto bound_expr = bindExpression(item.expr);
            if (!bound_expr)
                continue;

            // Detect whole-variable pass-through to add property requirements
            if (std::holds_alternative<BoundColumnRef>(*bound_expr)) {
                auto& col_ref = std::get<BoundColumnRef>(*bound_expr);
                if (col_ref.type.kind == BoundTypeKind::VERTEX) {
                    addAllPropertiesForVariable(col_ref.name);
                }
            }

            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            BoundProjectOp::ProjectItem proj_item;
            proj_item.expr = std::move(*bound_expr);
            proj_item.alias = alias;
            proj_item.result_type = getBoundExprType(proj_item.expr);
            proj->items.push_back(std::move(proj_item));
            with_outputs.emplace_back(alias, proj_item.result_type);
        }
        proj->child = std::move(child);
        current = std::move(proj);
    }

    // Register WITH aliases temporarily so ORDER BY / SKIP / LIMIT can resolve them.
    // Preserve metadata (source_label, etc.) from any prior registration so that
    // downstream SET/REMOVE can resolve property IDs.
    for (size_t i = 0; i < with_outputs.size(); ++i) {
        ColumnInfo info;
        info.name = with_outputs[i].first;
        info.type = BoundType::clone(with_outputs[i].second);
        info.column_index = static_cast<uint32_t>(i);
        auto existing = ctx_.symbols.find(with_outputs[i].first);
        if (existing != ctx_.symbols.end()) {
            info.source_labels = existing->second.source_labels;
            info.source_prop_id = existing->second.source_prop_id;
            info.strong_typed = existing->second.strong_typed;
        }
        ctx_.symbols[with_outputs[i].first] = std::move(info);
    }

    // ORDER BY
    if (wc.order_by) {
        auto sort = std::make_unique<BoundSortOp>();
        for (const auto& si : wc.order_by->items) {
            auto bound_key = bindExpression(si.expr);
            if (!bound_key)
                continue;
            BoundSortOp::SortItem sort_item;
            sort_item.expr = std::move(*bound_key);
            sort_item.direction = si.direction;
            sort->items.push_back(std::move(sort_item));
        }
        sort->child = std::move(current);
        current = std::move(sort);
    }

    // SKIP
    if (wc.skip) {
        auto count = bindSkipLimit(*wc.skip, "SKIP");
        if (!count)
            return std::nullopt;
        auto skip = std::make_unique<BoundSkipOp>();
        skip->count = *count;
        skip->child = std::move(current);
        current = std::move(skip);
    }

    // LIMIT
    if (wc.limit) {
        auto count = bindSkipLimit(*wc.limit, "LIMIT");
        if (!count)
            return std::nullopt;
        auto limit = std::make_unique<BoundLimitOp>();
        limit->count = *count;
        limit->child = std::move(current);
        current = std::move(limit);
    }

    // DISTINCT
    if (wc.distinct) {
        auto distinct = std::make_unique<BoundDistinctOp>();
        distinct->child = std::move(current);
        current = std::move(distinct);
    }

    // Scope reset: only WITH output columns are visible after this point.
    // Preserve metadata (source_label, source_prop_id, strong_typed) from old
    // symbols so that downstream SET/REMOVE can resolve property IDs.
    auto old_symbols = std::move(ctx_.symbols);
    ctx_.next_column_index = 0;
    for (size_t i = 0; i < with_outputs.size(); ++i) {
        ColumnInfo info;
        info.name = with_outputs[i].first;
        info.type = std::move(with_outputs[i].second);
        info.column_index = static_cast<uint32_t>(i);
        // Carry forward metadata if this variable was known before WITH.
        auto it = old_symbols.find(with_outputs[i].first);
        if (it != old_symbols.end()) {
            info.source_labels = it->second.source_labels;
            info.source_prop_id = it->second.source_prop_id;
            info.strong_typed = it->second.strong_typed;
        }
        ctx_.symbols[with_outputs[i].first] = std::move(info);
    }
    ctx_.next_column_index = static_cast<uint32_t>(with_outputs.size());

    // WHERE
    if (wc.where_pred) {
        auto where_op = bindWhere(*wc.where_pred, std::move(current));
        if (!where_op)
            return std::nullopt;
        current = std::move(*where_op);
    }

    return current;
}

} // namespace binder
} // namespace eugraph
