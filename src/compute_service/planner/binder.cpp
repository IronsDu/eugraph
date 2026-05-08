#include "compute_service/planner/binder.hpp"

#include "compute_service/function/batch_ops.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace eugraph {
namespace binder {

namespace {

cypher::Expression cloneExpression(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> cypher::Expression {
            using T = std::decay_t<decltype(ptr)>;
            using Elem = typename T::element_type;
            if constexpr (std::is_same_v<Elem, cypher::Literal>) {
                return std::make_unique<cypher::Literal>(*ptr);
            } else if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                return std::make_unique<cypher::Variable>(*ptr);
            } else if constexpr (std::is_same_v<Elem, cypher::Parameter>) {
                return std::make_unique<cypher::Parameter>(*ptr);
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                auto c = std::make_unique<cypher::BinaryOp>();
                c->op = ptr->op;
                c->left = cloneExpression(ptr->left);
                c->right = cloneExpression(ptr->right);
                return c;
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                auto c = std::make_unique<cypher::UnaryOp>();
                c->op = ptr->op;
                c->operand = cloneExpression(ptr->operand);
                return c;
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                auto c = std::make_unique<cypher::FunctionCall>();
                c->name = ptr->name;
                c->distinct = ptr->distinct;
                for (const auto& a : ptr->args)
                    c->args.push_back(cloneExpression(a));
                return c;
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                auto c = std::make_unique<cypher::PropertyAccess>();
                c->object = cloneExpression(ptr->object);
                c->property = ptr->property;
                return c;
            } else if constexpr (std::is_same_v<Elem, cypher::LabelCastExpr>) {
                auto c = std::make_unique<cypher::LabelCastExpr>();
                c->object = cloneExpression(ptr->object);
                c->label = ptr->label;
                return c;
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                auto c = std::make_unique<cypher::ListExpr>();
                for (const auto& e : ptr->elements)
                    c->elements.push_back(cloneExpression(e));
                return c;
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                auto c = std::make_unique<cypher::MapExpr>();
                for (const auto& [k, v] : ptr->entries)
                    c->entries.emplace_back(k, cloneExpression(v));
                return c;
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                auto c = std::make_unique<cypher::CaseExpr>();
                if (ptr->subject)
                    c->subject = cloneExpression(*ptr->subject);
                for (const auto& [w, t] : ptr->when_thens)
                    c->when_thens.emplace_back(cloneExpression(w), cloneExpression(t));
                if (ptr->else_expr)
                    c->else_expr = cloneExpression(*ptr->else_expr);
                return c;
            } else {
                return std::make_unique<cypher::Literal>();
            }
        },
        expr);
}

} // anonymous namespace

// ==================== Public Expression Binding API ====================

void Binder::registerColumn(const std::string& name, BoundType type) {
    uint32_t idx = static_cast<uint32_t>(ctx_.symbols.size());
    ColumnInfo info;
    info.name = name;
    info.type = std::move(type);
    info.column_index = idx;
    ctx_.symbols[name] = std::move(info);
    ctx_.next_column_index = idx + 1;
}

// ==================== Statement Binding ====================

std::optional<BoundStatement> Binder::bind(const cypher::Statement& stmt) {
    errors_.clear();
    ctx_ = BindContext{};

    return std::visit(
        [this](const auto& ptr) -> std::optional<BoundStatement> {
            using T = std::decay_t<decltype(ptr)>;
            using Elem = typename T::element_type;

            if constexpr (std::is_same_v<Elem, cypher::RegularQuery>) {
                BoundStatement result;
                if (bindRegularQuery(*ptr, result))
                    return std::make_optional(std::move(result));
                return std::nullopt;
            } else if constexpr (std::is_same_v<Elem, cypher::ExplainStatement>) {
                if (ptr->query) {
                    BoundStatement result;
                    if (bindRegularQuery(*ptr->query, result))
                        return std::make_optional(std::move(result));
                }
                error("EXPLAIN with no query");
                return std::nullopt;
            } else {
                error("CALL statement not yet supported in binder");
                return std::nullopt;
            }
        },
        stmt);
}

bool Binder::bindRegularQuery(const cypher::RegularQuery& query, BoundStatement& result) {
    if (!query.unions.empty()) {
        error("UNION not yet supported");
        return false;
    }
    return bindSingleQuery(query.first, result.plan);
}

