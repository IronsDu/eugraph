#include "compute_service/physical_plan/physical_planner.hpp"

#include <stdexcept>

namespace eugraph {
namespace compute {

std::variant<std::unique_ptr<PhysicalOperator>, std::string>
PhysicalPlanner::plan(LogicalPlan& logical_plan, IAsyncGraphDataStore& store, PlanContext& ctx) {
    Schema empty_schema;
    auto result = planOperator(logical_plan.root, store, ctx, empty_schema);
    if (std::holds_alternative<std::string>(result)) {
        return std::get<std::string>(result);
    }
    return std::move(std::get<PlanOperatorResult>(result).op);
}

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planOperator(LogicalOperator& op, IAsyncGraphDataStore& store, PlanContext& ctx, Schema input_schema) {
    return std::visit(
        [this, &store, &ctx, &input_schema](auto& ptr) -> std::variant<PlanOperatorResult, std::string> {
            using T = std::decay_t<decltype(ptr)>;
            using OpType = typename T::element_type;

            if constexpr (std::is_same_v<OpType, AllNodeScanOp>) {
                auto result =
                    std::make_unique<AllNodeScanPhysicalOp>(ptr->variable, store, ctx.label_name_to_id, ctx.label_defs);
                Schema output_schema;
                if (!ptr->variable.empty()) {
                    output_schema.push_back(ptr->variable);
                }
                return PlanOperatorResult{std::move(result), std::move(output_schema)};

            } else if constexpr (std::is_same_v<OpType, LabelScanOp>) {
                auto it = ctx.label_name_to_id.find(ptr->label);
                if (it == ctx.label_name_to_id.end()) {
                    return "Unknown label: " + ptr->label;
                }
                auto result = std::make_unique<LabelScanPhysicalOp>(ptr->variable, it->second, store, ctx.label_defs);
                Schema output_schema;
                if (!ptr->variable.empty()) {
                    output_schema.push_back(ptr->variable);
                }
                return PlanOperatorResult{std::move(result), std::move(output_schema)};

            } else if constexpr (std::is_same_v<OpType, ExpandOp>) {
                if (ptr->children.size() != 1) {
                    return "Expand requires exactly one child";
                }
                auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                if (std::holds_alternative<std::string>(child_result)) {
                    return std::get<std::string>(child_result);
                }
                auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
                auto child_op = std::move(child_res.op);
                auto child_schema = std::move(child_res.output_schema);

                std::optional<EdgeLabelId> label_filter;
                if (!ptr->rel_types.empty()) {
                    auto it = ctx.edge_label_name_to_id.find(ptr->rel_types[0]);
                    if (it != ctx.edge_label_name_to_id.end()) {
                        label_filter = it->second;
                    }
                }

                auto result =
                    std::make_unique<ExpandPhysicalOp>(ptr->src_variable, ptr->dst_variable, ptr->edge_variable,
                                                       label_filter, ptr->direction, store, std::move(child_op));

                // Extend schema with destination and edge variables
                Schema output_schema = child_schema;
                if (!ptr->dst_variable.empty()) {
                    output_schema.push_back(ptr->dst_variable);
                }
                if (!ptr->edge_variable.empty()) {
                    output_schema.push_back(ptr->edge_variable);
                }
                return PlanOperatorResult{std::move(result), std::move(output_schema)};

            } else if constexpr (std::is_same_v<OpType, FilterOp>) {
                if (ptr->children.size() != 1) {
                    return "Filter requires exactly one child";
                }

                // Try index scan optimization: if child is LabelScan and predicate is indexable
                if (std::holds_alternative<std::unique_ptr<LabelScanOp>>(ptr->children[0])) {
                    auto& scan_op = std::get<std::unique_ptr<LabelScanOp>>(ptr->children[0]);
                    auto label_it = ctx.label_name_to_id.find(scan_op->label);
                    if (label_it != ctx.label_name_to_id.end()) {
                        LabelId label_id = label_it->second;
                        auto def_it = ctx.label_defs.find(label_id);
                        if (def_it != ctx.label_defs.end()) {
                            auto& pred = ptr->predicate;
                            if (std::holds_alternative<std::unique_ptr<cypher::BinaryOp>>(pred)) {
                                auto& binop = std::get<std::unique_ptr<cypher::BinaryOp>>(pred);
                                auto idx_result = tryIndexScan(*scan_op, *binop, label_id, def_it->second, store, ctx);
                                if (idx_result.has_value()) {
                                    return std::move(idx_result.value());
                                }
                            }
                        }
                    }
                }

                auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                if (std::holds_alternative<std::string>(child_result)) {
                    return std::get<std::string>(child_result);
                }
                auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
                auto child_op = std::move(child_res.op);
                auto child_schema = std::move(child_res.output_schema);

                auto result = std::make_unique<FilterPhysicalOp>(std::move(ptr->predicate), Schema(child_schema),
                                                                 std::move(child_op), &ctx.label_defs);
                return PlanOperatorResult{std::move(result), std::move(child_schema)};

            } else if constexpr (std::is_same_v<OpType, ProjectOp>) {
                if (ptr->children.size() != 1) {
                    return "Project requires exactly one child";
                }
                auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                if (std::holds_alternative<std::string>(child_result)) {
                    return std::get<std::string>(child_result);
                }
                auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
                auto child_op = std::move(child_res.op);
                auto child_schema = std::move(child_res.output_schema);

                std::vector<ProjectPhysicalOp::ProjectItem> items;
                Schema output_schema;
                for (auto& item : ptr->items) {
                    ProjectPhysicalOp::ProjectItem pi;
                    pi.expr = std::move(item.expr);
                    pi.name = item.alias.value_or(cypher::expressionToString(pi.expr));
                    output_schema.push_back(pi.name);
                    items.push_back(std::move(pi));
                }

                auto result = std::make_unique<ProjectPhysicalOp>(std::move(items), Schema(child_schema),
                                                                  std::move(child_op), &ctx.label_defs);
                return PlanOperatorResult{std::move(result), std::move(output_schema)};

            } else if constexpr (std::is_same_v<OpType, LimitOp>) {
                if (ptr->children.size() != 1) {
                    return "Limit requires exactly one child";
                }
                auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                if (std::holds_alternative<std::string>(child_result)) {
                    return std::get<std::string>(child_result);
                }
                auto child_res = std::move(std::get<PlanOperatorResult>(child_result));
                auto child_op = std::move(child_res.op);
                auto child_schema = std::move(child_res.output_schema);

                auto result = std::make_unique<LimitPhysicalOp>(ptr->limit, std::move(child_op));
                // Limit passes through schema
                return PlanOperatorResult{std::move(result), std::move(child_schema)};

            } else if constexpr (std::is_same_v<OpType, CreateNodeOp>) {
                std::unique_ptr<PhysicalOperator> child_op;
                Schema child_schema;
                if (!ptr->children.empty()) {
                    auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                    if (std::holds_alternative<std::string>(child_result)) {
                        return std::get<std::string>(child_result);
                    }
                    auto child_res2 = std::move(std::get<PlanOperatorResult>(child_result));
                    auto cop = std::move(child_res2.op);
                    auto cschema = std::move(child_res2.output_schema);
                    child_op = std::move(cop);
                    child_schema = std::move(cschema);
                }

                VertexId vid = ctx.next_vertex_id++;
                if (!ptr->variable.empty()) {
                    ctx.variable_vertex_ids[ptr->variable] = vid;
                }

                std::vector<LabelId> label_ids;
                std::vector<std::pair<LabelId, Properties>> label_props;
                for (const auto& label_name : ptr->labels) {
                    auto it = ctx.label_name_to_id.find(label_name);
                    if (it != ctx.label_name_to_id.end()) {
                        LabelId lid = it->second;
                        label_ids.push_back(lid);

                        // Resolve properties from PropertiesMap → Properties (prop_id indexed)
                        Properties props;
                        if (ptr->properties.has_value()) {
                            auto def_it = ctx.label_defs.find(lid);
                            if (def_it != ctx.label_defs.end()) {
                                const auto& def = def_it->second;
                                // Build name → prop_id map
                                std::unordered_map<std::string, uint16_t> name_to_id;
                                for (const auto& pd : def.properties) {
                                    name_to_id[pd.name] = pd.id;
                                }
                                // Ensure props vector is large enough
                                if (!name_to_id.empty()) {
                                    props.resize(std::max_element(
                                                     name_to_id.begin(), name_to_id.end(),
                                                     [](const auto& a, const auto& b) { return a.second < b.second; })
                                                     ->second +
                                                 1);
                                }
                                for (const auto& [pname, expr] : ptr->properties->entries) {
                                    auto pi_it = name_to_id.find(pname);
                                    if (pi_it != name_to_id.end()) {
                                        // Evaluate literal expression
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
                                    // Property not in label def — skip
                                }
                            }
                        }
                        label_props.emplace_back(lid, std::move(props));
                    }
                }

                auto result = std::make_unique<CreateNodePhysicalOp>(
                    ptr->variable, std::move(label_ids), std::move(label_props), store, vid, std::move(child_op));

                Schema output_schema;
                if (!ptr->variable.empty()) {
                    output_schema.push_back(ptr->variable);
                }
                return PlanOperatorResult{std::move(result), std::move(output_schema)};

            } else if constexpr (std::is_same_v<OpType, CreateEdgeOp>) {
                std::unique_ptr<PhysicalOperator> child_op;
                Schema child_schema;
                if (!ptr->children.empty()) {
                    auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                    if (std::holds_alternative<std::string>(child_result)) {
                        return std::get<std::string>(child_result);
                    }
                    auto child_res2 = std::move(std::get<PlanOperatorResult>(child_result));
                    auto cop = std::move(child_res2.op);
                    auto cschema = std::move(child_res2.output_schema);
                    child_op = std::move(cop);
                    child_schema = std::move(cschema);
                }

                VertexId src_id = INVALID_VERTEX_ID;
                VertexId dst_id = INVALID_VERTEX_ID;
                auto src_it = ctx.variable_vertex_ids.find(ptr->src_variable);
                if (src_it != ctx.variable_vertex_ids.end()) {
                    src_id = src_it->second;
                }
                auto dst_it = ctx.variable_vertex_ids.find(ptr->dst_variable);
                if (dst_it != ctx.variable_vertex_ids.end()) {
                    dst_id = dst_it->second;
                }

                if (src_id == INVALID_VERTEX_ID || dst_id == INVALID_VERTEX_ID) {
                    return "Cannot resolve source/destination vertex IDs for edge creation";
                }

                EdgeId eid = ctx.next_edge_id++;

                EdgeLabelId label_id = INVALID_EDGE_LABEL_ID;
                if (!ptr->rel_types.empty()) {
                    auto it = ctx.edge_label_name_to_id.find(ptr->rel_types[0]);
                    if (it != ctx.edge_label_name_to_id.end()) {
                        label_id = it->second;
                    }
                }
                if (label_id == INVALID_EDGE_LABEL_ID) {
                    return "Cannot resolve edge label";
                }

                auto result = std::make_unique<CreateEdgePhysicalOp>(ptr->variable, src_id, dst_id, label_id, eid,
                                                                     store, std::move(child_op));

                Schema output_schema;
                if (!ptr->variable.empty()) {
                    output_schema.push_back(ptr->variable);
                }
                return PlanOperatorResult{std::move(result), std::move(output_schema)};

            } else {
                return "Unknown logical operator type";
            }
        },
        op);
}

std::optional<PlanOperatorResult>
PhysicalPlanner::tryIndexScan(LabelScanOp& scan_op, cypher::BinaryOp& binop,
                               LabelId label_id, const LabelDef& label_def,
                               IAsyncGraphDataStore& store, PlanContext& ctx) {
    // Check pattern: PropertyAccess(Variable, prop) op Literal
    if (!std::holds_alternative<std::unique_ptr<cypher::PropertyAccess>>(binop.left) ||
        !std::holds_alternative<std::unique_ptr<cypher::Literal>>(binop.right)) {
        return std::nullopt;
    }

    auto& pa = std::get<std::unique_ptr<cypher::PropertyAccess>>(binop.left);
    auto& lit = std::get<std::unique_ptr<cypher::Literal>>(binop.right);

    // Find property in label def
    for (const auto& prop_def : label_def.properties) {
        if (prop_def.name != pa->property) {
            continue;
        }

        // Find matching PUBLIC index
        for (const auto& idx : label_def.indexes) {
            if (idx.state != IndexState::PUBLIC || idx.prop_ids.size() != 1 || idx.prop_ids[0] != prop_def.id) {
                continue;
            }

            PropertyValue search_val;
            if (std::holds_alternative<std::string>(lit->value))
                search_val = std::get<std::string>(lit->value);
            else if (std::holds_alternative<int64_t>(lit->value))
                search_val = std::get<int64_t>(lit->value);
            else if (std::holds_alternative<double>(lit->value))
                search_val = std::get<double>(lit->value);
            else if (std::holds_alternative<bool>(lit->value))
                search_val = std::get<bool>(lit->value);

            using ScanMode = IndexScanPhysicalOp::ScanMode;
            std::unique_ptr<IndexScanPhysicalOp> result;

            if (binop.op == cypher::BinaryOperator::EQ) {
                result = std::make_unique<IndexScanPhysicalOp>(
                    scan_op.variable, label_id, prop_def.id,
                    ScanMode::EQUALITY,
                    std::move(search_val), std::nullopt,
                    store, ctx.label_defs);
            } else if (binop.op == cypher::BinaryOperator::GT ||
                       binop.op == cypher::BinaryOperator::GTE) {
                result = std::make_unique<IndexScanPhysicalOp>(
                    scan_op.variable, label_id, prop_def.id,
                    ScanMode::RANGE,
                    std::move(search_val), std::nullopt,
                    store, ctx.label_defs);
            } else if (binop.op == cypher::BinaryOperator::LT ||
                       binop.op == cypher::BinaryOperator::LTE) {
                result = std::make_unique<IndexScanPhysicalOp>(
                    scan_op.variable, label_id, prop_def.id,
                    ScanMode::RANGE,
                    PropertyValue{std::monostate{}},
                    std::move(search_val),
                    store, ctx.label_defs);
            } else {
                return std::nullopt;
            }

            Schema output_schema;
            if (!scan_op.variable.empty()) {
                output_schema.push_back(scan_op.variable);
            }
            return PlanOperatorResult{std::move(result), std::move(output_schema)};
        }
    }
    return std::nullopt;
}

} // namespace compute
} // namespace eugraph
