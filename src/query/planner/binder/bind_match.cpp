#include "query/planner/binder.hpp"

#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
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
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                auto c = std::make_unique<cypher::ParenExpr>();
                c->inner = cloneExpression(ptr->inner);
                return c;
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

        // For patterns after the first, join with previous via cross product
        std::optional<BoundLogicalOperator> previous = std::move(current);
        current = std::nullopt;

        // Only the first pattern in a correlated MATCH reuses the parent.
        bool correlated = (pi == 0) && parent.has_value();

        // Bind start node
        std::string start_var;
        uint32_t start_col;
        std::vector<LabelId> start_labels;
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
            if (!bindNodePattern(element.node, start_var, start_col, start_labels, start_prop_ids, true))
                return std::nullopt;
            start_col = col->column_index;
        } else {
            if (!bindNodePattern(element.node, start_var, start_col, start_labels, start_prop_ids))
                return std::nullopt;
        }

        // Collect bound variable names for path building
        std::vector<std::string> path_element_vars;
        path_element_vars.push_back(start_var);

        // Create scan operator (or reuse parent for correlated MATCH)
        if (correlated) {
            current = std::move(*parent);
        } else if (!start_labels.empty()) {
            BoundLabelScanOp scan;
            scan.variable = start_var;
            scan.column_index = start_col;
            scan.label_ids = start_labels;
            scan.label_names = element.node.labels;
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
                std::vector<LabelId> dst_labels;
                std::vector<uint16_t> dst_prop_ids;
                if (!bindNodePattern(node_pat, dst_var, dst_col, dst_labels, dst_prop_ids))
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
                    auto* path_existing = ctx_.lookup(*pp.variable);
                    if (path_existing && !isCompatibleForPatternUse(path_existing->type, BoundType::Path())) {
                        error("VariableAlreadyBound: variable '" + *pp.variable + "' already defined as " +
                              path_existing->type.toString() + " but used as path");
                        return std::nullopt;
                    }
                    varlen->path_variable = *pp.variable;
                    varlen->path_column_index = nextColumnIndex();
                    varlen->path_handled_by_varlen = true;
                    ctx_.symbols[varlen->path_variable] = makeColumnInfo(varlen->path_variable, BoundType::Path());
                }

                // P2: handle named edge variable → LIST<EDGE>
                if (edge_var) {
                    auto* edge_existing = ctx_.lookup(*edge_var);
                    if (edge_existing &&
                        !isCompatibleForPatternUse(edge_existing->type, BoundType::List(BoundType::Edge()))) {
                        error("VariableTypeConflict: variable '" + *edge_var + "' already defined as " +
                              edge_existing->type.toString() + " but used as variable-length edge");
                        return std::nullopt;
                    }
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
            std::vector<LabelId> dst_labels;
            std::vector<uint16_t> dst_prop_ids;
            if (!bindNodePattern(node_pat, dst_var, dst_col, dst_labels, dst_prop_ids))
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
                auto* path_existing = ctx_.lookup(*pp.variable);
                if (path_existing && !isCompatibleForPatternUse(path_existing->type, BoundType::Path())) {
                    error("VariableAlreadyBound: variable '" + *pp.variable + "' already defined as " +
                          path_existing->type.toString() + " but used as path");
                    return std::nullopt;
                }
                auto path_build = std::make_unique<BoundPathBuildOp>();
                path_build->path_variable = *pp.variable;
                path_build->path_column_index = nextColumnIndex();

                path_build->element_variables = path_element_vars;
                path_build->child = std::move(*current);

                ctx_.symbols[path_build->path_variable] = makeColumnInfo(path_build->path_variable, BoundType::Path());
                current = std::move(path_build);
            }
        }

        // For patterns after the first, join with previous via cross product
        if (pi > 0 && previous && current) {
            auto join = std::make_unique<BoundBinaryJoinOp>();
            join->join_type = JoinType::Cross;
            join->left = std::move(*previous);
            join->right = std::move(*current);
            current = std::move(join);
        }
    }

    // Bind WHERE predicate
    if (match.where_pred && current && !skip_where) {
        auto where_op = bindWhere(*match.where_pred, std::move(*current));
        current = std::move(where_op);
    }

    return current;
}