bool Binder::bindSingleQuery(const cypher::SingleQuery& query, BoundLogicalPlan& plan) {
    bool first_clause = true;
    std::optional<BoundLogicalOperator> current;

    for (const auto& clause : query.clauses) {
        std::optional<BoundLogicalOperator> op = std::visit(
            [this, &current, &first_clause](const auto& ptr) -> std::optional<BoundLogicalOperator> {
                using T = std::decay_t<decltype(ptr)>;
                using Elem = typename T::element_type;

                if constexpr (std::is_same_v<Elem, cypher::MatchClause>) {
                    if (!first_clause && !current) {
                        error("Cannot have MATCH without preceding context");
                        return std::nullopt;
                    }
                    auto match_op = bindMatch(*ptr);
                    if (match_op) {
                        // If there was a previous clause, chain it
                        if (current) {
                            // Replace child of the new match's root with current
                            // For now, we return the match as the new root
                            // (In practice, hold previous as sibling or child)
                        }
                    }
                    first_clause = false;
                    return match_op;
                } else if constexpr (std::is_same_v<Elem, cypher::ReturnClause>) {
                    if (!current) {
                        error("Nothing to return");
                        return std::nullopt;
                    }
                    return bindReturn(*ptr, std::move(*current));
                } else if constexpr (std::is_same_v<Elem, cypher::WithClause>) {
                    error("WITH clause not yet supported in binder");
                    return std::nullopt;
                } else if constexpr (std::is_same_v<Elem, cypher::CreateClause>) {
                    return bindCreate(*ptr, std::move(current));
                } else if constexpr (std::is_same_v<Elem, cypher::SetClause>) {
                    if (!current) {
                        error("SET without preceding clause");
                        return std::nullopt;
                    }
                    return bindSet(*ptr, std::move(*current));
                } else if constexpr (std::is_same_v<Elem, cypher::RemoveClause>) {
                    if (!current) {
                        error("REMOVE without preceding clause");
                        return std::nullopt;
                    }
                    return bindRemove(*ptr, std::move(*current));
                } else {
                    error("Clause type not yet supported in binder");
                    return std::nullopt;
                }
            },
            clause);

        if (errors_.empty()) {
            current = std::move(op);
        }
    }

    if (errors_.empty() && current) {
        plan.root = std::move(*current);
        // Extract output schema from the final projection/aggregate
        // For now, just use all symbols as schema
        for (const auto& [name, col_info] : ctx_.symbols) {
            plan.output_schema.push_back(col_info);
        }
        return true;
    }
    return false;
}

// ==================== Clause Binding ====================

std::optional<BoundLogicalOperator> Binder::bindMatch(const cypher::MatchClause& match) {
    if (match.patterns.empty()) {
        error("MATCH clause has no patterns");
        return std::nullopt;
    }

    std::optional<BoundLogicalOperator> current;

    for (size_t pi = 0; pi < match.patterns.size(); ++pi) {
        const auto& pp = match.patterns[pi];
        const auto& element = pp.element;

        // Bind start node
        std::string start_var;
        uint32_t start_col;
        std::optional<LabelId> start_label;
        std::vector<uint16_t> start_prop_ids;
        if (!bindNodePattern(element.node, start_var, start_col, start_label, start_prop_ids))
            return std::nullopt;

        // Create scan operator
        if (start_label) {
            BoundLabelScanOp scan;
            scan.variable = start_var;
            scan.column_index = start_col;
            scan.label_id = *start_label;
            scan.label_name = element.node.labels[0];
            scan.prop_ids = start_prop_ids;
            current = scan;
        } else {
            BoundScanOp scan;
            scan.variable = start_var;
            scan.column_index = start_col;
            scan.prop_ids = start_prop_ids;
            current = scan;
        }

        // Process inline properties on start node as filter
        std::optional<BoundLogicalOperator> inline_filter;
        if (element.node.properties && current) {
            for (const auto& [prop_name, prop_expr] : element.node.properties->entries) {
                // Create the property access expression and equality filter
                auto var_expr = std::make_unique<cypher::Variable>();
                var_expr->name = start_var;
                auto prop_access = std::make_unique<cypher::PropertyAccess>();
                prop_access->object = std::move(var_expr);
                prop_access->property = prop_name;

                auto eq = std::make_unique<cypher::BinaryOp>();
                eq->op = cypher::BinaryOperator::EQ;
                eq->left = std::move(prop_access);
                eq->right = cloneExpression(prop_expr);

                auto filter_pred = bindExpression(cypher::Expression(std::move(eq)));
                if (filter_pred && current) {
                    BoundFilterOp filter;
                    filter.predicate = std::move(*filter_pred);
                    filter.child = std::move(*current);
                    current = std::make_unique<BoundFilterOp>(std::move(filter));
                }
            }
        }

        // Process chain: expand hops
        for (const auto& [rel_pat, node_pat] : element.chain) {
            if (!current)
                break;

            // Bind relationship
            std::string edge_var;
            uint32_t edge_col;
            std::vector<EdgeLabelId> edge_label_ids;
            std::vector<uint16_t> edge_prop_ids;
            if (!bindRelationshipPattern(rel_pat, edge_var, edge_col, edge_label_ids, edge_prop_ids))
                return std::nullopt;

            // Bind target node
            std::string dst_var;
            uint32_t dst_col;
            std::optional<LabelId> dst_label;
            std::vector<uint16_t> dst_prop_ids;
            if (!bindNodePattern(node_pat, dst_var, dst_col, dst_label, dst_prop_ids))
                return std::nullopt;

            // Create expand operator
            auto expand = std::make_unique<BoundExpandOp>();
            expand->src_variable = start_var;
            expand->src_column_index = start_col;
            expand->edge_variable = edge_var;
            expand->edge_column_index = edge_col;
            expand->dst_variable = dst_var;
            expand->dst_column_index = dst_col;
            expand->edge_label_ids = edge_label_ids;
            expand->direction = rel_pat.direction;
            expand->edge_prop_ids = edge_prop_ids;
            expand->dst_prop_ids = dst_prop_ids;
            expand->child = std::move(*current);
            current = std::move(expand);

            // Inline properties on the relationship
            if (rel_pat.properties && current) {
                for (const auto& [prop_name, prop_expr] : rel_pat.properties->entries) {
                    auto var_expr = std::make_unique<cypher::Variable>();
                    var_expr->name = edge_var;
                    auto prop_access = std::make_unique<cypher::PropertyAccess>();
                    prop_access->object = std::move(var_expr);
                    prop_access->property = prop_name;

                    auto eq = std::make_unique<cypher::BinaryOp>();
                    eq->op = cypher::BinaryOperator::EQ;
                    eq->left = std::move(prop_access);
                    eq->right = cloneExpression(prop_expr);

                    auto filter_pred = bindExpression(cypher::Expression(std::move(eq)));
                    if (filter_pred && current) {
                        BoundFilterOp filter;
                        filter.predicate = std::move(*filter_pred);
                        filter.child = std::move(*current);
                        current = std::make_unique<BoundFilterOp>(std::move(filter));
                    }
                }
            }

            // Inline properties on the target node
            if (node_pat.properties && current) {
                for (const auto& [prop_name, prop_expr] : node_pat.properties->entries) {
                    auto var_expr = std::make_unique<cypher::Variable>();
                    var_expr->name = dst_var;
                    auto prop_access = std::make_unique<cypher::PropertyAccess>();
                    prop_access->object = std::move(var_expr);
                    prop_access->property = prop_name;

                    auto eq = std::make_unique<cypher::BinaryOp>();
                    eq->op = cypher::BinaryOperator::EQ;
                    eq->left = std::move(prop_access);
                    eq->right = cloneExpression(prop_expr);

                    auto filter_pred = bindExpression(cypher::Expression(std::move(eq)));
                    if (filter_pred && current) {
                        BoundFilterOp filter;
                        filter.predicate = std::move(*filter_pred);
                        filter.child = std::move(*current);
                        current = std::make_unique<BoundFilterOp>(std::move(filter));
                    }
                }
            }

            // This dst becomes the src for the next chain hop
            start_var = dst_var;
            start_col = dst_col;
        }

        // Handle named path variable
        if (pp.variable && current) {
            auto path_build = std::make_unique<BoundPathBuildOp>();
            path_build->path_variable = *pp.variable;
            path_build->path_column_index = nextColumnIndex();

            // Collect all element variables in the path
            path_build->element_variables.clear();
            const auto& first_node = element.node;
            if (first_node.variable)
                path_build->element_variables.push_back(*first_node.variable);
            for (const auto& [rel, node] : element.chain) {
                if (rel.variable)
                    path_build->element_variables.push_back(*rel.variable);
                if (node.variable)
                    path_build->element_variables.push_back(*node.variable);
            }
            path_build->child = std::move(*current);

            ctx_.symbols[path_build->path_variable] = makeColumnInfo(path_build->path_variable, BoundType::Path());
            current = std::move(path_build);
        }
    }

    // Bind WHERE predicate
    if (match.where_pred && current) {
        auto where_op = bindWhere(*match.where_pred, std::move(*current));
        current = std::move(where_op);
    }

    return current;
}

