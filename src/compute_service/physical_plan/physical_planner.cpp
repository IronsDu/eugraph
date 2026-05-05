#include "compute_service/physical_plan/physical_planner.hpp"

#include <stdexcept>

namespace eugraph {
namespace compute {

// ==================== Public ====================

std::variant<std::unique_ptr<PhysicalOperator>, std::string>
PhysicalPlanner::plan(LogicalPlan& logical_plan, IAsyncGraphDataStore& store, PlanContext& ctx) {
    Schema empty_schema;
    auto result = planOperator(logical_plan.root, store, ctx, empty_schema);
    if (std::holds_alternative<std::string>(result)) {
        return std::get<std::string>(result);
    }
    return std::move(std::get<PlanOperatorResult>(result).op);
}

// ==================== Dispatcher ====================

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planOperator(LogicalOperator& op, IAsyncGraphDataStore& store, PlanContext& ctx, Schema input_schema) {
    return std::visit(
        [this, &store, &ctx, &input_schema](auto& ptr) -> std::variant<PlanOperatorResult, std::string> {
            using T = std::decay_t<decltype(ptr)>;
            using OpType = typename T::element_type;

            if constexpr (std::is_same_v<OpType, AllNodeScanOp>)
                return planAllNodeScan(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, LabelScanOp>)
                return planLabelScan(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, ExpandOp>)
                return planExpand(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, FilterOp>)
                return planFilter(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, ProjectOp>)
                return planProject(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, LimitOp>)
                return planLimit(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, SkipOp>)
                return planSkip(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, SortOp>)
                return planSort(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, DistinctOp>)
                return planDistinct(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, AggregateOp>)
                return planAggregate(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, CreateNodeOp>)
                return planCreateNode(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, CreateEdgeOp>)
                return planCreateEdge(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, SetOp>)
                return planSet(*ptr, store, ctx, input_schema);
            else if constexpr (std::is_same_v<OpType, RemoveOp>)
                return planRemove(*ptr, store, ctx, input_schema);
            else
                return std::string("Unknown logical operator type");
        },
        op);
}

// ==================== Scan ====================

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planAllNodeScan(AllNodeScanOp& op, IAsyncGraphDataStore& store, PlanContext& ctx, Schema) {
    auto result = std::make_unique<AllNodeScanPhysicalOp>(op.variable, store, ctx.label_name_to_id, ctx.label_defs);
    Schema output_schema;
    if (!op.variable.empty()) {
        output_schema.push_back(op.variable);
    }
    return PlanOperatorResult{std::move(result), std::move(output_schema)};
}

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planLabelScan(LabelScanOp& op, IAsyncGraphDataStore& store, PlanContext& ctx, Schema) {
    auto it = ctx.label_name_to_id.find(op.label);
    if (it == ctx.label_name_to_id.end()) {
        return "Unknown label: " + op.label;
    }
    auto result = std::make_unique<LabelScanPhysicalOp>(op.variable, it->second, store, ctx.label_defs);
    Schema output_schema;
    if (!op.variable.empty()) {
        output_schema.push_back(op.variable);
    }
    return PlanOperatorResult{std::move(result), std::move(output_schema)};
}

// ==================== Expand ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planExpand(ExpandOp& op, IAsyncGraphDataStore& store,
                                                                          PlanContext& ctx, Schema input_schema) {
    if (op.children.size() != 1) {
        return "Expand requires exactly one child";
    }
    auto child_result = planOperator(op.children[0], store, ctx, input_schema);
    if (std::holds_alternative<std::string>(child_result)) {
        return std::get<std::string>(child_result);
    }
    auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
    auto child_op = std::move(child_res.op);
    auto child_schema = std::move(child_res.output_schema);

    std::optional<std::vector<EdgeLabelId>> label_filters;
    if (!op.rel_types.empty()) {
        label_filters = std::vector<EdgeLabelId>();
        for (const auto& type_name : op.rel_types) {
            auto it = ctx.edge_label_name_to_id.find(type_name);
            if (it != ctx.edge_label_name_to_id.end()) {
                label_filters->push_back(it->second);
            }
        }
    }

    auto result =
        std::make_unique<ExpandPhysicalOp>(op.src_variable, op.dst_variable, op.edge_variable, std::move(label_filters),
                                           op.direction, store, child_schema, std::move(child_op));

    Schema output_schema = child_schema;
    if (!op.dst_variable.empty()) {
        output_schema.push_back(op.dst_variable);
    }
    if (!op.edge_variable.empty()) {
        output_schema.push_back(op.edge_variable);
    }
    return PlanOperatorResult{std::move(result), std::move(output_schema)};
}

// ==================== Filter ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planFilter(FilterOp& op, IAsyncGraphDataStore& store,
                                                                          PlanContext& ctx, Schema input_schema) {
    if (op.children.size() != 1) {
        return "Filter requires exactly one child";
    }

    if (auto err = validateExpression(op.predicate, ctx); !err.empty())
        return err;

    // Helper lambda to collect indexable conditions from a predicate
    auto collectConditions = [](const cypher::Expression& pred,
                                std::vector<const cypher::BinaryOp*>& conditions) {
        if (std::holds_alternative<std::unique_ptr<cypher::BinaryOp>>(pred)) {
            auto& binop = std::get<std::unique_ptr<cypher::BinaryOp>>(pred);
            if (binop->op == cypher::BinaryOperator::AND) {
                auto collect = [](auto& self, const cypher::BinaryOp& node,
                                  std::vector<const cypher::BinaryOp*>& out) -> void {
                    if (node.op == cypher::BinaryOperator::AND) {
                        if (std::holds_alternative<std::unique_ptr<cypher::BinaryOp>>(node.left))
                            self(self, *std::get<std::unique_ptr<cypher::BinaryOp>>(node.left), out);
                        if (std::holds_alternative<std::unique_ptr<cypher::BinaryOp>>(node.right))
                            self(self, *std::get<std::unique_ptr<cypher::BinaryOp>>(node.right), out);
                    } else {
                        out.push_back(&node);
                    }
                };
                collect(collect, *binop, conditions);
            } else {
                conditions.push_back(binop.get());
            }
        }
    };

    // Try vertex index scan optimization: if child is LabelScan and predicate is indexable
    if (std::holds_alternative<std::unique_ptr<LabelScanOp>>(op.children[0])) {
        auto& scan_op = std::get<std::unique_ptr<LabelScanOp>>(op.children[0]);
        auto label_it = ctx.label_name_to_id.find(scan_op->label);
        if (label_it != ctx.label_name_to_id.end()) {
            LabelId label_id = label_it->second;
            auto def_it = ctx.label_defs.find(label_id);
            if (def_it != ctx.label_defs.end()) {
                std::vector<const cypher::BinaryOp*> conditions;
                collectConditions(op.predicate, conditions);
                if (!conditions.empty()) {
                    auto idx_result = tryIndexScan(*scan_op, conditions, label_id, def_it->second, store, ctx);
                    if (idx_result.has_value()) {
                        return std::move(idx_result.value());
                    }
                }
            }
        }
    }

    // Try edge index scan optimization: if child is Expand with a single label filter
    if (std::holds_alternative<std::unique_ptr<ExpandOp>>(op.children[0])) {
        auto& expand_op = std::get<std::unique_ptr<ExpandOp>>(op.children[0]);
        if (expand_op->rel_types.size() == 1) {
            auto edge_label_it = ctx.edge_label_name_to_id.find(expand_op->rel_types[0]);
            if (edge_label_it != ctx.edge_label_name_to_id.end()) {
                EdgeLabelId edge_label_id = edge_label_it->second;
                auto def_it = ctx.edge_label_defs.find(edge_label_id);
                if (def_it != ctx.edge_label_defs.end()) {
                    std::vector<const cypher::BinaryOp*> conditions;
                    collectConditions(op.predicate, conditions);
                    if (!conditions.empty()) {
                        auto idx_result =
                            tryEdgeIndexScan(*expand_op, conditions, edge_label_id, def_it->second, store, ctx);
                        if (idx_result.has_value()) {
                            return std::move(idx_result.value());
                        }
                    }
                }
            }
        }
    }

    auto child_result = planOperator(op.children[0], store, ctx, input_schema);
    if (std::holds_alternative<std::string>(child_result)) {
        return std::get<std::string>(child_result);
    }
    auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
    auto child_op = std::move(child_res.op);
    auto child_schema = std::move(child_res.output_schema);

    auto result = std::make_unique<FilterPhysicalOp>(std::move(op.predicate), Schema(child_schema), std::move(child_op),
                                                     ctx.label_defs);
    return PlanOperatorResult{std::move(result), std::move(child_schema)};
}