// ==================== EXISTS Subquery Binding ====================

void Binder::collectExistsFromAnd(const cypher::Expression& expr,
                                  std::vector<std::pair<const cypher::ExistsExpr*, bool>>& out) {
    std::visit(
        [&](const auto& ptr) {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                if (ptr->op == cypher::BinaryOperator::AND) {
                    collectExistsFromAnd(ptr->left, out);
                    collectExistsFromAnd(ptr->right, out);
                }
            } else if constexpr (std::is_same_v<Elem, cypher::ExistsExpr>) {
                out.emplace_back(ptr.get(), false);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                if (ptr->op == cypher::UnaryOperator::NOT) {
                    // Check if NOT wraps an EXISTS
                    bool found = false;
                    std::visit(
                        [&](const auto& inner) {
                            if constexpr (std::is_same_v<typename std::decay_t<decltype(inner)>::element_type,
                                                         cypher::ExistsExpr>) {
                                out.emplace_back(inner.get(), true);
                                found = true;
                            }
                        },
                        ptr->operand);
                    if (!found) {
                        // NOT wraps something else — also recurse into it for AND chains
                        // inside the NOT (e.g., NOT (a AND b AND EXISTS ...))
                        collectExistsFromAnd(ptr->operand, out);
                    }
                }
            }
        },
        expr);
}

std::optional<cypher::Expression> Binder::removeExistsFromWhere(const cypher::Expression& expr) {
    return std::visit(
        [&](const auto& ptr) -> std::optional<cypher::Expression> {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                if (ptr->op == cypher::BinaryOperator::AND) {
                    auto left = removeExistsFromWhere(ptr->left);
                    auto right = removeExistsFromWhere(ptr->right);
                    if (left && right)
                        return cypher::makeBinaryOp(cypher::BinaryOperator::AND, std::move(*left), std::move(*right));
                    if (left)
                        return left;
                    return right;
                }
                return cloneExpression(expr);
            } else if constexpr (std::is_same_v<Elem, cypher::ExistsExpr>) {
                return std::nullopt;
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                if (ptr->op == cypher::UnaryOperator::NOT) {
                    auto inner = removeExistsFromWhere(ptr->operand);
                    if (!inner)
                        return std::nullopt; // NOT(EXISTS) → fully removed
                    return cypher::makeUnaryOp(cypher::UnaryOperator::NOT, std::move(*inner));
                }
                return cloneExpression(expr);
            } else {
                return cloneExpression(expr);
            }
        },
        expr);
}