std::optional<BoundLogicalOperator> Binder::bindReturn(const cypher::ReturnClause& ret, BoundLogicalOperator child) {
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

            if (is_agg) {
                // Extract function name and argument from the expression
                BoundAggregateOp::AggregateItem agg_item;
                agg_item.alias = std::move(alias);
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
            spdlog::info("AGG alias[{}]='{}' registered={}", i, agg->output_names[i], ctx_.symbols.count(agg->output_names[i]));
            if (ctx_.symbols.find(agg->output_names[i]) == ctx_.symbols.end()) {
                BoundType col_type = (i < agg->group_keys.size())
                    ? getBoundExprType(agg->group_keys[i])
                    : BoundType::Any();
                ColumnInfo info;
                info.name = agg->output_names[i];
                info.type = std::move(col_type);
                info.column_index = static_cast<uint32_t>(i);
                ctx_.symbols[agg->output_names[i]] = std::move(info);
            }
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
            auto bound_skip = bindExpression(*ret.skip);
            if (bound_skip && std::holds_alternative<BoundLiteral>(*bound_skip)) {
                auto& lit = std::get<BoundLiteral>(*bound_skip);
                if (std::holds_alternative<int64_t>(lit.value)) {
                    auto skip = std::make_unique<BoundSkipOp>();
                    skip->count = std::get<int64_t>(lit.value);
                    skip->child = std::move(current);
                    current = std::move(skip);
                }
            }
        }
        if (ret.limit) {
            auto bound_limit = bindExpression(*ret.limit);
            if (bound_limit && std::holds_alternative<BoundLiteral>(*bound_limit)) {
                auto& lit = std::get<BoundLiteral>(*bound_limit);
                if (std::holds_alternative<int64_t>(lit.value)) {
                    auto limit = std::make_unique<BoundLimitOp>();
                    limit->count = std::get<int64_t>(lit.value);
                    limit->child = std::move(current);
                    current = std::move(limit);
                }
            }
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

        std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
        BoundProjectOp::ProjectItem proj_item;
        proj_item.expr = std::move(*bound_expr);
        proj_item.alias = std::move(alias);
        proj_item.result_type = getBoundExprType(proj_item.expr);
        proj->items.push_back(std::move(proj_item));
    }
    proj->child = std::move(child);

    // Register RETURN aliases so ORDER BY can reference them.
    // Column index = projection output position.
    for (size_t i = 0; i < proj->items.size(); ++i) {
        const auto& proj_item = proj->items[i];
        if (ctx_.symbols.find(proj_item.alias) == ctx_.symbols.end()) {
            ColumnInfo info;
            info.name = proj_item.alias;
            info.type = proj_item.result_type;
            info.column_index = static_cast<uint32_t>(i);
            ctx_.symbols[proj_item.alias] = std::move(info);
        }
    }

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

    // SKIP, LIMIT, DISTINCT
    if (ret.skip) {
        auto bound_skip = bindExpression(*ret.skip);
        if (bound_skip && std::holds_alternative<BoundLiteral>(*bound_skip)) {
            auto& lit = std::get<BoundLiteral>(*bound_skip);
            if (std::holds_alternative<int64_t>(lit.value)) {
                auto skip = std::make_unique<BoundSkipOp>();
                skip->count = std::get<int64_t>(lit.value);
                skip->child = std::move(current);
                current = std::move(skip);
            }
        }
    }
    if (ret.limit) {
        auto bound_limit = bindExpression(*ret.limit);
        if (bound_limit && std::holds_alternative<BoundLiteral>(*bound_limit)) {
            auto& lit = std::get<BoundLiteral>(*bound_limit);
            if (std::holds_alternative<int64_t>(lit.value)) {
                auto limit = std::make_unique<BoundLimitOp>();
                limit->count = std::get<int64_t>(lit.value);
                limit->child = std::move(current);
                current = std::move(limit);
            }
        }
    }
    if (ret.distinct) {
        auto distinct = std::make_unique<BoundDistinctOp>();
        distinct->child = std::move(current);
        current = std::move(distinct);
    }

    return current;
}

