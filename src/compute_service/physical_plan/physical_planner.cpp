#include "compute_service/physical_plan/physical_planner.hpp"

#include <stdexcept>

namespace eugraph {
namespace compute {

std::variant<std::unique_ptr<PhysicalOperator>, std::string>
PhysicalPlanner::plan(LogicalPlan& logical_plan, AsyncGraphStore& store, PlanContext& ctx) {
    Schema empty_schema;
    return planOperator(logical_plan.root, store, ctx, empty_schema);
}

std::variant<std::unique_ptr<PhysicalOperator>, std::string>
PhysicalPlanner::planOperator(LogicalOperator& op, AsyncGraphStore& store, PlanContext& ctx,
                               Schema input_schema) {
    return std::visit(
        [this, &store, &ctx, &input_schema](auto& ptr)
            -> std::variant<std::unique_ptr<PhysicalOperator>, std::string> {
            using T = std::decay_t<decltype(ptr)>;
            using OpType = typename T::element_type;

            if constexpr (std::is_same_v<OpType, AllNodeScanOp>) {
                auto result = std::make_unique<AllNodeScanPhysicalOp>(ptr->variable, store, ctx.label_name_to_id);
                return std::move(result);

            } else if constexpr (std::is_same_v<OpType, LabelScanOp>) {
                auto it = ctx.label_name_to_id.find(ptr->label);
                if (it == ctx.label_name_to_id.end()) {
                    return "Unknown label: " + ptr->label;
                }
                auto result = std::make_unique<LabelScanPhysicalOp>(ptr->variable, it->second, store);
                return std::move(result);

            } else if constexpr (std::is_same_v<OpType, ExpandOp>) {
                if (ptr->children.size() != 1) {
                    return "Expand requires exactly one child";
                }
                auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                if (std::holds_alternative<std::string>(child_result)) {
                    return std::get<std::string>(child_result);
                }
                auto child_op = std::move(std::get<std::unique_ptr<PhysicalOperator>>(child_result));

                std::optional<EdgeLabelId> label_filter;
                if (!ptr->rel_types.empty()) {
                    auto it = ctx.edge_label_name_to_id.find(ptr->rel_types[0]);
                    if (it != ctx.edge_label_name_to_id.end()) {
                        label_filter = it->second;
                    }
                }

                auto result = std::make_unique<ExpandPhysicalOp>(
                    ptr->src_variable, ptr->dst_variable, ptr->edge_variable,
                    label_filter, ptr->direction, store, std::move(child_op));
                return std::move(result);

            } else if constexpr (std::is_same_v<OpType, FilterOp>) {
                if (ptr->children.size() != 1) {
                    return "Filter requires exactly one child";
                }
                auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                if (std::holds_alternative<std::string>(child_result)) {
                    return std::get<std::string>(child_result);
                }
                auto child_op = std::move(std::get<std::unique_ptr<PhysicalOperator>>(child_result));

                auto result = std::make_unique<FilterPhysicalOp>(
                    std::move(ptr->predicate),
                    Schema(input_schema),
                    std::move(child_op));
                return std::move(result);

            } else if constexpr (std::is_same_v<OpType, ProjectOp>) {
                if (ptr->children.size() != 1) {
                    return "Project requires exactly one child";
                }
                auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                if (std::holds_alternative<std::string>(child_result)) {
                    return std::get<std::string>(child_result);
                }
                auto child_op = std::move(std::get<std::unique_ptr<PhysicalOperator>>(child_result));

                std::vector<ProjectPhysicalOp::ProjectItem> items;
                for (auto& item : ptr->items) {
                    ProjectPhysicalOp::ProjectItem pi;
                    pi.expr = std::move(item.expr);
                    pi.name = item.alias.value_or("");
                    items.push_back(std::move(pi));
                }

                auto result = std::make_unique<ProjectPhysicalOp>(
                    std::move(items), Schema(input_schema), std::move(child_op));
                return std::move(result);

            } else if constexpr (std::is_same_v<OpType, LimitOp>) {
                if (ptr->children.size() != 1) {
                    return "Limit requires exactly one child";
                }
                auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                if (std::holds_alternative<std::string>(child_result)) {
                    return std::get<std::string>(child_result);
                }
                auto child_op = std::move(std::get<std::unique_ptr<PhysicalOperator>>(child_result));

                auto result = std::make_unique<LimitPhysicalOp>(ptr->limit, std::move(child_op));
                return std::move(result);

            } else if constexpr (std::is_same_v<OpType, CreateNodeOp>) {
                std::unique_ptr<PhysicalOperator> child_op;
                if (!ptr->children.empty()) {
                    auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                    if (std::holds_alternative<std::string>(child_result)) {
                        return std::get<std::string>(child_result);
                    }
                    child_op = std::move(std::get<std::unique_ptr<PhysicalOperator>>(child_result));
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
                        label_ids.push_back(it->second);
                        label_props.emplace_back(it->second, Properties{});
                    }
                }

                auto result = std::make_unique<CreateNodePhysicalOp>(
                    ptr->variable, std::move(label_ids), std::move(label_props),
                    store, vid, std::move(child_op));
                return std::move(result);

            } else if constexpr (std::is_same_v<OpType, CreateEdgeOp>) {
                std::unique_ptr<PhysicalOperator> child_op;
                if (!ptr->children.empty()) {
                    auto child_result = planOperator(ptr->children[0], store, ctx, input_schema);
                    if (std::holds_alternative<std::string>(child_result)) {
                        return std::get<std::string>(child_result);
                    }
                    child_op = std::move(std::get<std::unique_ptr<PhysicalOperator>>(child_result));
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

                auto result = std::make_unique<CreateEdgePhysicalOp>(
                    ptr->variable, src_id, dst_id, label_id, eid, store, std::move(child_op));
                return std::move(result);

            } else {
                return "Unknown logical operator type";
            }
        },
        op);
}

} // namespace compute
} // namespace eugraph
