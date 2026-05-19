#include "query/planner/binder.hpp"

#include "query/function/batch_ops.hpp"
#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

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

// P3: convert AST Literal value variant to PropertyValue
PropertyValue literalToPropertyValue(const std::variant<cypher::NullValue, bool, int64_t, double, std::string>& val) {
    return std::visit(
        [](const auto& v) -> PropertyValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, cypher::NullValue>)
                return std::monostate{};
            else
                return v;
        },
        val);
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

                    // Check if this MATCH needs CrossProduct: preceding context exists
                    // but the start variable is not in scope (independent scan after WITH).
                    bool needs_cross = false;
                    if (current.has_value() && !ptr->patterns.empty()) {
                        const auto& node = ptr->patterns[0].element.node;
                        if (!node.variable || !ctx_.lookup(*node.variable)) {
                            needs_cross = true;
                        }
                    }

                    if (needs_cross) {
                        auto left = std::move(*current);
                        auto right = bindMatch(*ptr, std::nullopt, /*skip_where=*/true);
                        if (!right)
                            return std::nullopt;

                        auto join = std::make_unique<BoundBinaryJoinOp>();
                        join->join_type = JoinType::Cross;
                        join->left = std::move(left);
                        join->right = std::move(*right);

                        BoundLogicalOperator result = std::move(join);

                        if (ptr->where_pred) {
                            auto where_op = bindWhere(*ptr->where_pred, std::move(result));
                            if (!where_op)
                                return std::nullopt;
                            result = std::move(*where_op);
                        }

                        first_clause = false;
                        return result;
                    }

                    auto match_op = bindMatch(*ptr, std::move(current));
                    first_clause = false;
                    return match_op;
                } else if constexpr (std::is_same_v<Elem, cypher::ReturnClause>) {
                    if (!current) {
                        current = BoundSingletonOp{};
                    }
                    return bindReturn(*ptr, std::move(*current));
                } else if constexpr (std::is_same_v<Elem, cypher::WithClause>) {
                    if (!current) {
                        // WITH as first clause: create a singleton source
                        current = BoundSingletonOp{};
                    }
                    return bindWith(*ptr, std::move(*current));
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

        // Apply projection pushdown: fill label_prop_ids on scan/expand operators
        applyProjectionPushdown(plan.root);

        // Use return_columns if available (from RETURN clause), otherwise fall back to all symbols
        if (!ctx_.return_columns.empty()) {
            plan.output_schema = std::move(ctx_.return_columns);
        } else {
            for (const auto& [name, col_info] : ctx_.symbols) {
                plan.output_schema.push_back(col_info);
            }
        }
        return true;
    }
    return false;
}

// ==================== Clause Binding ====================