std::optional<BoundLogicalOperator> Binder::bindWhere(const cypher::Expression& pred, BoundLogicalOperator child,
                                                      std::optional<BoundLogicalOperator> extra_filter_child) {
    auto bound_pred = bindExpression(pred);
    if (!bound_pred)
        return std::nullopt;

    auto filter = std::make_unique<BoundFilterOp>();
    filter->predicate = std::move(*bound_pred);
    filter->child = std::move(child);
    return filter;
}

std::optional<BoundLogicalOperator> Binder::bindWith(const cypher::WithClause& /*with_clause*/,
                                                     BoundLogicalOperator /*child*/) {
    error("WITH clause not yet supported in binder");
    return std::nullopt;
}

std::optional<BoundLogicalOperator> Binder::bindCreate(const cypher::CreateClause& create,
                                                       std::optional<BoundLogicalOperator> child) {
    std::optional<BoundLogicalOperator> current;

    for (const auto& pp : create.patterns) {
        const auto& element = pp.element;

        // Create start node
        std::string start_var;
        uint32_t start_col;
        std::optional<LabelId> start_label;
        std::vector<uint16_t> start_prop_ids;
        if (!bindNodePattern(element.node, start_var, start_col, start_label, start_prop_ids))
            return std::nullopt;

        auto create_node = std::make_unique<BoundCreateNodeOp>();
        create_node->variable = start_var;
        create_node->label_id = start_label.value_or(INVALID_LABEL_ID);

        // Bind inline properties as expressions
        if (element.node.properties && start_label) {
            const auto* ldef = catalog_.lookupLabel(*start_label);
            if (ldef) {
                for (const auto& [prop_name, prop_expr] : element.node.properties->entries) {
                    auto* pd = catalog_.lookupProperty(*start_label, prop_name);
                    if (pd) {
                        auto bound_val = bindExpression(prop_expr);
                        if (bound_val)
                            create_node->properties.emplace_back(pd->id, std::move(*bound_val));
                    } else {
                        error("Property '" + prop_name + "' not found on label '" + ldef->name + "'");
                    }
                }
            }
        }

        if (child) {
            create_node->child = std::move(*child);
        }
        BoundLogicalOperator node_op = std::move(create_node);

        // Create edges in chain
        current = std::move(node_op);
        for (const auto& [rel_pat, node_pat] : element.chain) {
            if (!current)
                break;

            // Bind relationship
            std::string edge_var;
            uint32_t edge_col;
            std::vector<EdgeLabelId> edge_label_ids;
            std::vector<uint16_t> edge_prop_ids;
            if (!bindRelationshipPattern(rel_pat, edge_var, edge_col, edge_label_ids, edge_prop_ids))
                return std::nullopt;

            // Bind target node
            std::string dst_var;
            uint32_t dst_col;
            std::optional<LabelId> dst_label;
            std::vector<uint16_t> dst_prop_ids;
            if (!bindNodePattern(node_pat, dst_var, dst_col, dst_label, dst_prop_ids))
                return std::nullopt;

            // Create target node
            auto create_dst = std::make_unique<BoundCreateNodeOp>();
            create_dst->variable = dst_var;
            create_dst->label_id = dst_label.value_or(INVALID_LABEL_ID);
            if (node_pat.properties && dst_label) {
                for (const auto& [prop_name, prop_expr] : node_pat.properties->entries) {
                    auto* pd = catalog_.lookupProperty(*dst_label, prop_name);
                    if (pd) {
                        auto bound_val = bindExpression(prop_expr);
                        if (bound_val)
                            create_dst->properties.emplace_back(pd->id, std::move(*bound_val));
                    }
                }
            }
            create_dst->child = std::move(*current);

            // Create edge between them
            auto create_edge = std::make_unique<BoundCreateEdgeOp>();
            create_edge->variable = edge_var;
            create_edge->label_id = edge_label_ids.empty() ? INVALID_EDGE_LABEL_ID : edge_label_ids[0];
            create_edge->src_variable = start_var;
            create_edge->dst_variable = dst_var;
            if (rel_pat.properties && create_edge->label_id != INVALID_EDGE_LABEL_ID) {
                for (const auto& [prop_name, prop_expr] : rel_pat.properties->entries) {
                    auto* pd = catalog_.lookupEdgeLabelProperty(create_edge->label_id, prop_name);
                    if (pd) {
                        auto bound_val = bindExpression(prop_expr);
                        if (bound_val)
                            create_edge->properties.emplace_back(pd->id, std::move(*bound_val));
                    }
                }
            }
            BoundLogicalOperator dst_node_op = std::move(create_dst);
            create_edge->child = std::move(dst_node_op);
            current = std::move(create_edge);

            start_var = dst_var;
        }
    }

    return current;
}

