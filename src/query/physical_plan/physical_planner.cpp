#include "query/physical_plan/physical_planner.hpp"
#include "query/physical_plan/operator/cross_product_physical_op.hpp"
#include "query/physical_plan/operator/singleton_physical_op.hpp"
#include "query/physical_plan/operator/varlen_expand_physical_op.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <stdexcept>

namespace eugraph {
namespace compute {

// Convert a Value (runtime literal) to PropertyValue for storage.
static PropertyValue valueToPropertyValue(const Value& v) {
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v);
    if (std::holds_alternative<int64_t>(v))
        return std::get<int64_t>(v);
    if (std::holds_alternative<double>(v))
        return std::get<double>(v);
    if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
    return PropertyValue{};
}

// ==================== Bound Plan Index Scan Optimization ====================

/// Extract AND-chain of BoundBinaryOp conditions from a BoundExpression.
static void collectBoundConditions(const binder::BoundExpression& pred,
                                   std::vector<const binder::BoundBinaryOp*>& conditions) {
    if (auto* bp = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&pred)) {
        auto& binop = *bp;
        if (binop->op == cypher::BinaryOperator::AND) {
            collectBoundConditions(binop->left, conditions);
            collectBoundConditions(binop->right, conditions);
        } else {
            conditions.push_back(binop.get());
        }
    }
}

struct BoundIndexableCondition {
    uint16_t prop_id;
    cypher::BinaryOperator op;
    PropertyValue value;
};

/// Try to extract a property=value condition from a BoundBinaryOp.
static std::optional<BoundIndexableCondition> tryExtractBoundCondition(const binder::BoundBinaryOp& binop) {
    if (!std::holds_alternative<std::unique_ptr<binder::BoundPropertyRef>>(binop.left))
        return std::nullopt;
    auto& prop_ref = std::get<std::unique_ptr<binder::BoundPropertyRef>>(binop.left);
    if (prop_ref->candidates.empty())
        return std::nullopt;

    if (!std::holds_alternative<binder::BoundLiteral>(binop.right))
        return std::nullopt;
    auto& lit = std::get<binder::BoundLiteral>(binop.right);

    auto pv = valueToPropertyValue(lit.value);
    if (std::holds_alternative<std::monostate>(pv))
        return std::nullopt;

    return BoundIndexableCondition{prop_ref->candidates[0].prop_id, binop.op, std::move(pv)};
}