// ==================== Project ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planProject(ProjectOp& op, IAsyncGraphDataStore& store,
                                                                           PlanContext& ctx, Schema input_schema) {
    if (op.children.size() != 1) {
        return "Project requires exactly one child";
    }

    for (auto& item : op.items) {
        if (auto err = validateExpression(item.expr, ctx); !err.empty())
            return err;
    }

    auto child_result = planOperator(op.children[0], store, ctx, input_schema);
    if (std::holds_alternative<std::string>(child_result)) {
        return std::get<std::string>(child_result);
    }
    auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
    auto child_op = std::move(child_res.op);
    auto child_schema = std::move(child_res.output_schema);

    std::vector<ProjectPhysicalOp::ProjectItem> items;
    Schema output_schema;
    for (auto& item : op.items) {
        ProjectPhysicalOp::ProjectItem pi;
        pi.expr = std::move(item.expr);
        pi.name = item.alias.value_or(cypher::expressionToString(pi.expr));
        output_schema.push_back(pi.name);
        items.push_back(std::move(pi));
    }

    auto result = std::make_unique<ProjectPhysicalOp>(std::move(items), Schema(child_schema), std::move(child_op),
                                                      ctx.label_defs);
    return PlanOperatorResult{std::move(result), std::move(output_schema)};
}