std::optional<BoundLogicalOperator> Binder::bindSet(const cypher::SetClause& set, BoundLogicalOperator child) {
    auto set_op = std::make_unique<BoundSetOp>();

    for (const auto& item : set.items) {
        BoundSetOp::SetItem bound_item;
        switch (item.kind) {
        case cypher::SetItemKind::SET_PROPERTY: {
            // Extract variable name from target expression
            std::string var_name;
            std::string prop_name;
            std::optional<std::string> label_name; // for ::Label syntax

            std::visit(
                [&](const auto& ptr) {
                    using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                    if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                        // Try to extract variable from object
                        if (auto* var = std::get_if<std::unique_ptr<cypher::Variable>>(&ptr->object)) {
                            var_name = (*var)->name;
                            prop_name = ptr->property;
                        } else if (auto* lc = std::get_if<std::unique_ptr<cypher::LabelCastExpr>>(&ptr->object)) {
                            if (auto* v = std::get_if<std::unique_ptr<cypher::Variable>>(&(*lc)->object)) {
                                var_name = (*v)->name;
                                label_name = (*lc)->label;
                                prop_name = ptr->property;
                            }
                        }
                    }
                },
                item.target);

            if (var_name.empty()) {
                error("Cannot resolve SET target variable");
                continue;
            }

            bound_item.kind = BoundSetOp::ItemKind::SET_PROPERTY;
            bound_item.target_variable = var_name;

            if (label_name) {
                LabelId lid = catalog_.labelNameToId(*label_name);
                if (lid != INVALID_LABEL_ID) {
                    auto* pd = catalog_.lookupProperty(lid, prop_name);
                    if (pd) {
                        bound_item.prop_id = pd->id;
                        bound_item.label_id = lid;
                    }
                }
            } else {
                // Weak type: try to find the property in all labels of the variable
                auto* col = ctx_.lookup(var_name);
                if (col && col->source_label) {
                    auto* pd = catalog_.lookupProperty(*col->source_label, prop_name);
                    if (pd) {
                        bound_item.prop_id = pd->id;
                        bound_item.label_id = *col->source_label;
                    }
                }
            }

            if (item.value) {
                auto bound_val = bindExpression(*item.value);
                if (bound_val)
                    bound_item.value_expr = std::move(*bound_val);
            }
            break;
        }
        case cypher::SetItemKind::SET_LABELS: {
            bound_item.kind = BoundSetOp::ItemKind::SET_LABELS;
            LabelId lid = catalog_.labelNameToId(item.label);
            if (lid != INVALID_LABEL_ID)
                bound_item.label_id = lid;
            std::visit(
                [&](const auto& ptr) {
                    using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                    if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                        bound_item.target_variable = ptr->name;
                    }
                },
                item.target);
            break;
        }
        case cypher::SetItemKind::SET_PROPERTIES:
        case cypher::SetItemKind::SET_DYNAMIC_PROPERTY:
            error("SET properties/dynamic not yet supported in binder");
            continue;
        }
        set_op->items.push_back(std::move(bound_item));
    }

    set_op->child = std::move(child);
    return set_op;
}

std::optional<BoundLogicalOperator> Binder::bindRemove(const cypher::RemoveClause& rem, BoundLogicalOperator child) {
    auto rem_op = std::make_unique<BoundRemoveOp>();

    for (const auto& item : rem.items) {
        BoundRemoveOp::RemoveItem bound_item;
        if (item.kind == cypher::RemoveItem::Kind::LABEL) {
            bound_item.kind = BoundRemoveOp::ItemKind::REMOVE_LABEL;
            LabelId lid = catalog_.labelNameToId(item.name);
            if (lid != INVALID_LABEL_ID)
                bound_item.label_id = lid;
        } else {
            bound_item.kind = BoundRemoveOp::ItemKind::REMOVE_PROPERTY;
        }
        std::visit(
            [&](const auto& ptr) {
                using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                    bound_item.target_variable = ptr->name;
                }
            },
            item.target);

        rem_op->items.push_back(std::move(bound_item));
    }

    rem_op->child = std::move(child);
    return rem_op;
}