std::optional<PlanOperatorResult>
PhysicalPlanner::tryBoundIndexScan(const binder::BoundLabelScanOp& scan_op,
                                   const std::vector<const binder::BoundBinaryOp*>& conditions, LabelId label_id,
                                   const LabelDef& label_def, IAsyncGraphDataStore& store, PlanContext& ctx) {
    std::unordered_map<uint16_t, BoundIndexableCondition> prop_conditions;
    for (auto* cond : conditions) {
        auto extracted = tryExtractBoundCondition(*cond);
        if (extracted.has_value())
            prop_conditions[extracted->prop_id] = std::move(*extracted);
    }

    if (prop_conditions.empty())
        return std::nullopt;

    for (const auto& idx : label_def.indexes) {
        if (idx.state != IndexState::PUBLIC)
            continue;

        size_t match_count = 0;
        bool last_is_range = false;
        std::vector<PropertyValue> eq_values;
        std::optional<PropertyValue> range_start;
        std::optional<PropertyValue> range_end;

        for (size_t i = 0; i < idx.prop_ids.size(); ++i) {
            uint16_t pid = idx.prop_ids[i];
            auto it = prop_conditions.find(pid);
            if (it == prop_conditions.end())
                break;

            auto& cond = it->second;
            if (i < idx.prop_ids.size() - 1) {
                if (cond.op != cypher::BinaryOperator::EQ)
                    break;
                eq_values.push_back(std::move(cond.value));
                match_count++;
            } else {
                if (cond.op == cypher::BinaryOperator::EQ) {
                    eq_values.push_back(std::move(cond.value));
                    match_count++;
                } else if (cond.op == cypher::BinaryOperator::GT || cond.op == cypher::BinaryOperator::GTE) {
                    range_start = std::move(cond.value);
                    last_is_range = true;
                    match_count++;
                } else if (cond.op == cypher::BinaryOperator::LT || cond.op == cypher::BinaryOperator::LTE) {
                    range_end = std::move(cond.value);
                    last_is_range = true;
                    match_count++;
                } else {
                    break;
                }
            }
        }

        if (match_count == 0)
            continue;

        using ScanMode = IndexScanPhysicalOp::ScanMode;
        std::unique_ptr<IndexScanPhysicalOp> result;

        Schema output_schema;
        std::vector<binder::BoundType> output_types;
        if (!scan_op.variable.empty()) {
            output_schema.push_back(scan_op.variable);
            output_types.push_back(binder::BoundType::Vertex());
        }

        auto result_output_types = output_types;

        if (!last_is_range) {
            result = std::make_unique<IndexScanPhysicalOp>(
                scan_op.variable, label_id, idx.prop_ids, ScanMode::EQUALITY, std::move(eq_values), std::nullopt,
                std::nullopt, std::move(output_types), store, ctx.label_defs, scan_op.label_prop_ids);
        } else {
            std::optional<std::vector<PropertyValue>> composite_start;
            std::optional<std::vector<PropertyValue>> composite_end;
            if (range_start.has_value()) {
                auto start_vec = eq_values;
                start_vec.push_back(std::move(*range_start));
                composite_start = std::move(start_vec);
            } else if (!eq_values.empty()) {
                composite_start = eq_values;
            }
            if (range_end.has_value()) {
                auto end_vec = eq_values;
                end_vec.push_back(std::move(*range_end));
                composite_end = std::move(end_vec);
            }
            result = std::make_unique<IndexScanPhysicalOp>(scan_op.variable, label_id, idx.prop_ids, ScanMode::RANGE,
                                                           std::vector<PropertyValue>{}, std::move(composite_start),
                                                           std::move(composite_end), std::move(output_types), store,
                                                           ctx.label_defs, scan_op.label_prop_ids);
        }

        return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(result_output_types)};
    }
    return std::nullopt;
}

std::optional<PlanOperatorResult> PhysicalPlanner::tryBoundEdgeIndexScan(
    const binder::BoundExpandOp& expand_op, const std::vector<const binder::BoundBinaryOp*>& conditions,
    EdgeLabelId label_id, const EdgeLabelDef& edge_label_def, IAsyncGraphDataStore& store, PlanContext& ctx) {
    std::unordered_map<uint16_t, BoundIndexableCondition> prop_conditions;
    for (auto* cond : conditions) {
        auto extracted = tryExtractBoundCondition(*cond);
        if (extracted.has_value())
            prop_conditions[extracted->prop_id] = std::move(*extracted);
    }

    if (prop_conditions.empty())
        return std::nullopt;

    for (const auto& idx : edge_label_def.indexes) {
        if (idx.state != IndexState::PUBLIC)
            continue;

        size_t match_count = 0;
        bool last_is_range = false;
        std::vector<PropertyValue> eq_values;
        std::optional<PropertyValue> range_start;
        std::optional<PropertyValue> range_end;

        for (size_t i = 0; i < idx.prop_ids.size(); ++i) {
            uint16_t pid = idx.prop_ids[i];
            auto it = prop_conditions.find(pid);
            if (it == prop_conditions.end())
                break;

            auto& cond = it->second;
            if (i < idx.prop_ids.size() - 1) {
                if (cond.op != cypher::BinaryOperator::EQ)
                    break;
                eq_values.push_back(std::move(cond.value));
                match_count++;
            } else {
                if (cond.op == cypher::BinaryOperator::EQ) {
                    eq_values.push_back(std::move(cond.value));
                    match_count++;
                } else if (cond.op == cypher::BinaryOperator::GT || cond.op == cypher::BinaryOperator::GTE) {
                    range_start = std::move(cond.value);
                    last_is_range = true;
                    match_count++;
                } else if (cond.op == cypher::BinaryOperator::LT || cond.op == cypher::BinaryOperator::LTE) {
                    range_end = std::move(cond.value);
                    last_is_range = true;
                    match_count++;
                } else {
                    break;
                }
            }
        }

        if (match_count == 0)
            continue;

        using ScanMode = EdgeIndexScanPhysicalOp::ScanMode;
        std::unique_ptr<EdgeIndexScanPhysicalOp> result;

        Schema output_schema;
        std::vector<binder::BoundType> output_types;
        if (!expand_op.src_variable.empty()) {
            output_schema.push_back(expand_op.src_variable);
            output_types.push_back(binder::BoundType::Vertex());
        }
        if (!expand_op.dst_variable.empty()) {
            output_schema.push_back(expand_op.dst_variable);
            output_types.push_back(binder::BoundType::Vertex());
        }
        if (!expand_op.edge_variable.empty()) {
            output_schema.push_back(expand_op.edge_variable);
            output_types.push_back(binder::BoundType::Edge());
        }

        auto result_output_types = output_types;

        if (!last_is_range) {
            result = std::make_unique<EdgeIndexScanPhysicalOp>(
                expand_op.src_variable, expand_op.dst_variable, expand_op.edge_variable, label_id, idx.prop_ids,
                ScanMode::EQUALITY, std::move(eq_values), std::nullopt, std::nullopt, std::move(output_types), store,
                ctx.edge_label_defs);
        } else {
            std::optional<std::vector<PropertyValue>> composite_start;
            std::optional<std::vector<PropertyValue>> composite_end;
            if (range_start.has_value()) {
                auto start_vec = eq_values;
                start_vec.push_back(std::move(*range_start));
                composite_start = std::move(start_vec);
            } else if (!eq_values.empty()) {
                composite_start = eq_values;
            }
            if (range_end.has_value()) {
                auto end_vec = eq_values;
                end_vec.push_back(std::move(*range_end));
                composite_end = std::move(end_vec);
            }
            result = std::make_unique<EdgeIndexScanPhysicalOp>(
                expand_op.src_variable, expand_op.dst_variable, expand_op.edge_variable, label_id, idx.prop_ids,
                ScanMode::RANGE, std::vector<PropertyValue>{}, std::move(composite_start), std::move(composite_end),
                std::move(output_types), store, ctx.edge_label_defs);
        }

        return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(result_output_types)};
    }
    return std::nullopt;
}