std::optional<BoundLogicalOperator> Binder::bindExistsSubPlan(const cypher::ExistsExpr& exists,
                                                              std::vector<std::pair<uint32_t, uint32_t>>& correlation) {
    if (exists.patterns.empty()) {
        error("EXISTS subquery has no patterns");
        return std::nullopt;
    }

    // Determine correlation before saving context: first pattern node variable
    // must exist in outer scope.
    const auto& first_node = exists.patterns[0].element.node;
    if (!first_node.variable) {
        error("EXISTS subquery pattern must have a named start variable");
        return std::nullopt;
    }

    auto* outer_col = ctx_.lookup(*first_node.variable);
    if (!outer_col) {
        error("EXISTS subquery start variable '" + *first_node.variable +
              "' must reference an outer variable (non-correlated EXISTS not yet supported)");
        return std::nullopt;
    }

    uint32_t outer_idx = outer_col->column_index;
    ColumnInfo saved_outer_info = *outer_col; // copy before resetting

    // Save outer binding context
    auto saved_ctx = ctx_.save();

    // Reset to independent sub-scope
    ctx_.beginSubScope();

    // Register correlated variable in sub-scope (preserving metadata from outer scope)
    ColumnInfo sub_info = saved_outer_info;
    sub_info.column_index = nextColumnIndex();
    uint32_t sub_idx = sub_info.column_index;
    ctx_.symbols[*first_node.variable] = sub_info;
    correlation.emplace_back(outer_idx, sub_idx);

    // Also check for additional correlated variables in the WHERE predicate
    // (any outer variable referenced in the WHERE must also be correlated)
    // For Phase 1, only the start node is handled as correlated.

    // Create the correlated source leaf operator
    BoundCorrelatedSourceOp source;
    source.variables.push_back(*first_node.variable);
    source.types.push_back(BoundType::Vertex());
    source.column_indices.push_back(sub_idx);

    // Deep-clone the EXISTS patterns into a synthetic MatchClause.
    // (Expression contains unique_ptr and cannot be trivially copied.)
    cypher::MatchClause synthetic_match;
    synthetic_match.patterns.reserve(exists.patterns.size());
    for (const auto& pp : exists.patterns) {
        cypher::PatternPart cloned_pp;
        cloned_pp.variable = pp.variable;
        cloned_pp.element.node.variable = pp.element.node.variable;
        cloned_pp.element.node.labels = pp.element.node.labels;
        if (pp.element.node.properties) {
            cloned_pp.element.node.properties = cypher::PropertiesMap{};
            for (const auto& [name, expr] : pp.element.node.properties->entries) {
                cloned_pp.element.node.properties->entries.emplace_back(name, cloneExpression(expr));
            }
        }
        for (const auto& [rel_pat, node_pat] : pp.element.chain) {
            cypher::RelationshipPattern cloned_rel;
            cloned_rel.variable = rel_pat.variable;
            cloned_rel.rel_types = rel_pat.rel_types;
            cloned_rel.direction = rel_pat.direction;
            if (rel_pat.range) {
                cloned_rel.range = {cloneExpression(rel_pat.range->first), cloneExpression(rel_pat.range->second)};
            }
            if (rel_pat.properties) {
                cloned_rel.properties = cypher::PropertiesMap{};
                for (const auto& [name, expr] : rel_pat.properties->entries) {
                    cloned_rel.properties->entries.emplace_back(name, cloneExpression(expr));
                }
            }
            cypher::NodePattern cloned_node;
            cloned_node.variable = node_pat.variable;
            cloned_node.labels = node_pat.labels;
            if (node_pat.properties) {
                cloned_node.properties = cypher::PropertiesMap{};
                for (const auto& [name, expr] : node_pat.properties->entries) {
                    cloned_node.properties->entries.emplace_back(name, cloneExpression(expr));
                }
            }
            cloned_pp.element.chain.emplace_back(std::move(cloned_rel), std::move(cloned_node));
        }
        synthetic_match.patterns.push_back(std::move(cloned_pp));
    }
    if (exists.where_pred) {
        synthetic_match.where_pred = cloneExpression(*exists.where_pred);
    }

    // Bind the sub-plan with the correlated source as parent
    BoundLogicalOperator parent_op = std::move(source);
    auto sub_plan = bindMatch(synthetic_match, std::move(parent_op), false);
    if (!sub_plan)
        return std::nullopt;

    // Restore outer binding context
    ctx_.restore(saved_ctx);

    return sub_plan;
}

std::optional<BoundLogicalOperator> Binder::bindExistsAsSemiJoin(const cypher::ExistsExpr& exists,
                                                                 BoundLogicalOperator child, bool anti) {
    std::vector<std::pair<uint32_t, uint32_t>> correlation;
    auto sub_plan = bindExistsSubPlan(exists, correlation);
    if (!sub_plan)
        return std::nullopt;

    auto semi_join = std::make_unique<BoundSemiJoinOp>();
    semi_join->left = std::move(child);
    semi_join->right = std::move(*sub_plan);
    semi_join->correlation = std::move(correlation);
    semi_join->anti = anti;
    return semi_join;
}

// ==================== OPTIONAL MATCH Binding ====================