// ==================== Expression Binding ====================

std::optional<BoundExpression> Binder::bindExpression(const cypher::Expression& expr) {
    return std::visit(
        [this](const auto& ptr) -> std::optional<BoundExpression> {
            using T = std::decay_t<decltype(ptr)>;
            using Elem = typename T::element_type;

            if constexpr (std::is_same_v<Elem, cypher::Literal>) {
                // Convert AST literal to bound literal
                return BoundExpression(std::visit(
                    [](const auto& v) -> BoundExpression {
                        using V = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<V, cypher::NullValue>) {
                            return BoundLiteral{};
                        } else if constexpr (std::is_same_v<V, bool>) {
                            return BoundLiteral(v);
                        } else if constexpr (std::is_same_v<V, int64_t>) {
                            return BoundLiteral(v);
                        } else if constexpr (std::is_same_v<V, double>) {
                            return BoundLiteral(v);
                        } else if constexpr (std::is_same_v<V, std::string>) {
                            return BoundLiteral(std::string(v));
                        }
                        return BoundLiteral{};
                    },
                    ptr->value));
            } else if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                auto* col = ctx_.lookup(ptr->name);
                if (!col) {
                    error("Variable '" + ptr->name + "' not defined");
                    return std::nullopt;
                }
                return BoundExpression(BoundColumnRef(col->column_index, col->type, ptr->name));
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                auto left = bindExpression(ptr->left);
                auto right = bindExpression(ptr->right);
                if (!left || !right)
                    return std::nullopt;

                const BoundType& left_type = getBoundExprType(*left);
                const BoundType& right_type = getBoundExprType(*right);

                std::string err_msg;
                BoundType result_type = inferBinaryOpType(ptr->op, left_type, right_type, err_msg);
                if (!err_msg.empty()) {
                    error(err_msg);
                    return std::nullopt;
                }

                auto bin = std::make_unique<BoundBinaryOp>();
                bin->op = ptr->op;
                bin->left = std::move(*left);
                bin->right = std::move(*right);
                bin->result_type = std::move(result_type);
                bin->batch_fn = function::resolveBinaryBatchFn(bin->op, left_type.kind, right_type.kind);
                return BoundExpression(std::move(bin));
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                auto operand = bindExpression(ptr->operand);
                if (!operand)
                    return std::nullopt;

                const BoundType& operand_type = getBoundExprType(*operand);
                std::string err_msg;
                BoundType result_type = inferUnaryOpType(ptr->op, operand_type, err_msg);
                if (!err_msg.empty()) {
                    error(err_msg);
                    return std::nullopt;
                }

                auto un = std::make_unique<BoundUnaryOp>();
                un->op = ptr->op;
                un->operand = std::move(*operand);
                un->result_type = std::move(result_type);
                un->batch_fn = function::resolveUnaryBatchFn(un->op, operand_type.kind);
                return BoundExpression(std::move(un));
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                // Look up in function registry
                std::vector<binder::BoundType> arg_types;
                std::vector<BoundExpression> bound_args;
                for (const auto& arg : ptr->args) {
                    auto bound_arg = bindExpression(arg);
                    if (!bound_arg)
                        return std::nullopt;
                    arg_types.push_back(getBoundExprType(*bound_arg));
                    bound_args.push_back(std::move(*bound_arg));
                }

                const function::FunctionDef* func = func_registry_.lookup(ptr->name, arg_types);
                if (!func) {
                    std::string sig = ptr->name + "(";
                    for (size_t i = 0; i < arg_types.size(); ++i) {
                        if (i > 0)
                            sig += ", ";
                        sig += arg_types[i].toString();
                    }
                    sig += ")";
                    error("Function not found: " + sig);
                    return std::nullopt;
                }

                auto fc = std::make_unique<BoundFunctionCall>();
                fc->func_def = func;
                fc->args = std::move(bound_args);
                fc->return_type = BoundType::clone(func->return_type);
                fc->distinct = ptr->distinct;
                return BoundExpression(std::move(fc));
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                auto obj = bindExpression(ptr->object);
                if (!obj)
                    return std::nullopt;

                const BoundType& obj_type = getBoundExprType(*obj);

                if (obj_type.kind == BoundTypeKind::VERTEX) {
                    // Check if the object is a LabelCastExpr (strong type)
                    if (std::holds_alternative<std::unique_ptr<BoundLabelCast>>(*obj)) {
                        auto& lc = std::get<std::unique_ptr<BoundLabelCast>>(*obj);
                        auto* pd = catalog_.lookupProperty(lc->label_id, ptr->property);
                        if (!pd) {
                            const auto* ldef = catalog_.lookupLabel(lc->label_id);
                            std::string label_name = ldef ? ldef->name : "?";
                            error("Property '" + ptr->property + "' not found on label '" + label_name + "'");
                            return std::nullopt;
                        }
                        auto prop_ref = std::make_unique<BoundPropertyRef>();
                        prop_ref->object = std::move(*obj);
                        BoundPropertyRef::ResolvedProperty rp;
                        rp.label_id = lc->label_id;
                        rp.prop_id = pd->id;
                        rp.type = propertyTypeToBoundType(pd->type);
                        prop_ref->candidates.push_back(rp);
                        prop_ref->result_type = rp.type;

                        // Add projection requirement
                        // Extract variable name from label cast object
                        auto& inner = lc->object;
                        if (std::holds_alternative<BoundColumnRef>(inner)) {
                            auto& col_ref = std::get<BoundColumnRef>(inner);
                            ctx_.addPropertyRequirement(col_ref.name, lc->label_id, pd->id);
                        }

                        return BoundExpression(std::move(prop_ref));
                    }

                    // Weak type: check if object is a ColumnRef
                    if (std::holds_alternative<BoundColumnRef>(*obj)) {
                        auto& col_ref = std::get<BoundColumnRef>(*obj);
                        // Collect all labels for this variable (from context)
                        // For now, we scan ALL labels in the catalog
                        LabelIdSet all_labels;
                        for (const auto& [lid, ldef] : catalog_.allLabels()) {
                            all_labels.insert(lid);
                        }

                        auto candidates = catalog_.lookupPropertyAcrossLabels(all_labels, ptr->property);
                        if (candidates.empty()) {
                            error("Property '" + ptr->property + "' not found on any label for variable '" +
                                  col_ref.name + "'");
                            return std::nullopt;
                        }

                        auto prop_ref = std::make_unique<BoundPropertyRef>();
                        prop_ref->object = std::move(*obj);
                        BoundType merged = BoundType::Null();
                        for (auto& [lid, pd] : candidates) {
                            BoundPropertyRef::ResolvedProperty rp;
                            rp.label_id = lid;
                            rp.prop_id = pd->id;
                            rp.type = propertyTypeToBoundType(pd->type);
                            merged = BoundType::merge(merged, rp.type);
                            prop_ref->candidates.push_back(rp);

                            // Add projection requirement
                            ctx_.addPropertyRequirement(col_ref.name, lid, pd->id);
                        }
                        prop_ref->result_type = merged;

                        return BoundExpression(std::move(prop_ref));
                    }
                }

                if (obj_type.kind == BoundTypeKind::EDGE) {
                    // Edge property: try to look up across all edge labels
                    LabelIdSet all_edge_labels;
                    // For now, collect from catalog
                    auto prop_ref = std::make_unique<BoundPropertyRef>();
                    prop_ref->object = std::move(*obj);
                    prop_ref->result_type = BoundType::Any(); // Weak for now
                    return BoundExpression(std::move(prop_ref));
                }

                error("Property access on non-vertex/edge type: " + obj_type.toString());
                return std::nullopt;
            } else if constexpr (std::is_same_v<Elem, cypher::LabelCastExpr>) {
                auto obj = bindExpression(ptr->object);
                if (!obj)
                    return std::nullopt;

                LabelId lid = catalog_.labelNameToId(ptr->label);
                if (lid == INVALID_LABEL_ID) {
                    error("Label '" + ptr->label + "' does not exist");
                    return std::nullopt;
                }

                auto lc = std::make_unique<BoundLabelCast>();
                lc->object = std::move(*obj);
                lc->label_id = lid;
                lc->result_type = BoundType::Vertex();
                return BoundExpression(std::move(lc));
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                auto bl = std::make_unique<BoundList>();
                BoundType merged_elem = BoundType::Null();
                for (const auto& elem : ptr->elements) {
                    auto bound_elem = bindExpression(elem);
                    if (!bound_elem)
                        return std::nullopt;
                    merged_elem = BoundType::merge(merged_elem, getBoundExprType(*bound_elem));
                    bl->elements.push_back(std::move(*bound_elem));
                }
                bl->element_type = merged_elem;
                bl->result_type = BoundType::List(BoundType::clone(merged_elem));
                return BoundExpression(std::move(bl));
            } else if constexpr (std::is_same_v<Elem, cypher::Parameter>) {
                return BoundExpression(BoundParameter(ptr->name, BoundType::Any()));
            } else {
                // Other types not yet supported
                return std::nullopt;
            }
        },
        expr);
}

