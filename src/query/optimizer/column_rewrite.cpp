#include "query/optimizer/column_rewrite.hpp"

#include "query/planner/bound_expression/bound_property_ref.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"
#include "query/planner/logical_plan/operator/bound_sort_op.hpp"

namespace eugraph {
namespace optimizer {

namespace {

/// Find the offset of (lid, pid) in the extraction info's ordered prop list.
/// Returns SIZE_MAX if not found.
size_t findPropOffset(const PropertyExtractionInfo& info, LabelId lid, uint16_t pid) {
    for (size_t i = 0; i < info.prop_order.size(); ++i) {
        if (info.prop_order[i].first == lid && info.prop_order[i].second == pid)
            return i + 1; // +1 to skip the source column
    }
    return SIZE_MAX;
}

/// Rewrite a BoundExpression, replacing BoundPropertyRef with BoundColumnRef.
bool rewriteExpr(binder::BoundExpression& expr, const std::unordered_map<std::string, PropertyExtractionInfo>& info) {
    return std::visit(
        [&](auto& val) -> bool {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                if (!val)
                    return false;
                // Get variable name from the object expression.
                std::string var;
                if (auto* vref = std::get_if<binder::BoundVariableRef>(&val->object))
                    var = vref->name;
                else if (auto* cref = std::get_if<binder::BoundColumnRef>(&val->object)) {
                    // ColumnRef → need to map column index to variable name.
                    // For now, only handle VariableRef.
                    return false;
                }
                if (var.empty())
                    return false;

                auto it = info.find(var);
                if (it == info.end())
                    return false;

                // Find the property in the extraction list.
                for (const auto& cand : val->candidates) {
                    size_t offset = findPropOffset(it->second, cand.label_id, cand.prop_id);
                    if (offset == SIZE_MAX)
                        continue;
                    // Replace this BoundPropertyRef with BoundColumnRef.
                    uint32_t new_col = it->second.base_col + static_cast<uint32_t>(offset);
                    expr = binder::BoundColumnRef{new_col, binder::BoundType::Any(), ""};
                    return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                if (!val)
                    return false;
                bool a = rewriteExpr(val->left, info);
                bool b = rewriteExpr(val->right, info);
                return a || b;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                if (!val)
                    return false;
                return rewriteExpr(val->operand, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (!val)
                    return false;
                bool changed = false;
                for (auto& arg : val->args)
                    changed |= rewriteExpr(arg, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                if (!val)
                    return false;
                bool changed = false;
                for (auto& e : val->elements)
                    changed |= rewriteExpr(e, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (!val)
                    return false;
                bool changed = false;
                if (val->subject)
                    changed |= rewriteExpr(*val->subject, info);
                for (auto& [w, t] : val->when_thens) {
                    changed |= rewriteExpr(w, info);
                    changed |= rewriteExpr(t, info);
                }
                if (val->else_expr)
                    changed |= rewriteExpr(*val->else_expr, info);
                return changed;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                if (!val)
                    return false;
                return rewriteExpr(val->object, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                // Dynamic property ref — need labels. Not a specific prop_id.
                return false;
            }
            return false;
        },
        expr);
}

/// Rewrite column indices in operators that have expressions.
void rewriteOp(binder::BoundLogicalOperator& op, const std::unordered_map<std::string, PropertyExtractionInfo>& info) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v)
                    rewriteExpr(v->predicate, info);
                if (v)
                    rewriteColumnIndices(v->child, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    for (auto& item : v->items)
                        rewriteExpr(item.expr, info);
                    rewriteColumnIndices(v->child, info);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v) {
                    for (auto& item : v->items)
                        rewriteExpr(item.expr, info);
                    rewriteColumnIndices(v->child, info);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    for (auto& k : v->group_keys)
                        rewriteExpr(k, info);
                    for (auto& agg : v->aggregates)
                        rewriteExpr(agg.argument, info);
                    rewriteColumnIndices(v->child, info);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v)
                    rewriteColumnIndices(v->child, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v)
                    rewriteColumnIndices(v->child, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v)
                    rewriteColumnIndices(v->child, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (v)
                    rewriteColumnIndices(v->child, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                if (v)
                    rewriteColumnIndices(v->child, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                if (v)
                    rewriteColumnIndices(v->child, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                if (v)
                    rewriteColumnIndices(v->child, info);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                if (v) {
                    rewriteColumnIndices(v->left, info);
                    rewriteColumnIndices(v->right, info);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                if (v) {
                    rewriteColumnIndices(v->left, info);
                    rewriteColumnIndices(v->right, info);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                if (v) {
                    rewriteColumnIndices(v->left, info);
                    rewriteColumnIndices(v->right, info);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (v) {
                    rewriteColumnIndices(v->left, info);
                    rewriteColumnIndices(v->right, info);
                }
            }
            // Leaves (Scan, LabelScan, Singleton, CorrelatedSource) and
            // write ops (Create*, Set, Remove, Delete, Merge) are left unmodified.
        },
        op);
}

/// Walk ChosenPlan tree to collect property extraction info.
void collectFromChosen(const ChosenPlan& chosen, std::unordered_map<std::string, PropertyExtractionInfo>& info,
                       uint32_t base_col) {
    if (chosen.tag == PhysicalOpTag::VertexPropertyExtract) {
        auto& ext = info[chosen.enrich_variable];
        ext.base_col = base_col;
        auto it = chosen.enrich_output.find(chosen.enrich_variable);
        if (it != chosen.enrich_output.end()) {
            for (const auto& [lid, pids] : it->second.need_props)
                for (uint16_t pid : pids)
                    ext.prop_order.emplace_back(lid, pid);
        }
    }
    for (const auto& c : chosen.children)
        if (c)
            collectFromChosen(*c, info, base_col);
}

} // namespace

std::unordered_map<std::string, PropertyExtractionInfo> collectExtractionInfo(const ChosenPlan& chosen,
                                                                              uint32_t base_col) {
    std::unordered_map<std::string, PropertyExtractionInfo> info;
    collectFromChosen(chosen, info, base_col);
    return info;
}

// Walk the materialized tree to update base_col for each variable.
void updateBaseCols(const binder::BoundLogicalOperator& op,
                    std::unordered_map<std::string, PropertyExtractionInfo>& info, uint32_t& col_count) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp> || std::is_same_v<T, binder::BoundLabelScanOp>) {
                auto it = info.find(v.variable);
                if (it != info.end())
                    it->second.base_col = col_count;
                col_count += 1;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    updateBaseCols(v->child, info, col_count);
                    auto it_r = info.find(v->edge_variable);
                    if (it_r != info.end())
                        it_r->second.base_col = col_count;
                    if (!v->edge_variable.empty())
                        col_count += 1;
                    auto it_dst = info.find(v->dst_variable);
                    if (it_dst != info.end())
                        it_dst->second.base_col = col_count;
                    if (!v->dst_variable.empty())
                        col_count += 1;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v)
                    updateBaseCols(v->child, info, col_count);
            }
        },
        op);
}

void updateExtractionBaseCols(const binder::BoundLogicalOperator& op,
                              std::unordered_map<std::string, PropertyExtractionInfo>& info) {
    uint32_t col_count = 0;
    updateBaseCols(op, info, col_count);
}

bool rewriteExpression(binder::BoundExpression& expr,
                       const std::unordered_map<std::string, PropertyExtractionInfo>& info) {
    return rewriteExpr(expr, info);
}

void rewriteColumnIndices(binder::BoundLogicalOperator& op,
                          const std::unordered_map<std::string, PropertyExtractionInfo>& info) {
    rewriteOp(op, info);
}

// Walk a BoundLogicalOperator tree to collect extraction info for the RBO path.
void collectFromPlan(const binder::BoundLogicalOperator& op,
                     std::unordered_map<std::string, PropertyExtractionInfo>& info, uint32_t base_col) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                if (!v.label_prop_ids.empty()) {
                    auto& ext = info[v.variable];
                    ext.base_col = base_col;
                    for (const auto& [lid, pids] : v.label_prop_ids)
                        for (uint16_t pid : pids)
                            ext.prop_order.emplace_back(lid, pid);
                }
                base_col += 1;
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                if (!v.label_prop_ids.empty()) {
                    auto& ext = info[v.variable];
                    ext.base_col = base_col;
                    for (const auto& [lid, pids] : v.label_prop_ids)
                        for (uint16_t pid : pids)
                            ext.prop_order.emplace_back(lid, pid);
                }
                base_col += 1;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v) {
                    uint32_t child_cols = base_col;
                    collectFromPlan(v->child, info, base_col);
                    uint32_t after_child = base_col;
                    if (!v->edge_variable.empty()) {
                        if (!v->edge_prop_ids.empty()) {
                            auto& ext = info[v->edge_variable];
                            ext.base_col = after_child;
                            for (uint16_t pid : v->edge_prop_ids)
                                ext.prop_order.emplace_back(EdgeLabelId{0}, pid);
                        }
                        after_child += 1;
                    }
                    if (!v->dst_variable.empty()) {
                        if (!v->dst_label_prop_ids.empty()) {
                            auto& ext = info[v->dst_variable];
                            ext.base_col = after_child;
                            for (const auto& [lid, pids] : v->dst_label_prop_ids)
                                for (uint16_t pid : pids)
                                    ext.prop_order.emplace_back(lid, pid);
                        }
                        after_child += 1;
                    }
                    base_col = after_child;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                if (v)
                    collectFromPlan(v->child, info, base_col);
            }
        },
        op);
}