// ==================== Bound Plan Pipeline ====================

namespace {
std::unique_ptr<PhysicalOperator> finalizePlanResult(PlanOperatorResult&& result) {
    if (!result.op)
        return nullptr;
    result.op->setOutputSchema(std::move(result.output_schema), std::move(result.output_types));
    return std::move(result.op);
}

PlanOperatorResult extractChildResult(std::variant<PlanOperatorResult, std::string>&& child_result) {
    auto cr = std::move(std::get<PlanOperatorResult>(child_result));
    if (cr.op) {
        cr.op->setOutputSchema(Schema(cr.output_schema), std::vector<binder::BoundType>(cr.output_types));
    }
    return cr;
}
} // namespace

std::variant<std::unique_ptr<PhysicalOperator>, std::string>
PhysicalPlanner::planBound(binder::BoundLogicalPlan& bound_plan, IAsyncGraphDataStore& store,
                           IAsyncGraphMetaStore& meta, PlanContext& ctx) {
    Schema empty_schema;
    std::vector<binder::BoundType> empty_types;
    auto result = planBoundOperator(bound_plan.root, store, meta, ctx, empty_schema, empty_types);
    if (std::holds_alternative<std::string>(result))
        return std::get<std::string>(result);
    return finalizePlanResult(std::move(std::get<PlanOperatorResult>(result)));
}

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planBoundOperator(binder::BoundLogicalOperator& op, IAsyncGraphDataStore& store,
                                   IAsyncGraphMetaStore& meta, PlanContext& ctx, Schema input_schema,
                                   const std::vector<binder::BoundType>& input_types) {
    return std::visit(
        [this, &store, &meta, &ctx, &input_schema,
         &input_types](auto& val) -> std::variant<PlanOperatorResult, std::string> {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, binder::BoundSingletonOp>) {
                Schema output_schema;
                std::vector<binder::BoundType> output_types;
                auto result = std::make_unique<SingletonPhysicalOp>(std::vector<binder::BoundType>(output_types));
                return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                Schema output_schema;
                std::vector<binder::BoundType> output_types;
                if (!val.variable.empty()) {
                    output_schema.push_back(val.variable);
                    output_types.push_back(binder::BoundType::Vertex());
                }
                auto result = std::make_unique<AllNodeScanPhysicalOp>(
                    val.variable, std::vector<binder::BoundType>(output_types), store, ctx.label_name_to_id,
                    ctx.label_defs, val.label_prop_ids);
                return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                Schema output_schema;
                std::vector<binder::BoundType> output_types;
                if (!val.variable.empty()) {
                    output_schema.push_back(val.variable);
                    output_types.push_back(binder::BoundType::Vertex());
                }
                auto result = std::make_unique<LabelScanPhysicalOp>(val.variable, val.label_id,
                                                                    std::vector<binder::BoundType>(output_types), store,
                                                                    ctx.label_defs, val.label_prop_ids);
                return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
            } else {
                using Elem = typename T::element_type;
                if (!val)
                    return std::string("internal error: null operator in plan");
                auto& v = *val;

                if constexpr (std::is_same_v<Elem, binder::BoundExpandOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    std::optional<std::vector<EdgeLabelId>> label_filters;
                    if (!v.edge_label_ids.empty())
                        label_filters = v.edge_label_ids;

                    Schema output_schema = child_schema;
                    if (!v.edge_variable.empty()) {
                        output_schema.push_back(v.edge_variable);
                        output_types.push_back(binder::BoundType::Edge());
                    }
                    if (!v.dst_variable.empty()) {
                        output_schema.push_back(v.dst_variable);
                        output_types.push_back(binder::BoundType::Vertex());
                    }

                    auto result = std::make_unique<ExpandPhysicalOp>(
                        v.src_variable, v.dst_variable, v.edge_variable, std::move(label_filters), v.direction, store,
                        std::move(child_schema), std::vector<binder::BoundType>(output_types), std::move(child_op),
                        v.dst_label_prop_ids, v.edge_prop_ids);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundVarLenExpandOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    std::optional<std::vector<EdgeLabelId>> label_filters;
                    if (!v.edge_label_ids.empty())
                        label_filters = v.edge_label_ids;

                    Schema output_schema = child_schema;
                    output_schema.push_back(v.dst_variable);
                    output_types.push_back(binder::BoundType::Vertex());

                    // P1: add PATH column if path variable is set
                    if (!v.path_variable.empty()) {
                        output_schema.push_back(v.path_variable);
                        output_types.push_back(binder::BoundType::Path());
                    }
                    // P2: add LIST<EDGE> column if edge variable is set
                    if (!v.edge_variable.empty()) {
                        output_schema.push_back(v.edge_variable);
                        output_types.push_back(binder::BoundType::List(binder::BoundType::Edge()));
                    }

                    auto result = std::make_unique<VarLenExpandPhysicalOp>(
                        v.src_variable, v.dst_variable, std::move(label_filters), v.direction, v.min_hops, v.max_hops,
                        store, std::move(child_schema), std::vector<binder::BoundType>(output_types),
                        std::move(child_op), v.dst_label_prop_ids, v.path_variable, v.edge_variable,
                        v.edge_prop_filters);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundFilterOp>) {
                    // ── Index scan optimization: Filter(LabelScan) → IndexScan ──
                    if (std::holds_alternative<binder::BoundLabelScanOp>(v.child)) {
                        auto& scan_op = std::get<binder::BoundLabelScanOp>(v.child);
                        auto def_it = ctx.label_defs.find(scan_op.label_id);
                        if (def_it != ctx.label_defs.end()) {
                            std::vector<const binder::BoundBinaryOp*> conditions;
                            collectBoundConditions(v.predicate, conditions);
                            if (!conditions.empty()) {
                                auto idx_result = tryBoundIndexScan(scan_op, conditions, scan_op.label_id,
                                                                    def_it->second, store, ctx);
                                if (idx_result.has_value())
                                    return std::move(idx_result.value());
                            }
                        }
                    }

                    // ── Edge index scan optimization: Filter(Expand) → EdgeIndexScan ──
                    if (std::holds_alternative<std::unique_ptr<binder::BoundExpandOp>>(v.child)) {
                        auto& expand_ptr = std::get<std::unique_ptr<binder::BoundExpandOp>>(v.child);
                        if (expand_ptr->edge_label_ids.size() == 1) {
                            EdgeLabelId edge_label_id = expand_ptr->edge_label_ids[0];
                            auto def_it = ctx.edge_label_defs.find(edge_label_id);
                            if (def_it != ctx.edge_label_defs.end()) {
                                std::vector<const binder::BoundBinaryOp*> conditions;
                                collectBoundConditions(v.predicate, conditions);
                                if (!conditions.empty()) {
                                    auto idx_result = tryBoundEdgeIndexScan(*expand_ptr, conditions, edge_label_id,
                                                                            def_it->second, store, ctx);
                                    if (idx_result.has_value())
                                        return std::move(idx_result.value());
                                }
                            }
                        }
                    }

                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

                    auto result =
                        std::make_unique<FilterPhysicalOp>(std::move(v.predicate), cr.output_schema, std::move(cr.op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundProjectOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);

                    std::vector<ProjectPhysicalOp::ProjectItem> items;
                    Schema output_schema;
                    std::vector<binder::BoundType> output_types;
                    for (auto& item : v.items) {
                        ProjectPhysicalOp::ProjectItem pi;
                        pi.expr = std::move(item.expr);
                        pi.name = std::move(item.alias);
                        items.push_back(std::move(pi));
                        output_schema.push_back(items.back().name);
                        output_types.push_back(std::move(item.result_type));
                    }

                    auto result = std::make_unique<ProjectPhysicalOp>(std::move(items), std::move(cr.output_schema),
                                                                      std::move(child_op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundAggregateOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);

                    std::vector<AggregatePhysicalOp::GroupKey> group_keys;
                    for (size_t i = 0; i < v.group_keys.size() && i < v.output_names.size(); ++i) {
                        AggregatePhysicalOp::GroupKey gk;
                        gk.expr = std::move(v.group_keys[i]);
                        gk.name = v.output_names[i];
                        group_keys.push_back(std::move(gk));
                    }

                    std::vector<AggregatePhysicalOp::AggregateExpr> aggregates;
                    for (size_t i = 0; i < v.aggregates.size(); ++i) {
                        auto& ai = v.aggregates[i];
                        AggregatePhysicalOp::AggregateExpr ae;
                        ae.func_def = ai.func_def;
                        ae.arg = std::move(ai.argument);
                        ae.distinct = ai.distinct;
                        if (v.group_keys.size() + i < v.output_names.size())
                            ae.name = v.output_names[v.group_keys.size() + i];
                        aggregates.push_back(std::move(ae));
                    }

                    Schema output_schema(v.output_names.begin(), v.output_names.end());
                    std::vector<binder::BoundType> output_types;
                    std::vector<binder::BoundTypeKind> output_type_kinds;
                    for (size_t i = 0; i < group_keys.size(); ++i) {
                        auto gk_type = getBoundExprType(group_keys[i].expr);
                        output_types.push_back(gk_type);
                        output_type_kinds.push_back(gk_type.kind);
                    }
                    for (const auto& ae : aggregates) {
                        if (ae.func_def) {
                            output_types.push_back(ae.func_def->return_type);
                            output_type_kinds.push_back(ae.func_def->return_type.kind);
                        } else {
                            output_types.push_back(binder::BoundType::Any());
                            output_type_kinds.push_back(binder::BoundTypeKind::ANY);
                        }
                    }

                    auto result =
                        std::make_unique<AggregatePhysicalOp>(std::move(group_keys), std::move(aggregates),
                                                              std::move(child_op), std::move(output_type_kinds));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundSortOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

                    std::vector<SortPhysicalOp::SortItem> sort_items;
                    for (auto& si : v.items) {
                        SortPhysicalOp::SortItem item;
                        item.expr = std::move(si.expr);
                        item.ascending = (si.direction == cypher::OrderBy::Direction::ASC);
                        sort_items.push_back(std::move(item));
                    }

                    auto result = std::make_unique<SortPhysicalOp>(std::move(sort_items), std::move(cr.op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundSkipOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<SkipPhysicalOp>(v.count, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundLimitOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<LimitPhysicalOp>(v.count, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundDistinctOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<DistinctPhysicalOp>(std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundPathBuildOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    Schema output_schema = child_schema;
                    output_schema.push_back(v.path_variable);
                    output_types.push_back(binder::BoundType::Path());

                    auto result = std::make_unique<PathBuildPhysicalOp>(
                        v.path_variable, v.element_variables, std::move(child_schema),
                        std::vector<binder::BoundType>(output_types), std::move(child_op));
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundCreateNodeOp>) {
                    VertexId vid = ctx.next_vertex_id++;
                    ctx.variable_vertex_ids[v.variable] = vid;

                    std::vector<LabelId> label_ids;
                    std::vector<std::pair<LabelId, Properties>> label_props;

                    if (v.label_id != INVALID_LABEL_ID) {
                        label_ids.push_back(v.label_id);
                        Properties props;
                        for (auto& [pid, expr] : v.properties) {
                            if (std::holds_alternative<binder::BoundLiteral>(expr)) {
                                auto& lit = std::get<binder::BoundLiteral>(expr);
                                if (!isNull(lit.value)) {
                                    if (props.size() <= pid)
                                        props.resize(pid + 1);
                                    props[pid] = valueToPropertyValue(lit.value);
                                }
                            }
                        }
                        label_props.emplace_back(v.label_id, std::move(props));
                    }

                    std::unique_ptr<PhysicalOperator> child;
                    if (v.child) {
                        auto child_result = planBoundOperator(*v.child, store, meta, ctx, input_schema, input_types);
                        if (std::holds_alternative<std::string>(child_result))
                            return std::get<std::string>(child_result);
                        child = finalizePlanResult(std::move(std::get<PlanOperatorResult>(child_result)));
                    }

                    auto result =
                        std::make_unique<CreateNodePhysicalOp>(v.variable, std::move(label_ids), std::move(label_props),
                                                               store, vid, std::move(child), ctx.label_defs);
                    Schema node_schema;
                    if (!v.variable.empty())
                        node_schema.push_back(v.variable);
                    return PlanOperatorResult{std::move(result), std::move(node_schema), {binder::BoundType::Vertex()}};
                } else if constexpr (std::is_same_v<Elem, binder::BoundCreateEdgeOp>) {
                    EdgeId eid = ctx.next_edge_id++;
                    ctx.variable_edge_ids[v.variable] = eid;

                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto child_cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(child_cr.op);

                    // Extract property names from pending_props for schema registration
                    std::vector<std::string> pending_prop_names;
                    for (const auto& [name, _] : v.pending_props)
                        pending_prop_names.push_back(name);

                    if (v.label_name.has_value()) {
                        // Edge type doesn't exist: create it (with properties if any)
                        child_op = std::make_unique<CreateEdgeLabelPhysicalOp>(
                            *v.label_name, std::move(pending_prop_names), meta, store, ctx.edge_label_name_to_id,
                            ctx.edge_label_defs, std::move(child_op));
                    } else if (v.label_id.has_value() && !v.pending_props.empty()) {
                        // Edge type exists but has new properties: alter it
                        auto def_it = ctx.edge_label_defs.find(*v.label_id);
                        if (def_it != ctx.edge_label_defs.end()) {
                            child_op = std::make_unique<AlterEdgeLabelPhysicalOp>(
                                def_it->second.name, std::move(pending_prop_names), meta, ctx.edge_label_defs,
                                std::move(child_op));
                        }
                    }

                    VertexId src_id = 0, dst_id = 0;
                    auto sit = ctx.variable_vertex_ids.find(v.src_variable);
                    if (sit != ctx.variable_vertex_ids.end())
                        src_id = sit->second;
                    auto dit = ctx.variable_vertex_ids.find(v.dst_variable);
                    if (dit != ctx.variable_vertex_ids.end())
                        dst_id = dit->second;

                    Properties props;
                    for (auto& [pid, expr] : v.properties) {
                        if (std::holds_alternative<binder::BoundLiteral>(expr)) {
                            auto& lit = std::get<binder::BoundLiteral>(expr);
                            if (!isNull(lit.value)) {
                                if (props.size() <= pid)
                                    props.resize(pid + 1);
                                props[pid] = valueToPropertyValue(lit.value);
                            }
                        }
                    }

                    auto result = std::make_unique<CreateEdgePhysicalOp>(
                        v.variable, src_id, dst_id, v.label_id, eid, std::move(props), store, ctx.edge_label_defs,
                        v.label_name, ctx.edge_label_name_to_id, std::move(v.pending_props), std::move(child_op));
                    Schema edge_schema;
                    if (!v.variable.empty())
                        edge_schema.push_back(v.variable);
                    return PlanOperatorResult{std::move(result), std::move(edge_schema), {binder::BoundType::Edge()}};
                } else if constexpr (std::is_same_v<Elem, binder::BoundSetOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

                    std::vector<SetPhysicalOp::BoundSetItem> items;
                    for (auto& si : v.items) {
                        SetPhysicalOp::BoundSetItem bsi;
                        switch (si.kind) {
                        case binder::BoundSetOp::ItemKind::SET_PROPERTY:
                            bsi.kind = cypher::SetItemKind::SET_PROPERTY;
                            break;
                        case binder::BoundSetOp::ItemKind::SET_LABELS:
                            bsi.kind = cypher::SetItemKind::SET_LABELS;
                            break;
                        default:
                            bsi.kind = cypher::SetItemKind::SET_PROPERTY;
                            break;
                        }
                        bsi.var_name = si.target_variable;
                        if (si.prop_id && si.label_id) {
                            auto it = ctx.label_defs.find(*si.label_id);
                            if (it != ctx.label_defs.end()) {
                                for (const auto& pd : it->second.properties) {
                                    if (pd.id == *si.prop_id) {
                                        bsi.prop_name = pd.name;
                                        break;
                                    }
                                }
                            }
                        }
                        if (si.label_id) {
                            for (auto& [name, id] : ctx.label_name_to_id) {
                                if (id == *si.label_id) {
                                    bsi.label = name;
                                    break;
                                }
                            }
                        }
                        if (si.value_expr)
                            bsi.value = std::move(si.value_expr);
                        items.push_back(std::move(bsi));
                    }

                    auto result =
                        std::make_unique<SetPhysicalOp>(std::move(items), cr.output_schema, store, ctx.label_defs,
                                                        ctx.label_name_to_id, std::move(cr.op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundRemoveOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

                    std::vector<RemovePhysicalOp::BoundRemoveItem> items;
                    for (auto& ri : v.items) {
                        RemovePhysicalOp::BoundRemoveItem bri;
                        bri.kind = (ri.kind == binder::BoundRemoveOp::ItemKind::REMOVE_LABEL)
                                       ? RemovePhysicalOp::BoundRemoveItem::Kind::LABEL
                                       : RemovePhysicalOp::BoundRemoveItem::Kind::PROPERTY;
                        bri.var_name = ri.target_variable;
                        items.push_back(std::move(bri));
                    }

                    auto result =
                        std::make_unique<RemovePhysicalOp>(std::move(items), cr.output_schema, store, ctx.label_defs,
                                                           ctx.label_name_to_id, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundBinaryJoinOp>) {
                    auto left_result = planBoundOperator(v.left, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(left_result))
                        return std::get<std::string>(left_result);
                    auto lr = extractChildResult(std::move(left_result));

                    Schema right_input_schema;
                    std::vector<binder::BoundType> right_input_types;
                    auto right_result =
                        planBoundOperator(v.right, store, meta, ctx, right_input_schema, right_input_types);
                    if (std::holds_alternative<std::string>(right_result))
                        return std::get<std::string>(right_result);
                    auto rr = extractChildResult(std::move(right_result));

                    Schema output_schema = lr.output_schema;
                    output_schema.insert(output_schema.end(), rr.output_schema.begin(), rr.output_schema.end());
                    auto output_types = lr.output_types;
                    output_types.insert(output_types.end(), rr.output_types.begin(), rr.output_types.end());

                    auto result = std::make_unique<CrossProductPhysicalOp>(
                        std::move(lr.op), std::move(rr.op), std::move(lr.output_schema), std::move(rr.output_schema),
                        std::move(output_types));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                } else {
                    return std::string("Unknown bound logical operator type");
                }
            }
        },
        op);
}

} // namespace compute
} // namespace eugraph