BoundType Binder::inferBinaryOpType(cypher::BinaryOperator op, const BoundType& left_type, const BoundType& right_type,
                                    std::string& error_msg) {
    switch (op) {
    case cypher::BinaryOperator::EQ:
    case cypher::BinaryOperator::NEQ:
    case cypher::BinaryOperator::LT:
    case cypher::BinaryOperator::GT:
    case cypher::BinaryOperator::LTE:
    case cypher::BinaryOperator::GTE:
        // Comparison: operands must be compatible
        if (left_type.canCastTo(right_type) || right_type.canCastTo(left_type))
            return BoundType::Bool();
        error_msg = "Cannot compare " + left_type.toString() + " with " + right_type.toString();
        return BoundType::Any();

    case cypher::BinaryOperator::AND:
    case cypher::BinaryOperator::OR:
    case cypher::BinaryOperator::XOR:
        if (left_type.kind == BoundTypeKind::BOOL || left_type.kind == BoundTypeKind::NULL_TYPE ||
            left_type.kind == BoundTypeKind::ANY)
            return BoundType::Bool();
        error_msg = "Logical operators require boolean operands";
        return BoundType::Any();

    case cypher::BinaryOperator::ADD:
    case cypher::BinaryOperator::SUB:
    case cypher::BinaryOperator::MUL:
    case cypher::BinaryOperator::DIV:
    case cypher::BinaryOperator::MOD:
    case cypher::BinaryOperator::POW:
        // Arithmetic: INT64 or DOUBLE, with implicit conversion
        if (left_type == BoundType::Int64() && right_type == BoundType::Int64())
            return BoundType::Int64();
        if ((left_type.kind == BoundTypeKind::INT64 || left_type.kind == BoundTypeKind::DOUBLE) &&
            (right_type.kind == BoundTypeKind::INT64 || right_type.kind == BoundTypeKind::DOUBLE))
            return BoundType::Double();
        error_msg =
            "Arithmetic requires numeric operands: got " + left_type.toString() + " and " + right_type.toString();
        return BoundType::Any();

    case cypher::BinaryOperator::LIST_CONCAT:
        if (left_type.kind == BoundTypeKind::LIST && right_type.kind == BoundTypeKind::LIST)
            return BoundType::Any(); // Simplified
        error_msg = "List concatenation requires list operands";
        return BoundType::Any();

    default:
        error_msg = "Operator not implemented";
        return BoundType::Any();
    }
}