std::optional<BoundLogicalOperator> Binder::bindMatch(const cypher::MatchClause& match,
                                                      std::optional<BoundLogicalOperator> parent, bool skip_where) {
    if (match.patterns.empty()) {
        error("MATCH clause has no patterns");
        return std::nullopt;
    }

    std::optional<BoundLogicalOperator> current;

    for (size_t pi = 0; pi < match.patterns.size(); ++pi) {
        const auto& pp = match.patterns[pi];
        const auto& element = pp.element;

        // Only the first pattern in a correlated MATCH reuses the parent.
        bool correlated = (pi == 0) && parent.has_value();

        // Bind start node
        std::string start_var;
        uint32_t start_col;
        std::optional<LabelId> start_label;
        std::vector<uint16_t> start_prop_ids;

        if (correlated) {
            if (!element.node.variable) {
                error("MATCH after WITH requires a named start variable");
                return std::nullopt;
            }
            auto* col = ctx_.lookup(*element.node.variable);
            if (!col) {
                error("MATCH after WITH on unrelated variable '" + *element.node.variable + "' is not yet supported");
                return std::nullopt;
            }
            if (!bindNodePattern(element.node, start_var, start_col, start_label, start_prop_ids, true))
                return std::nullopt;
            start_col = col->column_index;
        } else {
            if (!bindNodePattern(element.node, start_var, start_col, start_label, start_prop_ids))
                return std::nullopt;
        }

        // Collect bound variable names for path building
        std::vector<std::string> path_element_vars;
        path_element_vars.push_back(start_var);

        // Create scan operator (or reuse parent for correlated MATCH)
        if (correlated) {
            current = std::move(*parent);
        } else if (start_label) {
            BoundLabelScanOp scan;
            scan.variable = start_var;
            scan.column_index = start_col;
            scan.label_id = *start_label;
            scan.label_name = element.node.labels[0];
            current = scan;
        } else {
            BoundScanOp scan;
            scan.variable = start_var;
            scan.column_index = start_col;
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

            if (rel_pat.range.has_value()) {
                // ── Variable-length expand ──

                // P2: named edge variable → LIST<EDGE>
                std::optional<std::string> edge_var;
                if (rel_pat.variable.has_value()) {
                    edge_var = *rel_pat.variable;
                }
                // P3: edge property filters (resolved after edge type binding below)
                std::unordered_map<EdgeLabelId, std::vector<std::pair<uint16_t, PropertyValue>>> edge_prop_filters;
                // P2: undirected allowed (physical op already handles BOTH)

                // Extract min/max from range (both must be integer literals)
                auto extractIntLiteral = [&](const cypher::Expression& expr, int64_t& out) -> bool {
                    if (!std::holds_alternative<std::unique_ptr<cypher::Literal>>(expr)) {
                        error("Variable-length range bounds must be integer literals");
                        return false;
                    }
                    auto& lit = std::get<std::unique_ptr<cypher::Literal>>(expr);
                    if (!std::holds_alternative<int64_t>(lit->value)) {
                        error("Variable-length range bounds must be integer literals");
                        return false;
                    }
                    out = std::get<int64_t>(lit->value);
                    return true;
                };

                int64_t min_hops = 1, max_hops = 1;
                const auto& [min_expr, max_expr] = *rel_pat.range;
                if (!extractIntLiteral(min_expr, min_hops))
                    return std::nullopt;
                if (!extractIntLiteral(max_expr, max_hops))
                    return std::nullopt;

                if (min_hops < 0) {
                    error("Variable-length minimum hop count must be >= 0 (got " + std::to_string(min_hops) + ")");
                    return std::nullopt;
                }
                // P2: unbounded upper range (max=-1 sentinel)
                if (max_hops >= 0 && max_hops < min_hops) {
                    error("Variable-length maximum hops (" + std::to_string(max_hops) + ") must be >= minimum (" +
                          std::to_string(min_hops) + ")");
                    return std::nullopt;
                }

                // Bind edge types
                std::vector<EdgeLabelId> edge_label_ids;
                if (!rel_pat.rel_types.empty()) {
                    edge_label_ids = catalog_.resolveEdgeLabelIds(rel_pat.rel_types);
                    if (edge_label_ids.empty()) {
                        error("Edge type '" + rel_pat.rel_types[0] + "' does not exist");
                        return std::nullopt;
                    }
                } else {
                    for (const auto& [id, def] : catalog_.allEdgeLabels()) {
                        edge_label_ids.push_back(id);
                    }
                }

                // P3: resolve inline edge property filters
                if (rel_pat.properties.has_value()) {
                    for (const auto& [prop_name, prop_expr] : rel_pat.properties->entries) {
                        if (!std::holds_alternative<std::unique_ptr<cypher::Literal>>(prop_expr)) {
                            error("Variable-length edge property filter value must be a literal");
                            return std::nullopt;
                        }
                        auto& lit = std::get<std::unique_ptr<cypher::Literal>>(prop_expr);
                        PropertyValue prop_val = literalToPropertyValue(lit->value);

                        bool found = false;
                        for (auto lid : edge_label_ids) {
                            auto* def = catalog_.lookupEdgeLabelProperty(lid, prop_name);
                            if (def) {
                                edge_prop_filters[lid].emplace_back(def->id, prop_val);
                                found = true;
                            }
                        }
                        if (!found) {
                            error("Edge property '" + prop_name + "' does not exist on the specified edge type(s)");
                            return std::nullopt;
                        }
                    }
                }

                // Bind target node
                std::string dst_var;
                uint32_t dst_col;
                std::optional<LabelId> dst_label;
                std::vector<uint16_t> dst_prop_ids;
                if (!bindNodePattern(node_pat, dst_var, dst_col, dst_label, dst_prop_ids))
                    return std::nullopt;

                // Create varlen expand operator
                auto varlen = std::make_unique<BoundVarLenExpandOp>();
                varlen->src_variable = start_var;
                varlen->src_column_index = start_col;
                varlen->dst_variable = dst_var;
                varlen->dst_column_index = dst_col;
                varlen->edge_label_ids = std::move(edge_label_ids);
                varlen->direction = rel_pat.direction;
                varlen->min_hops = min_hops;
                varlen->max_hops = max_hops;

                // P1: handle named path variable — varlen produces PathValue directly
                if (pp.variable) {
                    if (element.chain.size() > 1) {
                        error("Named path with mixed fixed/varlen chain is not supported yet");
                        return std::nullopt;
                    }
                    varlen->path_variable = *pp.variable;
                    varlen->path_column_index = nextColumnIndex();
                    varlen->path_handled_by_varlen = true;
                    ctx_.symbols[varlen->path_variable] = makeColumnInfo(varlen->path_variable, BoundType::Path());
                }

                // P2: handle named edge variable → LIST<EDGE>
                if (edge_var) {
                    varlen->edge_variable = *edge_var;
                    varlen->edge_column_index = nextColumnIndex();
                    ctx_.symbols[varlen->edge_variable] =
                        makeColumnInfo(varlen->edge_variable, BoundType::List(BoundType::Edge()));
                }

                // P3: edge property filters
                varlen->edge_prop_filters = std::move(edge_prop_filters);

                varlen->child = std::move(*current);
                current = std::move(varlen);

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

                // Varlen has no edge variable, so no edge-property filter needed.
                // For path_element_vars: varlen has no named edge, use dst only.
                path_element_vars.push_back(dst_var);

                start_var = dst_var;
                start_col = dst_col;
                continue; // skip the normal Expand logic below
            }

            // ── Fixed-length (original) expand ──

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

            path_element_vars.push_back(edge_var);
            path_element_vars.push_back(dst_var);
            expand->dst_variable = dst_var;
            expand->dst_column_index = dst_col;
            expand->edge_label_ids = edge_label_ids;
            expand->direction = rel_pat.direction;
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
            // Check if varlen expand already handles the path
            bool path_already_handled = false;
            std::visit(
                [&](auto& op) {
                    using T = std::decay_t<decltype(op)>;
                    if constexpr (std::is_same_v<T, std::unique_ptr<BoundVarLenExpandOp>>) {
                        if (op && op->path_handled_by_varlen)
                            path_already_handled = true;
                    }
                },
                *current);

            if (!path_already_handled) {
                auto path_build = std::make_unique<BoundPathBuildOp>();
                path_build->path_variable = *pp.variable;
                path_build->path_column_index = nextColumnIndex();

                // Collect all element variables in the path (use bound variable names)
                path_build->element_variables = path_element_vars;
                path_build->child = std::move(*current);

                ctx_.symbols[path_build->path_variable] = makeColumnInfo(path_build->path_variable, BoundType::Path());
                current = std::move(path_build);
            }
        }
    }

    // Bind WHERE predicate
    if (match.where_pred && current && !skip_where) {
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
        // Populate return_columns for output_schema
        ColumnInfo out_info;
        out_info.name = proj_item.alias;
        out_info.type = proj_item.result_type;
        out_info.column_index = static_cast<uint32_t>(i);
        ctx_.return_columns.push_back(std::move(out_info));
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
                                                      std::optional<BoundLogicalOperator> /*extra_filter_child*/) {
    auto bound_pred = bindExpression(pred);
    if (!bound_pred)
        return std::nullopt;

    auto filter = std::make_unique<BoundFilterOp>();
    filter->predicate = std::move(*bound_pred);
    filter->child = std::move(child);
    return filter;
}

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
            info.source_label = existing->second.source_label;
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
        auto bound_skip = bindExpression(*wc.skip);
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

    // LIMIT
    if (wc.limit) {
        auto bound_limit = bindExpression(*wc.limit);
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
            info.source_label = it->second.source_label;
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

std::optional<BoundLogicalOperator> Binder::bindCreate(const cypher::CreateClause& create,
                                                       std::optional<BoundLogicalOperator> child) {
    std::optional<BoundLogicalOperator> current;

    for (size_t pi = 0; pi < create.patterns.size(); ++pi) {
        const auto& pp = create.patterns[pi];
        const auto& element = pp.element;

        // Create start node
        std::string start_var;
        uint32_t start_col;
        std::optional<LabelId> start_label;
        std::vector<uint16_t> start_prop_ids;
        if (!bindNodePattern(element.node, start_var, start_col, start_label, start_prop_ids))
            return std::nullopt;

        // Mark as CREATE variable so property resolution uses source_label
        auto* sym = ctx_.lookup(start_var);
        if (sym)
            ctx_.symbols[start_var].is_create_variable = true;

        auto create_node = std::make_unique<BoundCreateNodeOp>();
        create_node->variable = start_var;
        create_node->label_id = start_label.value_or(catalog_.getAnonLabelId());

        // Bind inline properties as expressions
        if (element.node.properties && create_node->label_id != INVALID_LABEL_ID) {
            const auto* ldef = catalog_.lookupLabel(create_node->label_id);
            if (ldef) {
                for (const auto& [prop_name, prop_expr] : element.node.properties->entries) {
                    auto* pd = catalog_.lookupProperty(create_node->label_id, prop_name);
                    if (pd) {
                        auto bound_val = bindExpression(prop_expr);
                        if (bound_val)
                            create_node->properties.emplace_back(pd->id, std::move(*bound_val));
                    } else {
                        auto bound_val = bindExpression(prop_expr);
                        if (bound_val)
                            create_node->pending_props.emplace_back(prop_name, std::move(*bound_val));
                    }
                }
            }
        }

        // Chain: first pattern connects to preceding clause (MATCH/WITH),
        // subsequent patterns connect to the previous pattern's output.
        if (pi == 0 && child) {
            create_node->child = std::move(*child);
        } else if (pi > 0 && current) {
            create_node->child = std::move(*current);
        }
        BoundLogicalOperator node_op = std::move(create_node);

        // Create edges in chain
        current = std::move(node_op);
        for (const auto& [rel_pat, node_pat] : element.chain) {
            if (!current)
                break;

            // Bind relationship (CREATE context: allow unknown edge types)
            std::string edge_var;
            uint32_t edge_col;
            std::vector<EdgeLabelId> edge_label_ids;
            std::vector<uint16_t> edge_prop_ids;
            if (!bindRelationshipPattern(rel_pat, edge_var, edge_col, edge_label_ids, edge_prop_ids,
                                         /*for_create=*/true))
                return std::nullopt;

            // Bind target node
            std::string dst_var;
            uint32_t dst_col;
            std::optional<LabelId> dst_label;
            std::vector<uint16_t> dst_prop_ids;
            if (!bindNodePattern(node_pat, dst_var, dst_col, dst_label, dst_prop_ids))
                return std::nullopt;

            // Create edge
            auto create_edge = std::make_unique<BoundCreateEdgeOp>();
            create_edge->variable = edge_var;
            create_edge->label_id = edge_label_ids.empty() ? std::nullopt : std::make_optional(edge_label_ids[0]);
            create_edge->label_name = (!rel_pat.rel_types.empty() && edge_label_ids.empty())
                                          ? std::make_optional(rel_pat.rel_types[0])
                                          : std::nullopt;
            create_edge->src_variable = start_var;
            create_edge->dst_variable = dst_var;
            if (rel_pat.properties) {
                for (const auto& [prop_name, prop_expr] : rel_pat.properties->entries) {
                    auto bound_val = bindExpression(prop_expr);
                    if (!bound_val)
                        continue;
                    if (create_edge->label_id.has_value()) {
                        auto* pd = catalog_.lookupEdgeLabelProperty(*create_edge->label_id, prop_name);
                        if (pd) {
                            create_edge->properties.emplace_back(pd->id, std::move(*bound_val));
                            continue;
                        }
                    }
                    create_edge->pending_props.emplace_back(prop_name, std::move(*bound_val));
                }
            }

            if (dst_var == start_var) {
                // Self-loop: reuse the existing start node, don't create a duplicate.
                create_edge->child = std::move(*current);
            } else {
                // Create target node
                auto create_dst = std::make_unique<BoundCreateNodeOp>();
                create_dst->variable = dst_var;
                create_dst->label_id = dst_label.value_or(catalog_.getAnonLabelId());
                if (node_pat.properties && create_dst->label_id != INVALID_LABEL_ID) {
                    for (const auto& [prop_name, prop_expr] : node_pat.properties->entries) {
                        auto* pd = catalog_.lookupProperty(create_dst->label_id, prop_name);
                        if (pd) {
                            auto bound_val = bindExpression(prop_expr);
                            if (bound_val)
                                create_dst->properties.emplace_back(pd->id, std::move(*bound_val));
                        } else {
                            auto bound_val = bindExpression(prop_expr);
                            if (bound_val)
                                create_dst->pending_props.emplace_back(prop_name, std::move(*bound_val));
                        }
                    }
                }
                create_dst->child = std::move(*current);
                BoundLogicalOperator dst_node_op = std::move(create_dst);
                create_edge->child = std::move(dst_node_op);
            }
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
                    if (func_registry_.exists(ptr->name)) {
                        // Function exists but no overload matches these argument types
                        error("Invalid argument type for function '" + ptr->name + "': got " + sig);
                    } else {
                        error("Function not found: " + sig);
                    }
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
                            error("Label '" + label_name + "' has no property '" + ptr->property + "'");
                            return std::nullopt;
                        }
                        auto prop_ref = std::make_unique<BoundPropertyRef>();
                        auto saved_label_id = lc->label_id;
                        // Extract variable name from label cast object before move
                        std::optional<std::string> saved_col_name;
                        if (std::holds_alternative<BoundColumnRef>(lc->object)) {
                            saved_col_name = std::get<BoundColumnRef>(lc->object).name;
                        }
                        prop_ref->object = std::move(*obj);
                        BoundPropertyRef::ResolvedProperty rp;
                        rp.label_id = saved_label_id;
                        rp.prop_id = pd->id;
                        rp.type = propertyTypeToBoundType(pd->type);
                        prop_ref->candidates.push_back(rp);
                        prop_ref->result_type = rp.type;

                        // Add projection requirement
                        if (saved_col_name) {
                            ctx_.addPropertyRequirement(*saved_col_name, saved_label_id, pd->id);
                        }

                        return BoundExpression(std::move(prop_ref));
                    }

                    // Weak type: check if object is a ColumnRef
                    if (std::holds_alternative<BoundColumnRef>(*obj)) {
                        auto& col_ref = std::get<BoundColumnRef>(*obj);

                        // For CREATE variables, avoid picking up wrong candidates from
                        // unrelated labels. Use source_label if set, otherwise dynamic.
                        auto* col_info = ctx_.lookup(col_ref.name);
                        if (col_info && col_info->is_create_variable) {
                            if (col_info->source_label) {
                                auto* pd = catalog_.lookupProperty(*col_info->source_label, ptr->property);
                                if (pd) {
                                    auto prop_ref = std::make_unique<BoundPropertyRef>();
                                    prop_ref->object = std::move(*obj);
                                    BoundPropertyRef::ResolvedProperty rp;
                                    rp.label_id = *col_info->source_label;
                                    rp.prop_id = pd->id;
                                    rp.type = propertyTypeToBoundType(pd->type);
                                    prop_ref->candidates.push_back(rp);
                                    prop_ref->result_type = rp.type;
                                    ctx_.addPropertyRequirement(col_ref.name, rp.label_id, rp.prop_id);
                                    return BoundExpression(std::move(prop_ref));
                                }
                            }
                            // No source_label or property not yet registered → dynamic
                            auto dyn_ref = std::make_unique<BoundDynamicPropertyRef>();
                            dyn_ref->object = std::move(*obj);
                            dyn_ref->property = ptr->property;
                            dyn_ref->result_type = BoundType::Any();
                            return BoundExpression(std::move(dyn_ref));
                        }

                        // Non-CREATE: scan ALL labels in the catalog
                        LabelIdSet all_labels;
                        for (const auto& [lid, ldef] : catalog_.allLabels()) {
                            all_labels.insert(lid);
                        }

                        auto candidates = catalog_.lookupPropertyAcrossLabels(all_labels, ptr->property);
                        if (candidates.empty()) {
                            // Fallback: runtime property lookup by name
                            auto dyn_ref = std::make_unique<BoundDynamicPropertyRef>();
                            dyn_ref->object = std::move(*obj);
                            dyn_ref->property = ptr->property;
                            dyn_ref->result_type = BoundType::Any();
                            return BoundExpression(std::move(dyn_ref));
                        }

                        auto saved_var_name = col_ref.name;
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
                            ctx_.addPropertyRequirement(saved_var_name, lid, pd->id);
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
                    error("Label '" + ptr->label + "' not found");
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
            } else if constexpr (std::is_same_v<Elem, cypher::AllExpr> || std::is_same_v<Elem, cypher::AnyExpr> ||
                                 std::is_same_v<Elem, cypher::NoneExpr> || std::is_same_v<Elem, cypher::SingleExpr>) {
                auto list_expr = bindExpression(ptr->list_expr);
                if (!list_expr)
                    return std::nullopt;

                const BoundType& list_type = getBoundExprType(*list_expr);
                BoundType elem_type = (list_type.kind == BoundTypeKind::LIST && list_type.element_type)
                                          ? BoundType::clone(*list_type.element_type)
                                          : BoundType::Any();

                // Temporarily register loop variable in BindContext
                uint32_t loop_col = nextColumnIndex();
                ctx_.symbols[ptr->variable] = makeColumnInfo(ptr->variable, BoundType::clone(elem_type));

                std::optional<BoundExpression> where_pred;
                if (ptr->where_pred) {
                    where_pred = bindExpression(*ptr->where_pred);
                    if (!where_pred)
                        return std::nullopt;
                }

                // Remove loop variable from BindContext (scope ends)
                ctx_.symbols.erase(ptr->variable);

                BoundExpression result;
                BoundType bool_type = BoundType::Bool();
                if constexpr (std::is_same_v<Elem, cypher::AllExpr>) {
                    auto e = std::make_unique<BoundAllExpr>();
                    e->variable = ptr->variable;
                    e->loop_column_index = loop_col;
                    e->list_expr = std::move(*list_expr);
                    e->where_pred = std::move(where_pred);
                    e->result_type = std::move(bool_type);
                    result = BoundExpression(std::move(e));
                } else if constexpr (std::is_same_v<Elem, cypher::AnyExpr>) {
                    auto e = std::make_unique<BoundAnyExpr>();
                    e->variable = ptr->variable;
                    e->loop_column_index = loop_col;
                    e->list_expr = std::move(*list_expr);
                    e->where_pred = std::move(where_pred);
                    e->result_type = std::move(bool_type);
                    result = BoundExpression(std::move(e));
                } else if constexpr (std::is_same_v<Elem, cypher::NoneExpr>) {
                    auto e = std::make_unique<BoundNoneExpr>();
                    e->variable = ptr->variable;
                    e->loop_column_index = loop_col;
                    e->list_expr = std::move(*list_expr);
                    e->where_pred = std::move(where_pred);
                    e->result_type = std::move(bool_type);
                    result = BoundExpression(std::move(e));
                } else {
                    auto e = std::make_unique<BoundSingleExpr>();
                    e->variable = ptr->variable;
                    e->loop_column_index = loop_col;
                    e->list_expr = std::move(*list_expr);
                    e->where_pred = std::move(where_pred);
                    e->result_type = std::move(bool_type);
                    result = BoundExpression(std::move(e));
                }
                return result;
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                auto bc = std::make_unique<BoundCase>();

                if (ptr->subject) {
                    auto bound_subject = bindExpression(*ptr->subject);
                    if (!bound_subject)
                        return std::nullopt;
                    bc->subject = std::move(*bound_subject);
                }

                for (const auto& [when_expr, then_expr] : ptr->when_thens) {
                    auto bound_when = bindExpression(when_expr);
                    auto bound_then = bindExpression(then_expr);
                    if (!bound_when || !bound_then)
                        return std::nullopt;
                    bc->when_thens.emplace_back(std::move(*bound_when), std::move(*bound_then));
                }

                if (ptr->else_expr) {
                    auto bound_else = bindExpression(*ptr->else_expr);
                    if (!bound_else)
                        return std::nullopt;
                    bc->else_expr = std::move(*bound_else);
                }

                bc->result_type = BoundType::Any();
                return BoundExpression(std::move(bc));
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

    case cypher::BinaryOperator::STARTS_WITH:
    case cypher::BinaryOperator::ENDS_WITH:
    case cypher::BinaryOperator::CONTAINS:
        if ((left_type.kind == BoundTypeKind::STRING || left_type.kind == BoundTypeKind::ANY ||
             left_type.kind == BoundTypeKind::NULL_TYPE) &&
            (right_type.kind == BoundTypeKind::STRING || right_type.kind == BoundTypeKind::ANY ||
             right_type.kind == BoundTypeKind::NULL_TYPE))
            return BoundType::Bool();
        error_msg =
            "String operations require string operands: got " + left_type.toString() + " and " + right_type.toString();
        return BoundType::Any();

    case cypher::BinaryOperator::IN:
        if (right_type.kind == BoundTypeKind::LIST || right_type.kind == BoundTypeKind::ANY)
            return BoundType::Bool();
        error_msg = "IN requires a list on the right side, got " + right_type.toString();
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
                             std::optional<LabelId>& label_id, std::vector<uint16_t>& /*default_prop_ids*/,
                             bool skip_register) {
    var_name = node.variable.value_or("");
    if (var_name.empty())
        var_name = "__anon_" + std::to_string(nextAnonId());

    if (skip_register) {
        auto* col = ctx_.lookup(var_name);
        col_idx = col ? col->column_index : nextColumnIndex();
    } else {
        col_idx = nextColumnIndex();
    }

    label_id = std::nullopt;
    if (!node.labels.empty()) {
        if (node.labels.size() > 1) {
            error("Multi-label CREATE is not supported");
            return false;
        }
        LabelId lid = catalog_.labelNameToId(node.labels[0]);
        if (lid == INVALID_LABEL_ID) {
            error("Label '" + node.labels[0] + "' does not exist");
            return false;
        }
        label_id = lid;
    }

    if (!var_name.empty() && !skip_register) {
        ctx_.symbols[var_name] = makeColumnInfo(var_name, BoundType::Vertex(), label_id);
    }

    return true;
}

bool Binder::bindRelationshipPattern(const cypher::RelationshipPattern& rel, std::string& var_name, uint32_t& col_idx,
                                     std::vector<EdgeLabelId>& edge_label_ids,
                                     std::vector<uint16_t>& /*default_prop_ids*/, bool for_create) {
    var_name = rel.variable.value_or("");
    if (var_name.empty())
        var_name = "__anon_edge_" + std::to_string(nextAnonId());
    col_idx = nextColumnIndex();

    edge_label_ids.clear();
    if (!rel.rel_types.empty()) {
        edge_label_ids = catalog_.resolveEdgeLabelIds(rel.rel_types);
        if (edge_label_ids.empty() && !for_create) {
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
    info.column_index = ctx_.next_column_index > 0 ? ctx_.next_column_index - 1 : 0;
    info.source_label = source_label;
    info.source_prop_id = source_prop_id;
    info.strong_typed = strong_typed;
    return info;
}

// ==================== Projection Pushdown ====================

void Binder::addAllPropertiesForVariable(const std::string& var_name) {
    auto* col = ctx_.lookup(var_name);
    if (!col)
        return;

    // RETURN n means the whole vertex — we need all properties from all labels
    // because the vertex may have multiple labels at runtime
    for (const auto& [lid, ldef] : catalog_.allLabels()) {
        for (const auto& pd : ldef.properties) {
            ctx_.addPropertyRequirement(var_name, lid, pd.id);
        }
    }
}

void Binder::collectLabelPropIds(const std::string& var_name, std::unordered_map<LabelId, std::vector<uint16_t>>& out) {
    for (const auto& req : ctx_.property_requirements) {
        if (req.variable_name != var_name)
            continue;
        if (req.label_id) {
            auto& ids = out[*req.label_id];
            for (uint16_t pid : req.prop_ids) {
                if (std::find(ids.begin(), ids.end(), pid) == ids.end())
                    ids.push_back(pid);
            }
        } else {
            // Weak-typed: apply to all labels that have these properties
            for (const auto& [lid, ldef] : catalog_.allLabels()) {
                auto& ids = out[lid];
                for (uint16_t pid : req.prop_ids) {
                    bool has_prop = false;
                    for (const auto& pd : ldef.properties) {
                        if (pd.id == pid) {
                            has_prop = true;
                            break;
                        }
                    }
                    if (has_prop && std::find(ids.begin(), ids.end(), pid) == ids.end())
                        ids.push_back(pid);
                }
            }
        }
    }
}

void Binder::applyProjectionPushdown(BoundLogicalOperator& op) {
    std::visit(
        [this](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, BoundSingletonOp>) {
                // No children, no properties to collect
            } else if constexpr (std::is_same_v<T, BoundLabelScanOp>) {
                collectLabelPropIds(val.variable, val.label_prop_ids);
            } else if constexpr (std::is_same_v<T, BoundScanOp>) {
                collectLabelPropIds(val.variable, val.label_prop_ids);
            } else {
                using Elem = typename T::element_type;
                auto& v = *val;
                if constexpr (std::is_same_v<Elem, BoundExpandOp>) {
                    collectLabelPropIds(v.dst_variable, v.dst_label_prop_ids);
                    for (const auto& req : ctx_.property_requirements) {
                        if (req.variable_name == v.edge_variable && req.label_id) {
                            for (uint16_t pid : req.prop_ids) {
                                if (std::find(v.edge_prop_ids.begin(), v.edge_prop_ids.end(), pid) ==
                                    v.edge_prop_ids.end())
                                    v.edge_prop_ids.push_back(pid);
                            }
                        }
                    }
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundVarLenExpandOp>) {
                    collectLabelPropIds(v.dst_variable, v.dst_label_prop_ids);
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundFilterOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundProjectOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundSortOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundAggregateOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundSkipOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundLimitOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundDistinctOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundSetOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundRemoveOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundPathBuildOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundBinaryJoinOp>) {
                    applyProjectionPushdown(v.left);
                    applyProjectionPushdown(v.right);
                }
                // BoundCreateNodeOp, BoundCreateEdgeOp: no child traversal needed
            }
        },
        op);
}

} // namespace binder
} // namespace eugraph