// ==================== Limit ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planLimit(LimitOp& op, IAsyncGraphDataStore& store,
                                                                         PlanContext& ctx, Schema input_schema) {
    if (op.children.size() != 1) {
        return "Limit requires exactly one child";
    }
    auto child_result = planOperator(op.children[0], store, ctx, input_schema);
    if (std::holds_alternative<std::string>(child_result)) {
        return std::get<std::string>(child_result);
    }
    auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
    auto result = std::make_unique<LimitPhysicalOp>(op.limit, std::move(child_res.op));
    return PlanOperatorResult{std::move(result), std::move(child_res.output_schema)};
}

// ==================== Skip ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planSkip(SkipOp& op, IAsyncGraphDataStore& store,
                                                                        PlanContext& ctx, Schema input_schema) {
    if (op.children.size() != 1) {
        return "Skip requires exactly one child";
    }
    auto child_result = planOperator(op.children[0], store, ctx, input_schema);
    if (std::holds_alternative<std::string>(child_result)) {
        return std::get<std::string>(child_result);
    }
    auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
    auto result = std::make_unique<SkipPhysicalOp>(op.skip, std::move(child_res.op));
    return PlanOperatorResult{std::move(result), std::move(child_res.output_schema)};
}

// ==================== Sort ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planSort(SortOp& op, IAsyncGraphDataStore& store,
                                                                        PlanContext& ctx, Schema input_schema) {
    if (op.children.size() != 1) {
        return "Sort requires exactly one child";
    }

    for (auto& si : op.sort_items) {
        if (auto err = validateExpression(si.expr, ctx); !err.empty())
            return err;
    }

    auto child_result = planOperator(op.children[0], store, ctx, input_schema);
    if (std::holds_alternative<std::string>(child_result)) {
        return std::get<std::string>(child_result);
    }
    auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
    auto child_op = std::move(child_res.op);
    auto child_schema = std::move(child_res.output_schema);

    std::vector<SortPhysicalOp::SortItem> items;
    for (auto& si : op.sort_items) {
        SortPhysicalOp::SortItem item;
        item.expr = std::move(si.expr);
        item.direction = si.direction;
        items.push_back(std::move(item));
    }

    auto result =
        std::make_unique<SortPhysicalOp>(std::move(items), Schema(child_schema), std::move(child_op), ctx.label_defs);
    return PlanOperatorResult{std::move(result), std::move(child_schema)};
}

