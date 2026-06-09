#include "query/planner/binder.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace binder {

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

bool Binder::registerPatternVariable(const std::string& name, BoundType type, bool as_path) {
    auto* existing = ctx_.lookup(name);
    if (!existing) {
        nextColumnIndex(); // allocate the next column index
        ctx_.symbols[name] = makeColumnInfo(name, std::move(type));
        return true;
    }

    if (isCompatibleForPatternUse(existing->type, type))
        return true;

    const char* error_code = as_path ? "VariableAlreadyBound" : "VariableTypeConflict";
    error(std::string(error_code) + ": variable '" + name + "' already defined as " + existing->type.toString() +
          " but used as " + type.toString());
    return false;
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
    // Bind the first query
    BoundLogicalPlan first_plan;
    if (!bindSingleQuery(query.first, first_plan))
        return false;

    if (query.unions.empty()) {
        result.plan = std::move(first_plan);
        return true;
    }

    // All UNION clauses in a query must be consistently UNION or UNION ALL
    bool first_all = query.unions.front().first.all;
    for (const auto& [ucl, sq] : query.unions) {
        if (ucl.all != first_all) {
            error("InvalidClauseComposition: Cannot mix UNION and UNION ALL in the same query");
            return false;
        }
    }

    // Bind each UNION sub-query and chain them
    BoundLogicalOperator current = std::move(first_plan.root);
    std::vector<ColumnInfo> first_schema = std::move(first_plan.output_schema);

    for (const auto& [ucl, sq] : query.unions) {
        // Create a fresh BindContext for each UNION sub-query
        BindContext union_ctx;
        BindContext saved_ctx = std::move(ctx_);
        ctx_ = std::move(union_ctx);

        BoundLogicalPlan sub_plan;
        bool ok = bindSingleQuery(sq, sub_plan);

        // Restore the original context (UNION sub-queries don't share scope)
        ctx_ = std::move(saved_ctx);

        if (!ok)
            return false;

        if (sub_plan.output_schema.size() != first_schema.size()) {
            error(
                "DifferentColumnsInUnion: All sub queries in a UNION must have the same number of columns (expected " +
                std::to_string(first_schema.size()) + ", got " + std::to_string(sub_plan.output_schema.size()) + ")");
            return false;
        }
        for (size_t ci = 0; ci < first_schema.size(); ++ci) {
            if (sub_plan.output_schema[ci].name != first_schema[ci].name) {
                error(
                    "DifferentColumnsInUnion: All sub queries in a UNION must have the same column names (expected '" +
                    first_schema[ci].name + "', got '" + sub_plan.output_schema[ci].name + "')");
                return false;
            }
        }

        auto union_op = std::make_unique<BoundUnionOp>();
        union_op->all = ucl.all;
        union_op->left = std::move(current);
        union_op->right = std::move(sub_plan.root);
        current = BoundLogicalOperator(std::move(union_op));
    }

    result.plan.root = std::move(current);
    result.plan.output_schema = std::move(first_schema);
    return true;
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

                    // OPTIONAL MATCH: create a LeftJoin
                    if (ptr->optional) {
                        if (!current) {
                            current = BoundSingletonOp{};
                        }
                        auto result = bindOptionalMatch(*ptr, std::move(*current));
                        first_clause = false;
                        return result;
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
                } else if constexpr (std::is_same_v<Elem, cypher::DeleteClause>) {
                    if (!current) {
                        error("DELETE without preceding clause");
                        return std::nullopt;
                    }
                    return bindDelete(*ptr, std::move(*current));
                } else if constexpr (std::is_same_v<Elem, cypher::UnwindClause>) {
                    return bindUnwind(*ptr, std::move(current));
                } else if constexpr (std::is_same_v<Elem, cypher::MergeClause>) {
                    return bindMerge(*ptr, std::move(current));
                } else {
                    error("Clause type not yet supported in binder");
                    return std::nullopt;
                }
            },
            clause);

        if (errors_.empty()) {
            current = std::move(op);
        } else {
            break; // stop processing clauses after first binding error
        }
    }

    if (errors_.empty() && current) {
        plan.root = std::move(*current);

        // Apply projection pushdown: fill label_prop_ids on scan/expand operators
        applyProjectionPushdown(plan.root);

        if (!ctx_.return_columns.empty()) {
            plan.output_schema = std::move(ctx_.return_columns);
        } else {
            // No RETURN clause: wrap root in an empty BoundProjectOp → 0 output columns
            auto proj = std::make_unique<BoundProjectOp>();
            proj->child = std::move(plan.root);
            plan.root = std::move(proj);
        }
        return true;
    }
    return false;
}

std::optional<BoundLogicalOperator> Binder::bindWhere(const cypher::Expression& pred, BoundLogicalOperator child,
                                                      std::optional<BoundLogicalOperator> /*extra_filter_child*/) {
    // Check for EXISTS subqueries in the top-level AND chain.
    std::vector<std::pair<const cypher::ExistsExpr*, bool>> exists_list;
    collectExistsFromAnd(pred, exists_list);

    if (!exists_list.empty()) {
        // Wrap each EXISTS as a SemiJoin operator.
        auto current = std::move(child);
        for (auto& [ex, anti] : exists_list) {
            auto sj = bindExistsAsSemiJoin(*ex, std::move(current), anti);
            if (!sj)
                return std::nullopt;
            current = std::move(*sj);
        }

        // Bind remaining (non-EXISTS) WHERE conditions as a Filter above the SemiJoin chain.
        auto remaining = removeExistsFromWhere(pred);
        if (remaining) {
            auto bound_rem = bindExpression(*remaining);
            if (!bound_rem)
                return std::nullopt;
            auto filter = std::make_unique<BoundFilterOp>();
            filter->predicate = std::move(*bound_rem);
            filter->child = std::move(current);
            return filter;
        }
        return current;
    }

    // No EXISTS: original behavior.
    auto bound_pred = bindExpression(pred);
    if (!bound_pred)
        return std::nullopt;

    // Collect property requirements from WHERE predicate so that
    // scan operators fetch the properties needed for filter evaluation.
    // Without this, filters after cross-joins fail when no RETURN
    // clause triggers projection pushdown (e.g. MATCH+CREATE).
    {
        std::function<void(const BoundExpression&)> collectVars = [&](const BoundExpression& expr) {
            std::visit(
                [&](const auto& e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, BoundColumnRef>) {
                        addAllPropertiesForVariable(e.name);
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundBinaryOp>>) {
                        collectVars(e->left);
                        collectVars(e->right);
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundUnaryOp>>) {
                        collectVars(e->operand);
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundPropertyRef>>) {
                        collectVars(e->object);
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<BoundLabelCast>>) {
                        collectVars(e->object);
                    }
                },
                expr);
        };
        collectVars(*bound_pred);
    }

    auto filter = std::make_unique<BoundFilterOp>();
    filter->predicate = std::move(*bound_pred);
    filter->child = std::move(child);
    return filter;
}

} // namespace binder
} // namespace eugraph
