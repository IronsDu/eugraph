#include "query/planner/binder.hpp"

#include "query/function/batch_ops.hpp"
#include "query/planner/bound_expression/bound_literal.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"

#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_left_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_apply_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

#include <algorithm>

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

bool containsParameter(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::Parameter>) {
                return true;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return containsParameter(ptr->left) || containsParameter(ptr->right);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return containsParameter(ptr->operand);
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                for (const auto& arg : ptr->args)
                    if (containsParameter(arg))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                return containsParameter(ptr->object);
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& e : ptr->elements)
                    if (containsParameter(e))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries)
                    if (containsParameter(v))
                        return true;
                return false;
            }
            return false;
        },
        expr);
}

bool propertiesContainParameter(const std::optional<cypher::PropertiesMap>& props) {
    if (!props)
        return false;
    for (const auto& [name, expr] : props->entries)
        if (containsParameter(expr))
            return true;
    return false;
}

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

        // Anonymous MATCH () after a preceding clause: treat as a fresh
        // all-nodes scan; the cross product is created in binder.cpp
        // (CREATE3 [3]).
        if (correlated && !element.node.variable) {
            correlated = false;
        }

        if (correlated) {
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
        } else if (!start_labels.empty() || !element.node.labels.empty()) {
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

        // Parameter as predicate is not allowed in MATCH
        if (propertiesContainParameter(element.node.properties)) {
            error("InvalidParameterUse: MATCH does not support parameter as node predicate");
            return std::nullopt;
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

            // Parameter as predicate is not allowed in MATCH
            if (propertiesContainParameter(rel_pat.properties)) {
                error("InvalidParameterUse: MATCH does not support parameter as relationship predicate");
                return std::nullopt;
            }
            if (propertiesContainParameter(node_pat.properties)) {
                error("InvalidParameterUse: MATCH does not support parameter as node predicate");
                return std::nullopt;
            }

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
                    error("SyntaxError: InvalidRelationshipPattern: Variable-length minimum hop count must be >= 0 "
                          "(got " +
                          std::to_string(min_hops) + ")");
                    return std::nullopt;
                }
                // Variable-length paths with max_hops < min_hops (empty
                // interval, e.g. *2..1) are valid — they match zero rows.
                // The VarLenExpandPhysicalOp DFS loop naturally handles
                // this by never executing a single iteration.

                // Bind edge types. Non-existent edge types produce an empty
                // label list, causing the expand to match zero rows — correct
                // behaviour for both OPTIONAL MATCH (null row via LeftJoin) and
                // regular MATCH (no matches when the edge type doesn't exist).
                std::vector<EdgeLabelId> edge_label_ids;
                if (!rel_pat.rel_types.empty()) {
                    edge_label_ids = catalog_.resolveEdgeLabelIds(rel_pat.rel_types);
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
                if (auto* dinfo = ctx_.lookup(dst_var))
                    varlen->dst_slot_id = dinfo->slot_id;
                varlen->dst_label_ids = dst_labels;
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
                    if (auto* einfo = ctx_.lookup(*edge_var))
                        varlen->edge_slot_id = einfo->slot_id;
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
            if (auto* sinfo = ctx_.lookup(start_var))
                expand->src_slot_id = sinfo->slot_id;
            expand->edge_variable = edge_var;
            expand->edge_column_index = edge_col;
            if (auto* einfo = ctx_.lookup(edge_var))
                expand->edge_slot_id = einfo->slot_id;

            // For RIGHT_TO_LEFT patterns, put dst+edge before src so the
            // path order matches the query's written direction (§path-order).
            if (rel_pat.direction == cypher::RelationshipDirection::RIGHT_TO_LEFT) {
                path_element_vars.clear();
                path_element_vars.push_back(dst_var);
                path_element_vars.push_back(edge_var);
                path_element_vars.push_back(start_var);
            } else {
                path_element_vars.push_back(edge_var);
                path_element_vars.push_back(dst_var);
            }
            expand->dst_variable = dst_var;
            expand->dst_column_index = dst_col;
            if (auto* dinfo = ctx_.lookup(dst_var))
                expand->dst_slot_id = dinfo->slot_id;
            expand->dst_label_ids = dst_labels;
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

    // Determine correlation before saving context.
    const auto& first_node = exists.patterns[0].element.node;
    std::string start_var_name;
    if (first_node.variable.has_value()) {
        start_var_name = *first_node.variable;
    } else {
        start_var_name = "__anon_exists_start_" + std::to_string(nextAnonId());
    }

    bool is_correlated = false;
    ColumnInfo saved_outer_info;
    SlotId outer_slot = INVALID_SLOT_ID;
    if (auto* outer_col = ctx_.lookup(start_var_name)) {
        is_correlated = true;
        outer_slot = outer_col->slot_id;
        saved_outer_info = *outer_col;
    }

    // Save outer binding context
    auto saved_ctx = ctx_.save();

    // Validate: all named variables in the pattern must exist in the outer
    // scope. Bare patterns like (n)-[r]->(a) where `a` or `r` are new should
    // raise UndefinedVariable (Pattern1 [10]).
    // Also, a bare node without a chain (e.g. (n)) is not a valid pattern
    // predicate — raise InvalidArgumentType (Pattern1 [11]).
    //
    // This strictness applies only to *bare* pattern predicates — explicit
    // `EXISTS { ... }` subqueries may introduce fresh local variables.
    if (exists.is_bare_predicate) {
        for (const auto& pp : exists.patterns) {
            if (pp.element.chain.empty()) {
                error("InvalidArgumentType: bare node pattern is not a valid predicate");
                return std::nullopt;
            }
            auto checkVar = [&](const std::optional<std::string>& var) {
                if (var.has_value() && !saved_ctx.symbols.count(*var)) {
                    error("UndefinedVariable: variable '" + *var + "' not defined in pattern predicate");
                    return false;
                }
                return true;
            };
            if (!checkVar(pp.element.node.variable))
                return std::nullopt;
            for (const auto& [rel_pat, node_pat] : pp.element.chain) {
                if (!checkVar(rel_pat.variable))
                    return std::nullopt;
                if (!checkVar(node_pat.variable))
                    return std::nullopt;
            }
        }
    }

    // Reset to independent sub-scope
    ctx_.beginSubScope();

    // Register the start variable in sub-scope.
    uint32_t sub_idx;
    SlotId sub_slot = INVALID_SLOT_ID;
    if (is_correlated) {
        ColumnInfo sub_info = saved_outer_info;
        sub_info.column_index = nextColumnIndex();
        // Reuse the outer variable's slot_id rather than allocating a new one:
        // allocateNamedSlot would overwrite ctx_.all_symbols[start_var_name]
        // and break the left side's makeSlotLayout, which still needs the
        // original slot to find the column in the left TupleSlotLayout.
        sub_idx = sub_info.column_index;
        sub_slot = sub_info.slot_id;
        ctx_.symbols[start_var_name] = sub_info;
        correlation.emplace_back(outer_slot, sub_slot);
    } else {
        sub_idx = nextColumnIndex();
        ctx_.symbols[start_var_name] = makeColumnInfo(start_var_name, BoundType::Vertex());
    }

    // Track saved (reference) columns for correlated chain nodes.
    // These hold the original correlated value so we can filter after
    // the Expand overwrites the destination column.
    struct SavedCorr {
        std::string orig_var;    // user-facing chain var (e.g. "m")
        std::string sub_dst_var; // fresh name in sub-plan (e.g. "__exists_dst_1")
        std::string saved_var;   // fresh name for saved outer value (e.g. "__exists_saved_1")
        uint32_t saved_col;
        SlotId saved_slot;
        SlotId sub_dst_slot;
    };
    std::vector<SavedCorr> saved_chain_corrs;

    // For chain nodes that reference outer-scope variables (e.g. `(n)-[]->(m)`
    // where both `n` and `m` are from the outer scope), allocate fresh names
    // and slots inside the sub-plan. Reusing the outer var's name would make
    // Expand's output schema contain the name twice (passthrough + dst) and
    // makeSlotLayout would collapse them onto the same slot, breaking the
    // post-Expand equality filter.
    uint32_t chain_counter = 0;
    for (const auto& pp : exists.patterns) {
        for (auto& [rel_pat, node_pat] : pp.element.chain) {
            if (!node_pat.variable.has_value())
                continue;
            const auto& chain_var = *node_pat.variable;
            if (chain_var == start_var_name)
                continue;
            auto it = saved_ctx.symbols.find(chain_var);
            if (it == saved_ctx.symbols.end())
                continue;

            const auto& chain_outer = it->second;
            ++chain_counter;

            // Fresh name for the destination that Expand writes to. This name
            // is registered in ctx_.symbols (so bindNodePattern reuses the
            // column/slot and the post-Expand filter can reference it) but is
            // NOT added to correlation or to the source's output_schema —
            // otherwise Expand's output_schema would contain the name twice
            // (passthrough + dst) and makeSlotLayout would collapse them onto
            // the same slot, breaking the filter.
            std::string sub_dst_var = "__exists_dst_" + std::to_string(chain_counter);
            uint32_t sub_dst_col = nextColumnIndex();
            SlotId sub_dst_slot = allocateNamedSlot(sub_dst_var);
            ColumnInfo sub_dst_info;
            sub_dst_info.name = sub_dst_var;
            sub_dst_info.type = chain_outer.type;
            sub_dst_info.column_index = sub_dst_col;
            sub_dst_info.slot_id = sub_dst_slot;
            sub_dst_info.source_labels = chain_outer.source_labels;
            ctx_.symbols[sub_dst_var] = sub_dst_info;

            // Fresh name + slot for the saved outer value (preserved across
            // Expand). This IS correlated so the SemiJoin injects the outer
            // value into the source's output.
            std::string saved_var = "__exists_saved_" + std::to_string(chain_counter);
            uint32_t saved_col = nextColumnIndex();
            SlotId saved_slot = allocateNamedSlot(saved_var);
            ColumnInfo saved_info;
            saved_info.name = saved_var;
            saved_info.type = chain_outer.type;
            saved_info.column_index = saved_col;
            saved_info.slot_id = saved_slot;
            saved_info.source_labels = chain_outer.source_labels;
            ctx_.symbols[saved_var] = saved_info;
            correlation.emplace_back(chain_outer.slot_id, saved_slot);

            saved_chain_corrs.push_back({chain_var, sub_dst_var, saved_var, saved_col, saved_slot, sub_dst_slot});
        }
    }

    // Create the source leaf operator
    BoundLogicalOperator source_op;
    if (is_correlated || !correlation.empty()) {
        BoundCorrelatedSourceOp source;
        // Collect correlated variables in the same order as correlation pairs.
        // Each (outer_slot, sub_slot) pair maps to a symbol in ctx_.symbols;
        // find it by slot_id and emit its name + type.
        //
        // The runtime Value injected by SemiJoin is the topology-stage form
        // (VertexRef / EdgeKey) — what the left side actually stores in its
        // VertexRef / EdgeKey columns. The declared variable type (VERTEX /
        // EDGE) is the semantic stage, but emitting that here would make PE
        // create VERTEX-typed passthrough columns and then setValue(VertexRef)
        // on a VERTEX column silently drops the value. Emit the topology
        // counterpart so the source's output_types matches the actual value
        // kind; PE can still upgrade to VERTEX via Construct specs.
        for (const auto& [outer_slot, sub_slot] : correlation) {
            for (const auto& [name, info] : ctx_.symbols) {
                if (sub_slot == info.slot_id) {
                    source.variables.push_back(name);
                    BoundType topo = BoundType::clone(info.type);
                    BoundTypeKind tk = topologyCounterpart(info.type.kind);
                    if (tk != info.type.kind) {
                        topo.kind = tk;
                    }
                    source.types.push_back(std::move(topo));
                    source.column_indices.push_back(info.column_index);
                    break;
                }
            }
        }
        source_op = std::move(source);
    } else {
        BoundScanOp scan;
        scan.variable = start_var_name;
        scan.column_index = sub_idx;
        source_op = std::move(scan);
    }

    // Deep-clone the EXISTS patterns into a synthetic MatchClause.
    // (Expression contains unique_ptr and cannot be trivially copied.)
    // For correlated chain nodes, the dst variable is rewritten to the fresh
    // sub-plan name (sub_dst_var) so Expand writes to a dedicated slot.
    std::unordered_map<std::string, std::string> chain_var_rewrite;
    for (const auto& sc : saved_chain_corrs)
        chain_var_rewrite[sc.orig_var] = sc.sub_dst_var;

    cypher::MatchClause synthetic_match;
    synthetic_match.patterns.reserve(exists.patterns.size());
    for (const auto& pp : exists.patterns) {
        cypher::PatternPart cloned_pp;
        cloned_pp.variable = pp.variable;
        // Use the resolved start variable name (auto-generated for anonymous nodes).
        cloned_pp.element.node.variable =
            pp.element.node.variable.has_value() ? pp.element.node.variable : std::make_optional(start_var_name);
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
            // Rewrite chain var to the fresh sub-plan name when correlated,
            // so Expand writes to a slot distinct from the saved outer value.
            if (node_pat.variable.has_value()) {
                auto rw = chain_var_rewrite.find(*node_pat.variable);
                if (rw != chain_var_rewrite.end())
                    cloned_node.variable = rw->second;
                else
                    cloned_node.variable = node_pat.variable;
            }
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

    // Bind the sub-plan with the source as parent
    auto sub_plan = bindMatch(synthetic_match, std::move(source_op), false);
    if (!sub_plan)
        return std::nullopt;

    // For correlated chain nodes: filter the expanded dst against the saved
    // outer value (Pattern1 [12]-[18]). The dst was rewritten to sub_dst_var,
    // which has its own slot; the saved outer value lives in saved_var.
    //
    // The reference type is VERTEX_REF (not VERTEX) on purpose: the source
    // emits topology-stage VertexRef values and Expand writes a topology-stage
    // VertexRef into the dst column. Declaring VERTEX would make PE allocate
    // Construct slots for both columns and rewriteColumnIndices would retarget
    // the slot_id to those Construct columns, but the runtime EQ on the
    // resulting VertexValues misbehaves (likely because the evaluator's
    // genericEqBatch path has subtle issues with VERTEX columns). Comparing
    // the VertexRef ids directly avoids the Construct altogether and matches
    // the topology-stage semantics of the data.
    for (const auto& sc : saved_chain_corrs) {
        auto dst_it = ctx_.symbols.find(sc.sub_dst_var);
        if (dst_it == ctx_.symbols.end())
            continue;
        uint32_t dst_col = dst_it->second.column_index;
        SlotId dst_slot = dst_it->second.slot_id;

        BoundExpression left_ref =
            BoundExpression(BoundColumnRef(dst_col, BoundType::VertexRef(), sc.sub_dst_var, dst_slot));
        BoundExpression right_ref =
            BoundExpression(BoundColumnRef(sc.saved_col, BoundType::VertexRef(), sc.saved_var, sc.saved_slot));
        auto eq = std::make_unique<BoundBinaryOp>();
        eq->op = cypher::BinaryOperator::EQ;
        eq->left = std::move(left_ref);
        eq->right = std::move(right_ref);
        eq->result_type = BoundType::Bool();
        eq->batch_fn = function::resolveBinaryBatchFn(eq->op, BoundTypeKind::VERTEX_REF, BoundTypeKind::VERTEX_REF);

        BoundFilterOp filter;
        filter.predicate = BoundExpression(std::move(eq));
        filter.child = std::move(*sub_plan);
        sub_plan = std::make_unique<BoundFilterOp>(std::move(filter));
    }

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

// ==================== Pattern Comprehension Binding ====================

namespace {

/// Resolve a Variable/property/Literal from a PatternComprehension projection
/// AST into a BoundExpression. Since the sub-plan scope was already restored
/// by bindExistsSubPlan, we use ctx_.all_symbols (permanent) for slot_id
/// lookup and a map of sub-plan variable names → BoundType collected from
/// the pattern definition.
BoundExpression bindSimpleProjectionExpr(Binder& binder, const cypher::Expression& proj_ast,
                                         const cypher::PatternComprehension& pc) {
    // Build a map of sub-plan variable → BoundType from the pattern definition.
    // These are the names/names that bindExistsSubPlan registers in the sub-scope.
    std::unordered_map<std::string, BoundType> sub_var_types;
    if (pc.patterns.empty())
        return BoundExpression(BoundLiteral(int64_t(1)));

    const auto& pp = pc.patterns[0];
    // Path variable from PatternComprehension (e.g. `p` in `[p=(n)-->() | p]`)
    if (pc.variable.has_value())
        sub_var_types[*pc.variable] = BoundType::Path();
    // Also check PatternPart variable for shared AST patterns
    if (pp.variable.has_value())
        sub_var_types[*pp.variable] = BoundType::Path();
    // Chain node and edge variables (non-correlated ones keep their names)
    if (!pp.element.chain.empty()) {
        for (const auto& [rel_pat, node_pat] : pp.element.chain) {
            if (rel_pat.variable.has_value())
                sub_var_types[*rel_pat.variable] = BoundType::Edge();
            if (node_pat.variable.has_value())
                sub_var_types[*node_pat.variable] = BoundType::Vertex();
        }
    } else if (pp.element.node.variable.has_value()) {
        sub_var_types[*pp.element.node.variable] = BoundType::Vertex();
    }

    // Variable
    if (auto* var = std::get_if<std::unique_ptr<cypher::Variable>>(&proj_ast)) {
        const auto& name = (*var)->name;
        auto type_it = sub_var_types.find(name);
        auto slot_it = binder.ctx().all_symbols.find(name);
        if (type_it != sub_var_types.end() && slot_it != binder.ctx().all_symbols.end()) {
            BoundType topo = BoundType::clone(type_it->second);
            BoundTypeKind tk = topologyCounterpart(topo.kind);
            if (tk != topo.kind)
                topo.kind = tk;
            return BoundExpression(BoundColumnRef(0, topo, name, slot_it->second));
        }
    }
    // Literal
    else if (auto* lit = std::get_if<std::unique_ptr<cypher::Literal>>(&proj_ast)) {
        const auto& v = (*lit)->value;
        if (std::holds_alternative<bool>(v))
            return BoundExpression(BoundLiteral(std::get<bool>(v)));
        if (std::holds_alternative<int64_t>(v))
            return BoundExpression(BoundLiteral(std::get<int64_t>(v)));
        if (std::holds_alternative<double>(v))
            return BoundExpression(BoundLiteral(std::get<double>(v)));
        if (std::holds_alternative<std::string>(v))
            return BoundExpression(BoundLiteral(std::get<std::string>(v)));
    }
    // PropertyAccess: e.g. b.name → look up b's slot, create BoundPropertyRef
    // with catalog-resolved candidates so the evaluator can find the property
    // and applyProjectionPushdown wires Expand to materialize the value.
    else if (auto* pr = std::get_if<std::unique_ptr<cypher::PropertyAccess>>(&proj_ast)) {
        auto obj_var = std::get_if<std::unique_ptr<cypher::Variable>>(&(*pr)->object);
        if (obj_var) {
            const auto& name = (*obj_var)->name;
            auto type_it = sub_var_types.find(name);
            auto slot_it = binder.ctx().all_symbols.find(name);
            if (type_it != sub_var_types.end() && slot_it != binder.ctx().all_symbols.end()) {
                const auto& pname = (*pr)->property;
                auto prop_ref = std::make_unique<BoundPropertyRef>();
                prop_ref->property_name = pname;
                prop_ref->object = BoundExpression(BoundColumnRef(0, type_it->second, name, slot_it->second));

                if (type_it->second.kind == BoundTypeKind::VERTEX) {
                    // Resolve candidates across all labels so the evaluator
                    // knows which (label_id, prop_id) pairs to read. Also
                    // push property requirements so applyProjectionPushdown
                    // wires the Expand to materialize the destination.
                    LabelIdSet all_labels;
                    for (const auto& [lid, ldef] : binder.catalog().allLabels())
                        all_labels.insert(lid);
                    auto candidates = binder.catalog().lookupPropertyAcrossLabels(all_labels, pname);
                    BoundType merged = BoundType::Null();
                    for (auto& [lid, pd] : candidates) {
                        BoundPropertyRef::ResolvedProperty rp;
                        rp.label_id = lid;
                        rp.prop_id = pd->id;
                        rp.type = binder.propertyTypeToBoundType(pd->type);
                        merged = BoundType::merge(merged, rp.type);
                        prop_ref->candidates.push_back(rp);
                        binder.ctx().addPropertyRequirement(name, lid, pd->id);
                    }
                    prop_ref->result_type = candidates.empty() ? BoundType::Any() : merged;
                } else if (type_it->second.kind == BoundTypeKind::EDGE) {
                    // Edge property: resolve candidates across all edge labels.
                    // EdgeVariableRef is rare here; we set candidates so the
                    // evaluator's edge-property path can find the property.
                    BoundType merged = BoundType::Null();
                    for (const auto& [elid, eldef] : binder.catalog().allEdgeLabels()) {
                        for (const auto& pd : eldef.properties) {
                            if (pd.name == pname) {
                                BoundPropertyRef::ResolvedProperty rp;
                                rp.label_id = static_cast<LabelId>(elid);
                                rp.prop_id = pd.id;
                                rp.type = binder.propertyTypeToBoundType(pd.type);
                                merged = BoundType::merge(merged, rp.type);
                                prop_ref->candidates.push_back(rp);
                                binder.ctx().addPropertyRequirement(name, static_cast<LabelId>(elid), pd.id);
                            }
                        }
                    }
                    prop_ref->result_type = prop_ref->candidates.empty() ? BoundType::Any() : merged;
                } else {
                    prop_ref->result_type = BoundType::Any();
                }
                return BoundExpression(std::move(prop_ref));
            }
        }
    }

    // Fallback: literal 1 (works for size([... | 1]) and similar)
    return BoundExpression(BoundLiteral(int64_t(1)));
}

} // namespace

// ==================== Pattern Comprehension Binding ====================

std::optional<BoundLogicalOperator>
Binder::bindPatternComprehension(const cypher::PatternComprehension& pc, BoundLogicalOperator child,
                                 std::vector<std::pair<SlotId, SlotId>>& correlation, SlotId& out_slot,
                                 std::string& out_name, BoundType& out_element_type) {
    // Reuse bindExistsSubPlan by synthesising an ExistsExpr wrapper. Pattern
    // binding, sub-scope management, and correlated-source wiring are
    // identical; we then append Project + Aggregate(collect) to collapse the
    // sub-plan into a single-row single-list result.
    if (pc.patterns.empty()) {
        error("PatternComprehension: empty pattern");
        return std::nullopt;
    }

    cypher::ExistsExpr synthetic;
    synthetic.is_bare_predicate = false;
    synthetic.patterns.reserve(pc.patterns.size());
    for (const auto& pp : pc.patterns) {
        cypher::PatternPart cloned_pp;
        // Propagate the path variable from pc.variable (PatternComprehension
        // level, e.g. `[p=(n)-->() | p]` stores `p` here), falling back to
        // pp.variable (PatternPart level, for shared AST patterns).
        cloned_pp.variable = pc.variable.has_value() ? pc.variable : pp.variable;
        cloned_pp.element.node.variable = pp.element.node.variable;
        cloned_pp.element.node.labels = pp.element.node.labels;
        if (pp.element.node.properties) {
            cloned_pp.element.node.properties = cypher::PropertiesMap{};
            for (const auto& [k, v] : pp.element.node.properties->entries)
                cloned_pp.element.node.properties->entries.emplace_back(k, cloneExpression(v));
        }
        for (const auto& [rel_pat, node_pat] : pp.element.chain) {
            cypher::RelationshipPattern cloned_rel;
            cloned_rel.variable = rel_pat.variable;
            cloned_rel.rel_types = rel_pat.rel_types;
            cloned_rel.direction = rel_pat.direction;
            if (rel_pat.range) {
                cloned_rel.range =
                    std::make_pair(cloneExpression(rel_pat.range->first), cloneExpression(rel_pat.range->second));
            }
            if (rel_pat.properties) {
                cloned_rel.properties = cypher::PropertiesMap{};
                for (const auto& [k, v] : rel_pat.properties->entries)
                    cloned_rel.properties->entries.emplace_back(k, cloneExpression(v));
            }
            cypher::NodePattern cloned_node;
            cloned_node.variable = node_pat.variable;
            cloned_node.labels = node_pat.labels;
            if (node_pat.properties) {
                cloned_node.properties = cypher::PropertiesMap{};
                for (const auto& [k, v] : node_pat.properties->entries)
                    cloned_node.properties->entries.emplace_back(k, cloneExpression(v));
            }
            cloned_pp.element.chain.emplace_back(std::move(cloned_rel), std::move(cloned_node));
        }
        synthetic.patterns.push_back(std::move(cloned_pp));
    }
    if (pc.where_pred)
        synthetic.where_pred = cloneExpression(*pc.where_pred);

    std::vector<std::pair<uint32_t, uint32_t>> exists_corr;
    auto sub_plan = bindExistsSubPlan(synthetic, exists_corr);
    if (!sub_plan)
        return std::nullopt;
    for (const auto& [l, r] : exists_corr)
        correlation.emplace_back(static_cast<SlotId>(l), static_cast<SlotId>(r));

    // Bind projection expression. The sub-plan scope has been restored by
    // bindExistsSubPlan, so variables registered during spine construction
    // are no longer in ctx_.symbols. However, ctx_.all_symbols retains
    // their globally-unique slot_ids. We resolve the slot_id and type by
    // collecting from the pattern definition (path var, chain vars, etc.),
    // then build a BoundColumnRef. The column_index (currently 0) will be
    // resolved later by ExpressionCompiler using the slot_layout.
    BoundExpression proj_expr;
    if (pc.projection) {
        proj_expr = bindSimpleProjectionExpr(*this, *pc.projection, pc);
    } else {
        proj_expr = BoundExpression(BoundLiteral(int64_t(1)));
    }
    out_element_type = getBoundExprType(proj_expr);

    // Append Project → single column __pc_proj
    auto proj_op = std::make_unique<BoundProjectOp>();
    BoundProjectOp::ProjectItem item;
    item.expr = std::move(proj_expr);
    item.alias = "__pc_proj";
    item.result_type = out_element_type;
    item.output_slot = INVALID_SLOT_ID;
    proj_op->items.push_back(std::move(item));
    proj_op->child = std::move(*sub_plan);
    sub_plan = std::move(proj_op);

    // Append Aggregate → single collect() over __pc_proj
    auto agg_op = std::make_unique<BoundAggregateOp>();
    const function::FunctionDef* collect_fn = func_registry_.lookup("collect", {out_element_type});
    if (!collect_fn) {
        // Fall back to ANY-typed collect if a typed overload is missing.
        collect_fn = func_registry_.lookup("collect", {BoundType::Any()});
    }
    if (!collect_fn) {
        error("PatternComprehension: collect() not registered");
        return std::nullopt;
    }
    BoundAggregateOp::AggregateItem agg_item;
    agg_item.func_def = collect_fn;
    agg_item.function_name = "collect";
    agg_item.arguments.push_back(BoundExpression(BoundColumnRef(0, out_element_type, "__pc_proj", INVALID_SLOT_ID)));
    agg_item.alias = "__pc_list";
    agg_item.result_type = BoundType::List(out_element_type);
    agg_item.is_internal = false; // must be visible — PCApply reads this column directly
    agg_item.keeps_nulls = true;  // collect() must retain nulls for pattern comprehension
    agg_op->aggregates.push_back(std::move(agg_item));
    agg_op->output_names.push_back("__pc_list");
    agg_op->child = std::move(*sub_plan);
    sub_plan = std::move(agg_op);

    // Allocate output slot and wrap in the Apply op. The unique name ties
    // together: the all_symbols entry, the Apply op's Output struct (used by
    // column_rewrite to register the slot for downstream name resolution), and
    // the placeholder's output_name (used by column_rewrite to rewrite the
    // placeholder to a BoundColumnRef).
    out_name = "__pc_" + std::to_string(nextAnonId());
    out_slot = allocateNamedSlot(out_name);

    auto apply = std::make_unique<BoundPatternComprehensionApplyOp>();
    apply->left = std::move(child);
    apply->right = std::move(*sub_plan);
    apply->correlation = std::move(correlation);
    BoundPatternComprehensionApplyOp::Output out;
    out.slot_id = out_slot;
    out.name = out_name;
    out.element_type = out_element_type;
    apply->outputs.push_back(std::move(out));
    return BoundLogicalOperator(std::move(apply));
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
        // Similar to bindExistsSubPlan: create a CorrelatedSource sub-plan.
        // Note: LeftJoin correlation uses column indices (not SlotIds) because
        // a WITH projection between the outer scan and OPTIONAL MATCH forwards
        // graph variables under their PEPlan object_slot, breaking slot-based
        // lookup while column_index remains valid (Project preserves order).
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