// ==================== Distinct ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planDistinct(DistinctOp& op, IAsyncGraphDataStore& store,
                                                                            PlanContext& ctx, Schema input_schema) {
    if (op.children.size() != 1) {
        return "Distinct requires exactly one child";
    }
    auto child_result = planOperator(op.children[0], store, ctx, input_schema);
    if (std::holds_alternative<std::string>(child_result)) {
        return std::get<std::string>(child_result);
    }
    auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
    auto result = std::make_unique<DistinctPhysicalOp>(std::move(child_res.op));
    return PlanOperatorResult{std::move(result), std::move(child_res.output_schema)};
}

// ==================== Aggregate ====================

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planAggregate(AggregateOp& op, IAsyncGraphDataStore& store, PlanContext& ctx, Schema input_schema) {
    if (op.children.size() != 1) {
        return "Aggregate requires exactly one child";
    }

    for (auto& gk : op.group_keys) {
        if (auto err = validateExpression(gk, ctx); !err.empty())
            return err;
    }
    for (auto& af : op.aggregates) {
        if (auto err = validateExpression(af.arg, ctx); !err.empty())
            return err;
    }

    auto child_result = planOperator(op.children[0], store, ctx, input_schema);
    if (std::holds_alternative<std::string>(child_result)) {
        return std::get<std::string>(child_result);
    }
    auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
    auto child_op = std::move(child_res.op);
    auto child_schema = std::move(child_res.output_schema);

    std::vector<AggregatePhysicalOp::GroupKey> group_keys;
    for (auto& gk : op.group_keys) {
        AggregatePhysicalOp::GroupKey key;
        key.expr = std::move(gk);
        group_keys.push_back(std::move(key));
    }

    std::vector<AggregatePhysicalOp::AggregateExpr> aggregates;
    for (auto& af : op.aggregates) {
        AggregatePhysicalOp::AggregateExpr ae;
        ae.func_name = std::move(af.func_name);
        ae.arg = std::move(af.arg);
        ae.distinct = af.distinct;
        aggregates.push_back(std::move(ae));
    }

    Schema output_schema = Schema(op.output_names);

    for (size_t i = 0; i < group_keys.size() && i < op.output_names.size(); ++i) {
        group_keys[i].name = op.output_names[i];
    }
    for (size_t i = 0; i < aggregates.size() && group_keys.size() + i < op.output_names.size(); ++i) {
        aggregates[i].name = op.output_names[group_keys.size() + i];
    }

    auto result = std::make_unique<AggregatePhysicalOp>(std::move(group_keys), std::move(aggregates),
                                                        std::move(child_op), Schema(child_schema), ctx.label_defs);
    return PlanOperatorResult{std::move(result), std::move(output_schema)};
}

