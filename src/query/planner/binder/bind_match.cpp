#include "query/planner/binder.hpp"

#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace eugraph {
namespace binder {

namespace {

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

cypher::Expression cloneExpression(const cypher::Expression& expr);

} // anonymous namespace

// Forward declare for the anonymous namespace helper
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

// ==================== MATCH Binding ====================

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

} // namespace binder
} // namespace eugraph