std::optional<BoundLogicalOperator> Binder::bindOptionalMatch(const cypher::MatchClause& match,
                                                              BoundLogicalOperator current) {
    if (match.patterns.empty()) {
        error("OPTIONAL MATCH clause has no patterns");
        return std::nullopt;
    }

    const auto& first_node = match.patterns[0].element.node;

    // Determine if the OPTIONAL MATCH is correlated (reuses a variable from current scope)
    bool correlated = false;
    std::string corr_var_name;
    const ColumnInfo* outer_col = nullptr;

    if (first_node.variable) {
        outer_col = ctx_.lookup(*first_node.variable);
        if (outer_col) {
            correlated = true;
            corr_var_name = *first_node.variable;
        }
    }

    if (correlated) {
        // ── Correlated OPTIONAL MATCH ──
        // Similar to bindExistsSubPlan: create a CorrelatedSource sub-plan
        uint32_t outer_idx = outer_col->column_index;
        ColumnInfo saved_outer_info = *outer_col;

        auto saved_ctx = ctx_.save();
        ctx_.beginSubScope();

        // Register correlated variable in sub-scope
        ColumnInfo sub_info = saved_outer_info;
        sub_info.column_index = nextColumnIndex();
        uint32_t sub_idx = sub_info.column_index;
        ctx_.symbols[corr_var_name] = sub_info;

        std::vector<std::pair<uint32_t, uint32_t>> correlation;
        correlation.emplace_back(outer_idx, sub_idx);

        // Create CorrelatedSource leaf
        BoundCorrelatedSourceOp source;
        source.variables.push_back(corr_var_name);
        source.types.push_back(sub_info.type);
        source.column_indices.push_back(sub_idx);

        // Bind the MATCH pattern in the sub-scope
        BoundLogicalOperator parent_op = std::move(source);
        auto sub_plan = bindMatch(match, std::move(parent_op), /*skip_where=*/false);
        if (!sub_plan)
            return std::nullopt;

        // Collect new variables from sub-scope before restoring
        std::vector<std::pair<std::string, ColumnInfo>> new_vars;
        for (const auto& [name, info] : ctx_.symbols) {
            if (name != corr_var_name) {
                new_vars.emplace_back(name, info);
            }
        }

        ctx_.restore(saved_ctx);

        // Adjust column indices: sub-scope indices start from 0, but the
        // LeftJoin physical output is [left_cols... | right_cols...], so
        // right-side variables need an offset equal to the left column count.
        uint32_t col_offset = ctx_.next_column_index;
        for (auto& [name, info] : new_vars) {
            info.column_index += col_offset;
            ctx_.symbols[name] = std::move(info);
        }
        ctx_.next_column_index = col_offset + static_cast<uint32_t>(new_vars.size());

        auto left_join = std::make_unique<BoundLeftJoinOp>();
        left_join->left = std::move(current);
        left_join->right = std::move(*sub_plan);
        left_join->correlation = std::move(correlation);
        return left_join;
    } else {
        // ── Independent (non-correlated) OPTIONAL MATCH ──
        // Bind the pattern as an independent sub-plan, then left-join
        auto saved_ctx = ctx_.save();
        ctx_.beginSubScope();

        auto sub_plan = bindMatch(match, std::nullopt, /*skip_where=*/false);
        if (!sub_plan)
            return std::nullopt;

        // Collect new variables from sub-scope
        std::vector<std::pair<std::string, ColumnInfo>> new_vars;
        for (const auto& [name, info] : ctx_.symbols) {
            new_vars.emplace_back(name, info);
        }

        ctx_.restore(saved_ctx);

        // Adjust column indices (same as correlated case)
        uint32_t col_offset = ctx_.next_column_index;
        for (auto& [name, info] : new_vars) {
            info.column_index += col_offset;
            ctx_.symbols[name] = std::move(info);
        }
        ctx_.next_column_index = col_offset + static_cast<uint32_t>(new_vars.size());

        auto left_join = std::make_unique<BoundLeftJoinOp>();
        left_join->left = std::move(current);
        left_join->right = std::move(*sub_plan);
        return left_join;
    }
}

} // namespace binder
} // namespace eugraph