// ==================== Create ====================

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planCreateNode(CreateNodeOp& op, IAsyncGraphDataStore& store, PlanContext& ctx, Schema input_schema) {
    std::unique_ptr<PhysicalOperator> child_op;
    Schema child_schema;
    if (!op.children.empty()) {
        auto child_result = planOperator(op.children[0], store, ctx, input_schema);
        if (std::holds_alternative<std::string>(child_result)) {
            return std::get<std::string>(child_result);
        }
        auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
        child_op = std::move(child_res.op);
        child_schema = std::move(child_res.output_schema);
    }

    VertexId vid = ctx.next_vertex_id++;
    if (!op.variable.empty()) {
        ctx.variable_vertex_ids[op.variable] = vid;
    }

    std::vector<LabelId> label_ids;
    std::vector<std::pair<LabelId, Properties>> label_props;
    for (const auto& label_name : op.labels) {
        auto it = ctx.label_name_to_id.find(label_name);
        if (it != ctx.label_name_to_id.end()) {
            LabelId lid = it->second;
            label_ids.push_back(lid);

            Properties props;
            if (op.properties.has_value()) {
                auto def_it = ctx.label_defs.find(lid);
                if (def_it != ctx.label_defs.end()) {
                    const auto& def = def_it->second;
                    std::unordered_map<std::string, uint16_t> name_to_id;
                    for (const auto& pd : def.properties) {
                        name_to_id[pd.name] = pd.id;
                    }
                    if (!name_to_id.empty()) {
                        props.resize(std::max_element(name_to_id.begin(), name_to_id.end(),
                                                      [](const auto& a, const auto& b) { return a.second < b.second; })
                                         ->second +
                                     1);
                    }
                    for (const auto& [pname, expr] : op.properties->entries) {
                        auto pi_it = name_to_id.find(pname);
                        if (pi_it != name_to_id.end()) {
                            PropertyValue val;
                            if (std::holds_alternative<std::unique_ptr<cypher::Literal>>(expr)) {
                                auto& lit = std::get<std::unique_ptr<cypher::Literal>>(expr);
                                if (std::holds_alternative<std::string>(lit->value))
                                    val = std::get<std::string>(lit->value);
                                else if (std::holds_alternative<int64_t>(lit->value))
                                    val = std::get<int64_t>(lit->value);
                                else if (std::holds_alternative<double>(lit->value))
                                    val = std::get<double>(lit->value);
                                else if (std::holds_alternative<bool>(lit->value))
                                    val = std::get<bool>(lit->value);
                            }
                            props[pi_it->second] = std::move(val);
                        }
                    }
                }
            }
            label_props.emplace_back(lid, std::move(props));
        }
    }

    auto result = std::make_unique<CreateNodePhysicalOp>(op.variable, std::move(label_ids), std::move(label_props),
                                                         store, vid, std::move(child_op), ctx.label_defs);

    Schema output_schema;
    if (!op.variable.empty()) {
        output_schema.push_back(op.variable);
    }
    return PlanOperatorResult{std::move(result), std::move(output_schema)};
}

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planCreateEdge(CreateEdgeOp& op, IAsyncGraphDataStore& store, PlanContext& ctx, Schema input_schema) {
    std::unique_ptr<PhysicalOperator> child_op;
    Schema child_schema;
    if (!op.children.empty()) {
        auto child_result = planOperator(op.children[0], store, ctx, input_schema);
        if (std::holds_alternative<std::string>(child_result)) {
            return std::get<std::string>(child_result);
        }
        auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
        child_op = std::move(child_res.op);
        child_schema = std::move(child_res.output_schema);
    }

    VertexId src_id = INVALID_VERTEX_ID;
    VertexId dst_id = INVALID_VERTEX_ID;
    auto src_it = ctx.variable_vertex_ids.find(op.src_variable);
    if (src_it != ctx.variable_vertex_ids.end()) {
        src_id = src_it->second;
    }
    auto dst_it = ctx.variable_vertex_ids.find(op.dst_variable);
    if (dst_it != ctx.variable_vertex_ids.end()) {
        dst_id = dst_it->second;
    }

    if (src_id == INVALID_VERTEX_ID || dst_id == INVALID_VERTEX_ID) {
        return "Cannot resolve source/destination vertex IDs for edge creation";
    }

    EdgeId eid = ctx.next_edge_id++;

    EdgeLabelId label_id = INVALID_EDGE_LABEL_ID;
    if (!op.rel_types.empty()) {
        auto it = ctx.edge_label_name_to_id.find(op.rel_types[0]);
        if (it != ctx.edge_label_name_to_id.end()) {
            label_id = it->second;
        }
    }
    if (label_id == INVALID_EDGE_LABEL_ID) {
        return "Cannot resolve edge label";
    }

    // Build edge properties from the query (if any)
    Properties props;
    if (op.properties.has_value()) {
        auto def_it = ctx.edge_label_defs.find(label_id);
        if (def_it != ctx.edge_label_defs.end()) {
            const auto& def = def_it->second;
            std::unordered_map<std::string, uint16_t> name_to_id;
            for (const auto& pd : def.properties) {
                name_to_id[pd.name] = pd.id;
            }
            if (!name_to_id.empty()) {
                uint16_t max_id = 0;
                for (const auto& [name, pid] : name_to_id) {
                    (void)name;
                    if (pid > max_id) max_id = pid;
                }
                props.resize(max_id + 1);
            }
            for (const auto& [pname, expr] : op.properties->entries) {
                auto pi_it = name_to_id.find(pname);
                if (pi_it != name_to_id.end()) {
                    PropertyValue val;
                    if (std::holds_alternative<std::unique_ptr<cypher::Literal>>(expr)) {
                        auto& lit = std::get<std::unique_ptr<cypher::Literal>>(expr);
                        if (std::holds_alternative<std::string>(lit->value))
                            val = std::get<std::string>(lit->value);
                        else if (std::holds_alternative<int64_t>(lit->value))
                            val = std::get<int64_t>(lit->value);
                        else if (std::holds_alternative<double>(lit->value))
                            val = std::get<double>(lit->value);
                        else if (std::holds_alternative<bool>(lit->value))
                            val = std::get<bool>(lit->value);
                    }
                    props[pi_it->second] = std::move(val);
                }
            }
        }
    }

    auto result = std::make_unique<CreateEdgePhysicalOp>(op.variable, src_id, dst_id, label_id, eid, std::move(props),
                                                         store, ctx.edge_label_defs, std::move(child_op));

    Schema output_schema;
    if (!op.variable.empty()) {
        output_schema.push_back(op.variable);
    }
    return PlanOperatorResult{std::move(result), std::move(output_schema)};
}

