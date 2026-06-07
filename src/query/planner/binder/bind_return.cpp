#include "query/planner/binder.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace binder {

// ==================== Aggregate Detection Helper ====================

static bool hasAggregate(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                if (ptr->name == "count" || ptr->name == "sum" || ptr->name == "avg" || ptr->name == "min" ||
                    ptr->name == "max" || ptr->name == "collect")
                    return true;
                for (const auto& arg : ptr->args)
                    if (hasAggregate(arg))
                        return true;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return hasAggregate(ptr->left) || hasAggregate(ptr->right);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return hasAggregate(ptr->operand);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                return hasAggregate(ptr->object);
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries)
                    if (hasAggregate(v))
                        return true;
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& e : ptr->elements)
                    if (hasAggregate(e))
                        return true;
            }
            return false;
        },
        expr);
}

// ==================== Aggregate Extraction & Replacement ====================

/// Walk a BoundExpression tree, replacing every aggregate BoundFunctionCall with
/// a BoundVariableRef to an anonymous column (__agg_0, __agg_1, ...).
/// The extracted aggregate info is pushed into `out_aggs`.
static void walkAndReplaceAggCalls(binder::BoundExpression& expr,
                                   std::vector<BoundAggregateOp::AggregateItem>& out_aggs,
                                   uint32_t& agg_idx) {
    std::visit(
        [&](auto& ptr) {
            using T = std::decay_t<decltype(ptr)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (ptr->func_def->is_aggregate) {
                    // Extract aggregate info.
                    BoundAggregateOp::AggregateItem item;
                    item.func_def = ptr->func_def;
                    item.distinct = ptr->distinct;
                    item.result_type = ptr->return_type;
                    if (!ptr->args.empty())
                        item.argument = std::move(ptr->args[0]);
                    out_aggs.push_back(std::move(item));

                    // Replace this node with a BoundVariableRef.
                    std::string anon_name = "__agg_" + std::to_string(agg_idx++);
                    auto ret_type = out_aggs.back().result_type;
                    out_aggs.back().alias = anon_name;
                    expr = BoundVariableRef{std::move(anon_name), std::move(ret_type)};
                    return; // MUST return: ptr is now a dangling reference
                } else {
                    // Non-aggregate function: recurse into args.
                    for (auto& arg : ptr->args)
                        walkAndReplaceAggCalls(arg, out_aggs, agg_idx);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                walkAndReplaceAggCalls(ptr->left, out_aggs, agg_idx);
                walkAndReplaceAggCalls(ptr->right, out_aggs, agg_idx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                walkAndReplaceAggCalls(ptr->operand, out_aggs, agg_idx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (auto& elem : ptr->elements)
                    walkAndReplaceAggCalls(elem, out_aggs, agg_idx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (auto& [k, v] : ptr->entries)
                    walkAndReplaceAggCalls(v, out_aggs, agg_idx);
            }
        },
        expr);
}

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
        // Build a simple projection over all symbols, ordered by column index
        auto proj = std::make_unique<BoundProjectOp>();
        std::vector<const ColumnInfo*> sorted_symbols;
        for (const auto& [name, col_info] : ctx_.symbols)
            sorted_symbols.push_back(&col_info);
        std::sort(sorted_symbols.begin(), sorted_symbols.end(),
                  [](const ColumnInfo* a, const ColumnInfo* b) { return a->column_index < b->column_index; });
        for (const auto* col_info : sorted_symbols) {
            auto bound_expr = std::make_optional<BoundExpression>(
                BoundColumnRef(col_info->column_index, col_info->type, col_info->name));
            BoundProjectOp::ProjectItem proj_item;
            proj_item.expr = std::move(*bound_expr);
            proj_item.alias = col_info->name;
            proj_item.result_type = getBoundExprType(proj_item.expr);
            proj->items.push_back(std::move(proj_item));

            ColumnInfo out_info;
            out_info.name = col_info->name;
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

    // Check for aggregate functions in return items (recursively)
    bool has_aggregate = false;
    for (const auto& item : ret.items) {
        if (hasAggregate(item.expr)) {
            has_aggregate = true;
            break;
        }
    }

    if (has_aggregate) {
        // ── Aggregate + Project strategy ──
        // AggregateOp handles only simple aggregate functions, outputting them
        // as anonymous columns (__agg_0, __agg_1...). Complex expressions like
        // count(a)+3 are split: the aggregate goes to AggregateOp as an anonymous
        // column, and the scalar part (+3) is handled by a downstream ProjectOp.
        auto agg = std::make_unique<BoundAggregateOp>();

        // Items for the downstream ProjectOp (complex aggregate expressions
        // that were rewritten to reference anonymous columns).
        struct ProjItem {
            binder::BoundExpression expr;
            std::string alias;
        };
        std::vector<ProjItem> proj_items;

        // Column index tracker for anonymous aggregate columns.
        uint32_t anon_idx = 0;

        for (const auto& item : ret.items) {
            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            bool is_simple_agg = std::holds_alternative<std::unique_ptr<cypher::FunctionCall>>(item.expr) &&
                                 hasAggregate(item.expr);

            auto bound_expr = bindExpression(item.expr);
            if (!bound_expr)
                continue;

            if (is_simple_agg) {
                // Top-level aggregate function: e.g., RETURN count(a).
                auto& fc = std::get<std::unique_ptr<cypher::FunctionCall>>(item.expr);
                BoundAggregateOp::AggregateItem agg_item;
                agg_item.alias = alias;
                if (std::holds_alternative<std::unique_ptr<BoundFunctionCall>>(*bound_expr)) {
                    auto& bfc = std::get<std::unique_ptr<BoundFunctionCall>>(*bound_expr);
                    agg_item.func_def = bfc->func_def;
                    agg_item.distinct = bfc->distinct;
                    agg_item.result_type = bfc->return_type;
                    if (!bfc->args.empty())
                        agg_item.argument = std::move(bfc->args[0]);
                }
                agg->aggregates.push_back(std::move(agg_item));
                agg->output_names.push_back(alias);

                ColumnInfo info;
                info.name = alias;
                info.type = BoundType::clone(agg->aggregates.back().result_type);
                info.column_index = static_cast<uint32_t>(agg->group_keys.size() + agg->aggregates.size() - 1);
                ctx_.symbols[alias] = std::move(info);
            } else if (hasAggregate(item.expr)) {
                // Complex expression containing aggregates: e.g., RETURN count(a) + 3.
                // Walk the bound expression, extract each aggregate call into
                // AggregateOp as an anonymous column, and replace the call in the
                // expression tree with a BoundVariableRef to that column.
                auto full_expr = std::move(*bound_expr);
                walkAndReplaceAggCalls(full_expr, agg->aggregates, anon_idx);

                // Register anonymous aggregate columns in the symbol table
                // so the ProjectOp and ColumnResolver can find them.
                size_t start_agg = agg->aggregates.size() - (agg->aggregates.size() > 0 ? 0 : 0);
                for (size_t i = 0; i < agg->aggregates.size(); ++i) {
                    auto& ai = agg->aggregates[i];
                    if (ai.alias.starts_with("__agg_")) {
                        if (ctx_.symbols.find(ai.alias) == ctx_.symbols.end()) {
                            ColumnInfo info;
                            info.name = ai.alias;
                            info.type = BoundType::clone(ai.result_type);
                            info.column_index = static_cast<uint32_t>(agg->group_keys.size() + i);
                            ctx_.symbols[ai.alias] = std::move(info);
                        }
                        agg->output_names.push_back(ai.alias);
                    }
                }

                proj_items.push_back({std::move(full_expr), alias});
            } else {
                // Group key: non-aggregate expression.
                agg->group_keys.push_back(std::move(*bound_expr));
                agg->output_names.push_back(alias);

                ColumnInfo info;
                info.name = alias;
                info.type = getBoundExprType(agg->group_keys.back());
                info.column_index = static_cast<uint32_t>(agg->group_keys.size() - 1);
                ctx_.symbols[alias] = std::move(info);
            }
        }

        agg->child = std::move(child);

        // Register all AggregateOp columns (group keys + aggregates) in symbol table.
        for (size_t i = 0; i < agg->output_names.size(); ++i) {
            if (ctx_.symbols.find(agg->output_names[i]) == ctx_.symbols.end()) {
                ColumnInfo info;
                info.name = agg->output_names[i];
                info.column_index = static_cast<uint32_t>(i);
                // Determine type: group keys first, then aggregates.
                if (i < agg->group_keys.size())
                    info.type = getBoundExprType(agg->group_keys[i]);
                else
                    info.type = BoundType::clone(agg->aggregates[i - agg->group_keys.size()].result_type);
                ctx_.symbols[agg->output_names[i]] = std::move(info);
            }
        }

        BoundLogicalOperator current;
        if (!proj_items.empty()) {
            // Build ProjectOp above AggregateOp for complex aggregate expressions.
            auto proj = std::make_unique<BoundProjectOp>();
            for (auto& pi : proj_items) {
                BoundProjectOp::ProjectItem pi_item;
                pi_item.expr = std::move(pi.expr);
                pi_item.alias = std::move(pi.alias);
                pi_item.result_type = getBoundExprType(pi_item.expr);
                proj->items.push_back(std::move(pi_item));

                ColumnInfo out_info;
                out_info.name = proj->items.back().alias;
                out_info.type = proj->items.back().result_type;
                out_info.column_index = static_cast<uint32_t>(proj->items.size() - 1);
                ctx_.return_columns.push_back(std::move(out_info));
            }
            proj->child = std::move(agg);
            current = std::move(proj);
        } else {
            // No complex expressions: AggregateOp output is the final result.
            for (size_t i = 0; i < agg->output_names.size(); ++i) {
                ColumnInfo out_info;
                out_info.name = agg->output_names[i];
                out_info.column_index = static_cast<uint32_t>(i);
                if (i < agg->group_keys.size())
                    out_info.type = getBoundExprType(agg->group_keys[i]);
                else
                    out_info.type = BoundType::clone(agg->aggregates[i - agg->group_keys.size()].result_type);
                ctx_.return_columns.push_back(std::move(out_info));
            }
            current = std::move(agg);
        }

        // ORDER BY, SKIP, LIMIT, DISTINCT

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
    // Check for aggregate functions in WITH items (recursively)
    bool has_aggregate = false;
    for (const auto& item : wc.items) {
        if (hasAggregate(item.expr)) {
            has_aggregate = true;
            break;
        }
    }

    // Collect output names and types for scope reset
    std::vector<std::pair<std::string, BoundType>> with_outputs;

    BoundLogicalOperator current;

    if (has_aggregate) {
        auto agg = std::make_unique<BoundAggregateOp>();

        struct ProjItem {
            binder::BoundExpression expr;
            std::string alias;
        };
        std::vector<ProjItem> proj_items;
        uint32_t anon_idx = 0;

        for (const auto& item : wc.items) {
            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            bool is_simple_agg = std::holds_alternative<std::unique_ptr<cypher::FunctionCall>>(item.expr) &&
                                 hasAggregate(item.expr);

            auto bound_expr = bindExpression(item.expr);
            if (!bound_expr)
                continue;

            if (is_simple_agg) {
                BoundAggregateOp::AggregateItem agg_item;
                agg_item.alias = alias;
                if (std::holds_alternative<std::unique_ptr<BoundFunctionCall>>(*bound_expr)) {
                    auto& fc = std::get<std::unique_ptr<BoundFunctionCall>>(*bound_expr);
                    agg_item.func_def = fc->func_def;
                    agg_item.distinct = fc->distinct;
                    agg_item.result_type = fc->return_type;
                    if (!fc->args.empty())
                        agg_item.argument = std::move(fc->args[0]);
                }
                agg->aggregates.push_back(std::move(agg_item));
                agg->output_names.push_back(alias);
                with_outputs.emplace_back(alias, BoundType::clone(agg->aggregates.back().result_type));
            } else if (hasAggregate(item.expr)) {
                auto full_expr = std::move(*bound_expr);
                walkAndReplaceAggCalls(full_expr, agg->aggregates, anon_idx);

                for (auto& ai : agg->aggregates) {
                    if (ai.alias.starts_with("__agg_")) {
                        size_t idx = agg->output_names.size();
                        agg->output_names.push_back(ai.alias);
                        with_outputs.emplace_back(ai.alias, BoundType::clone(ai.result_type));
                    }
                }
                proj_items.push_back({std::move(full_expr), alias});
            } else {
                agg->group_keys.push_back(std::move(*bound_expr));
                agg->output_names.push_back(alias);
                with_outputs.emplace_back(alias, getBoundExprType(agg->group_keys.back()));
            }
        }

        agg->child = std::move(child);

        if (!proj_items.empty()) {
            auto proj = std::make_unique<BoundProjectOp>();
            for (auto& pi : proj_items) {
                BoundProjectOp::ProjectItem proj_item;
                proj_item.expr = std::move(pi.expr);
                proj_item.alias = std::move(pi.alias);
                proj_item.result_type = getBoundExprType(proj_item.expr);
                proj->items.push_back(std::move(proj_item));
            }
            proj->child = std::move(agg);
            current = std::move(proj);
        } else {
            current = std::move(agg);
        }
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