std::unordered_map<std::string, PropertyExtractionInfo>
collectExtractionFromPlan(const binder::BoundLogicalOperator& root, uint32_t base_col) {
    std::unordered_map<std::string, PropertyExtractionInfo> info;
    collectFromPlan(root, info, base_col);
    return info;
}

void findWholeObj(const binder::BoundLogicalOperator& op, std::set<std::string>& result) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v) {
                    for (const auto& item : v->items) {
                        if (std::holds_alternative<binder::BoundColumnRef>(item.expr)) {
                            auto k = item.result_type.kind;
                            if (k == binder::BoundTypeKind::VERTEX || k == binder::BoundTypeKind::EDGE ||
                                k == binder::BoundTypeKind::PATH)
                                result.insert(item.alias);
                        }
                    }
                    findWholeObj(v->child, result);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v)
                    findWholeObj(v->child, result);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v)
                    findWholeObj(v->child, result);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (v)
                    findWholeObj(v->child, result);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v)
                    findWholeObj(v->child, result);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (v)
                    findWholeObj(v->child, result);
            }
        },
        op);
}

std::set<std::string> findWholeObjectVariables(const binder::BoundLogicalOperator& op) {
    std::set<std::string> result;
    findWholeObj(op, result);
    return result;
}

} // namespace optimizer
} // namespace eugraph