// ==================== SET ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planSet(SetOp& op, IAsyncGraphDataStore& store,
                                                                       PlanContext& ctx, Schema input_schema) {
    std::unique_ptr<PhysicalOperator> child_op;
    Schema child_schema = input_schema;
    if (!op.children.empty()) {
        auto child_result = planOperator(op.children[0], store, ctx, input_schema);
        if (std::holds_alternative<std::string>(child_result)) {
            return std::get<std::string>(child_result);
        }
        auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
        child_op = std::move(child_res.op);
        child_schema = std::move(child_res.output_schema);
    }

    auto result = std::make_unique<SetPhysicalOp>(std::move(op.items), child_schema, store, ctx.label_defs,
                                                  ctx.label_name_to_id, std::move(child_op));
    return PlanOperatorResult{std::move(result), child_schema};
}

// ==================== REMOVE ====================

std::variant<PlanOperatorResult, std::string> PhysicalPlanner::planRemove(RemoveOp& op, IAsyncGraphDataStore& store,
                                                                          PlanContext& ctx, Schema input_schema) {
    std::unique_ptr<PhysicalOperator> child_op;
    Schema child_schema = input_schema;
    if (!op.children.empty()) {
        auto child_result = planOperator(op.children[0], store, ctx, input_schema);
        if (std::holds_alternative<std::string>(child_result)) {
            return std::get<std::string>(child_result);
        }
        auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
        child_op = std::move(child_res.op);
        child_schema = std::move(child_res.output_schema);
    }

    auto result = std::make_unique<RemovePhysicalOp>(std::move(op.items), child_schema, store, ctx.label_defs,
                                                     ctx.label_name_to_id, std::move(child_op));
    return PlanOperatorResult{std::move(result), child_schema};
}

// ==================== Expression Validation ====================

std::string PhysicalPlanner::validateExpression(const cypher::Expression& expr, const PlanContext& ctx) const {
    return std::visit(
        [&](const auto& ptr) -> std::string {
            using T = std::decay_t<decltype(ptr)>;
            using Elem = typename T::element_type;

            // Recursively validate sub-expressions first
            if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                if (auto err = validateExpression(ptr->left, ctx); !err.empty())
                    return err;
                if (auto err = validateExpression(ptr->right, ctx); !err.empty())
                    return err;
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                if (auto err = validateExpression(ptr->operand, ctx); !err.empty())
                    return err;
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                for (const auto& arg : ptr->args) {
                    if (auto err = validateExpression(arg, ctx); !err.empty())
                        return err;
                }
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                // Validate the sub-expression first (e.g. LabelCastExpr inside)
                if (auto err = validateExpression(ptr->object, ctx); !err.empty())
                    return err;

                // If object is a LabelCastExpr, validate property exists in that label
                if (std::holds_alternative<std::unique_ptr<cypher::LabelCastExpr>>(ptr->object)) {
                    auto& lc = std::get<std::unique_ptr<cypher::LabelCastExpr>>(ptr->object);
                    for (const auto& [lid, ldef] : ctx.label_defs) {
                        if (ldef.name == lc->label) {
                            for (const auto& pd : ldef.properties) {
                                if (pd.name == ptr->property)
                                    return std::string{}; // found, valid
                            }
                            return "Label '" + lc->label + "' has no property '" + ptr->property + "'";
                        }
                    }
                }
            } else if constexpr (std::is_same_v<Elem, cypher::LabelCastExpr>) {
                // Validate the sub-expression first
                if (auto err = validateExpression(ptr->object, ctx); !err.empty())
                    return err;

                // Validate the label exists
                for (const auto& [lid, ldef] : ctx.label_defs) {
                    if (ldef.name == ptr->label)
                        return std::string{}; // found
                }
                return "Label '" + ptr->label + "' not found";
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                if (ptr->subject) {
                    if (auto err = validateExpression(*ptr->subject, ctx); !err.empty())
                        return err;
                }
                for (const auto& [when, then] : ptr->when_thens) {
                    if (auto err = validateExpression(when, ctx); !err.empty())
                        return err;
                    if (auto err = validateExpression(then, ctx); !err.empty())
                        return err;
                }
                if (ptr->else_expr) {
                    if (auto err = validateExpression(*ptr->else_expr, ctx); !err.empty())
                        return err;
                }
            }
            return std::string{};
        },
        expr);
}