BoundType Binder::inferUnaryOpType(cypher::UnaryOperator op, const BoundType& operand_type, std::string& error_msg) {
    switch (op) {
    case cypher::UnaryOperator::NOT:
        if (operand_type.kind == BoundTypeKind::BOOL || operand_type.kind == BoundTypeKind::NULL_TYPE ||
            operand_type.kind == BoundTypeKind::ANY)
            return BoundType::Bool();
        error_msg = "NOT requires boolean operand, got " + operand_type.toString();
        return BoundType::Any();

    case cypher::UnaryOperator::NEGATE:
        if (operand_type.kind == BoundTypeKind::INT64)
            return BoundType::Int64();
        if (operand_type.kind == BoundTypeKind::DOUBLE)
            return BoundType::Double();
        error_msg = "Negation requires numeric operand, got " + operand_type.toString();
        return BoundType::Any();

    case cypher::UnaryOperator::IS_NULL:
    case cypher::UnaryOperator::IS_NOT_NULL:
        return BoundType::Bool();

    default:
        error_msg = "Unary operator not implemented";
        return BoundType::Any();
    }
}

// ==================== Pattern Binding ====================

bool Binder::bindNodePattern(const cypher::NodePattern& node, std::string& var_name, uint32_t& col_idx,
                             std::optional<LabelId>& label_id, std::vector<uint16_t>& default_prop_ids) {
    var_name = node.variable.value_or("");
    col_idx = nextColumnIndex();

    label_id = std::nullopt;
    if (!node.labels.empty()) {
        LabelId lid = catalog_.labelNameToId(node.labels[0]);
        if (lid == INVALID_LABEL_ID) {
            error("Label '" + node.labels[0] + "' does not exist");
            return false;
        }
        label_id = lid;
    }

    if (!var_name.empty()) {
        ctx_.symbols[var_name] = makeColumnInfo(var_name, BoundType::Vertex(), label_id);
    }

    return true;
}

bool Binder::bindRelationshipPattern(const cypher::RelationshipPattern& rel, std::string& var_name, uint32_t& col_idx,
                                     std::vector<EdgeLabelId>& edge_label_ids,
                                     std::vector<uint16_t>& default_prop_ids) {
    var_name = rel.variable.value_or("");
    col_idx = nextColumnIndex();

    edge_label_ids.clear();
    if (!rel.rel_types.empty()) {
        edge_label_ids = catalog_.resolveEdgeLabelIds(rel.rel_types);
        if (edge_label_ids.empty()) {
            error("Edge type '" + rel.rel_types[0] + "' does not exist");
            return false;
        }
    } else {
        // No type filter: collect all edge labels
        for (const auto& [id, def] : catalog_.allEdgeLabels()) {
            edge_label_ids.push_back(id);
        }
    }

    if (!var_name.empty()) {
        ctx_.symbols[var_name] = makeColumnInfo(var_name, BoundType::Edge());
    }

    return true;
}

// ==================== Helpers ====================

BoundType Binder::propertyTypeToBoundType(PropertyType pt) {
    switch (pt) {
    case PropertyType::BOOL:
        return BoundType::Bool();
    case PropertyType::INT64:
        return BoundType::Int64();
    case PropertyType::DOUBLE:
        return BoundType::Double();
    case PropertyType::STRING:
        return BoundType::String();
    case PropertyType::INT64_ARRAY:
    case PropertyType::DOUBLE_ARRAY:
    case PropertyType::STRING_ARRAY:
        return BoundType::List(BoundType::Any()); // Simplified
    default:
        return BoundType::Any();
    }
}

ColumnInfo Binder::makeColumnInfo(const std::string& name, BoundType type, std::optional<LabelId> source_label,
                                  std::optional<uint16_t> source_prop_id, bool strong_typed) {
    ColumnInfo info;
    info.name = name;
    info.type = std::move(type);
    info.source_label = source_label;
    info.source_prop_id = source_prop_id;
    info.strong_typed = strong_typed;
    return info;
}

} // namespace binder
} // namespace eugraph