// ==================== Index Scan Optimization ====================

// Extract PropertyValue from a cypher::Literal
static std::optional<PropertyValue> literalToPropValue(const cypher::Literal& lit) {
    if (std::holds_alternative<std::string>(lit.value))
        return std::get<std::string>(lit.value);
    if (std::holds_alternative<int64_t>(lit.value))
        return std::get<int64_t>(lit.value);
    if (std::holds_alternative<double>(lit.value))
        return std::get<double>(lit.value);
    if (std::holds_alternative<bool>(lit.value))
        return std::get<bool>(lit.value);
    return std::nullopt;
}

// Check if a condition is PropertyAccess op Literal, returning (prop_name, op, value)
struct IndexableCondition {
    std::string prop_name;
    cypher::BinaryOperator op;
    PropertyValue value;
};

static std::optional<IndexableCondition> tryExtractCondition(const cypher::BinaryOp& binop) {
    if (!std::holds_alternative<std::unique_ptr<cypher::PropertyAccess>>(binop.left) ||
        !std::holds_alternative<std::unique_ptr<cypher::Literal>>(binop.right)) {
        return std::nullopt;
    }
    auto& pa = std::get<std::unique_ptr<cypher::PropertyAccess>>(binop.left);
    auto& lit = std::get<std::unique_ptr<cypher::Literal>>(binop.right);
    auto pv = literalToPropValue(*lit);
    if (!pv)
        return std::nullopt;
    return IndexableCondition{pa->property, binop.op, std::move(*pv)};
}

std::optional<PlanOperatorResult> PhysicalPlanner::tryIndexScan(LabelScanOp& scan_op,
                                                                const std::vector<const cypher::BinaryOp*>& conditions,
                                                                LabelId label_id, const LabelDef& label_def,
                                                                IAsyncGraphDataStore& store, PlanContext& ctx) {
    // Extract all indexable conditions and map property name -> (op, value)
    struct CondInfo {
        cypher::BinaryOperator op;
        PropertyValue value;
    };
    std::unordered_map<std::string, CondInfo> prop_conditions;
    for (auto* cond : conditions) {
        auto extracted = tryExtractCondition(*cond);
        if (extracted.has_value())
            prop_conditions[extracted->prop_name] = {extracted->op, std::move(extracted->value)};
    }

    if (prop_conditions.empty())
        return std::nullopt;

    // Build prop_name -> prop_id mapping
    std::unordered_map<std::string, uint16_t> name_to_id;
    for (const auto& pd : label_def.properties)
        name_to_id[pd.name] = pd.id;

    // Try to match each index
    for (const auto& idx : label_def.indexes) {
        if (idx.state != IndexState::PUBLIC)
            continue;

        // Check if the conditions match a prefix of this index
        // Conditions must be in the same order as the index columns
        // All prefix columns must have EQ conditions; only the last may have a range condition
        size_t match_count = 0;
        bool last_is_range = false;
        std::vector<PropertyValue> eq_values;
        std::optional<PropertyValue> range_start;
        std::optional<PropertyValue> range_end;

        for (size_t i = 0; i < idx.prop_ids.size(); ++i) {
            uint16_t pid = idx.prop_ids[i];
            // Find property name for this prop_id
            std::string pname;
            for (const auto& pd : label_def.properties) {
                if (pd.id == pid) {
                    pname = pd.name;
                    break;
                }
            }
            auto it = prop_conditions.find(pname);
            if (it == prop_conditions.end())
                break; // no condition on this column

            auto& cond = it->second;
            if (i < idx.prop_ids.size() - 1) {
                // Prefix columns must be exact equality matches
                if (cond.op != cypher::BinaryOperator::EQ)
                    break;
                eq_values.push_back(std::move(cond.value));
                match_count++;
            } else {
                // Last column: accept EQ, GT, GTE, LT, LTE
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

        // Build the IndexScanPhysicalOp
        using ScanMode = IndexScanPhysicalOp::ScanMode;
        std::unique_ptr<IndexScanPhysicalOp> result;

        if (!last_is_range) {
            // Full equality match on the matched prefix
            result = std::make_unique<IndexScanPhysicalOp>(scan_op.variable, label_id, idx.prop_ids, ScanMode::EQUALITY,
                                                           std::move(eq_values), std::nullopt, std::nullopt, store,
                                                           ctx.label_defs);
        } else {
            // Range scan on last column
            std::optional<std::vector<PropertyValue>> composite_start;
            std::optional<std::vector<PropertyValue>> composite_end;
            if (range_start.has_value()) {
                auto start_vec = eq_values; // copy
                start_vec.push_back(std::move(*range_start));
                composite_start = std::move(start_vec);
            } else if (!eq_values.empty()) {
                composite_start = eq_values; // prefix only as start
            }
            if (range_end.has_value()) {
                auto end_vec = eq_values; // copy
                end_vec.push_back(std::move(*range_end));
                composite_end = std::move(end_vec);
            }
            result = std::make_unique<IndexScanPhysicalOp>(scan_op.variable, label_id, idx.prop_ids, ScanMode::RANGE,
                                                           std::vector<PropertyValue>{}, std::move(composite_start),
                                                           std::move(composite_end), store, ctx.label_defs);
        }

        Schema output_schema;
        if (!scan_op.variable.empty()) {
            output_schema.push_back(scan_op.variable);
        }
        return PlanOperatorResult{std::move(result), std::move(output_schema)};
    }
    return std::nullopt;
}

std::optional<PlanOperatorResult> PhysicalPlanner::tryEdgeIndexScan(
    ExpandOp& expand_op, const std::vector<const cypher::BinaryOp*>& conditions, EdgeLabelId label_id,
    const EdgeLabelDef& edge_label_def, IAsyncGraphDataStore& store, PlanContext& ctx) {
    // Extract all indexable conditions and map property name -> (op, value)
    struct CondInfo {
        cypher::BinaryOperator op;
        PropertyValue value;
    };
    std::unordered_map<std::string, CondInfo> prop_conditions;
    for (auto* cond : conditions) {
        auto extracted = tryExtractCondition(*cond);
        if (extracted.has_value())
            prop_conditions[extracted->prop_name] = {extracted->op, std::move(extracted->value)};
    }

    if (prop_conditions.empty())
        return std::nullopt;

    // Build prop_name -> prop_id mapping
    std::unordered_map<std::string, uint16_t> name_to_id;
    for (const auto& pd : edge_label_def.properties)
        name_to_id[pd.name] = pd.id;

    // Try to match each index
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
            std::string pname;
            for (const auto& pd : edge_label_def.properties) {
                if (pd.id == pid) {
                    pname = pd.name;
                    break;
                }
            }
            auto it = prop_conditions.find(pname);
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

        if (!last_is_range) {
            result = std::make_unique<EdgeIndexScanPhysicalOp>(
                expand_op.src_variable, expand_op.dst_variable, expand_op.edge_variable, label_id, idx.prop_ids,
                ScanMode::EQUALITY, std::move(eq_values), std::nullopt, std::nullopt, store, ctx.edge_label_defs);
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
                store, ctx.edge_label_defs);
        }

        Schema output_schema;
        if (!expand_op.src_variable.empty()) output_schema.push_back(expand_op.src_variable);
        if (!expand_op.dst_variable.empty()) output_schema.push_back(expand_op.dst_variable);
        if (!expand_op.edge_variable.empty()) output_schema.push_back(expand_op.edge_variable);
        return PlanOperatorResult{std::move(result), std::move(output_schema)};
    }
    return std::nullopt;
}

} // namespace compute
} // namespace eugraph
