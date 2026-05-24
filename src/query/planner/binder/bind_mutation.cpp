#include "query/planner/binder.hpp"

#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"

namespace eugraph {
namespace binder {

// ==================== CREATE Binding ====================

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

// ==================== SET Binding ====================

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

// ==================== REMOVE Binding ====================

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

// ==================== DELETE Binding ====================

std::optional<BoundLogicalOperator> Binder::bindDelete(const cypher::DeleteClause& del, BoundLogicalOperator child) {
    auto del_op = std::make_unique<BoundDeleteOp>();
    del_op->detach = del.detach;

    for (const auto& expr : del.expressions) {
        BoundDeleteOp::DeleteTarget target;

        // Resolve expression to variable: only simple variables are valid DELETE targets
        std::string var_name;
        std::visit(
            [&](const auto& ptr) {
                using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                    var_name = ptr->name;
                }
            },
            expr);

        if (var_name.empty()) {
            error("DELETE requires variable references, not expressions");
            continue;
        }

        // Validate the variable exists and determine its type
        auto* col = ctx_.lookup(var_name);
        if (!col) {
            error("DELETE: variable '" + var_name + "' not defined");
            continue;
        }

        switch (col->type.kind) {
        case BoundTypeKind::VERTEX:
            target.kind = BoundDeleteOp::TargetKind::VERTEX;
            break;
        case BoundTypeKind::EDGE:
            target.kind = BoundDeleteOp::TargetKind::EDGE;
            break;
        default:
            error("DELETE: variable '" + var_name + "' is not a vertex or edge");
            continue;
        }

        target.variable_name = var_name;
        del_op->targets.push_back(std::move(target));
    }

    if (del_op->targets.empty()) {
        return std::nullopt;
    }

    del_op->child = std::move(child);
    return del_op;
}

// ==================== UNWIND Binding ====================

std::optional<BoundLogicalOperator> Binder::bindUnwind(const cypher::UnwindClause& unwind,
                                                       std::optional<BoundLogicalOperator> child) {
    // Bind the list expression
    auto bound_list = bindExpression(unwind.list_expr);
    if (!bound_list)
        return std::nullopt;

    // Create singleton source if no preceding clause
    if (!child) {
        child = BoundSingletonOp{};
    }

    // Determine element type from list expression type
    const BoundType& list_type = getBoundExprType(*bound_list);
    BoundType elem_type = (list_type.kind == BoundTypeKind::LIST && list_type.element_type)
                              ? BoundType::clone(*list_type.element_type)
                              : BoundType::Any();

    // Register the UNWIND variable in BindContext
    uint32_t var_col = nextColumnIndex();
    ctx_.symbols[unwind.variable] = makeColumnInfo(unwind.variable, BoundType::clone(elem_type));

    auto unwind_op = std::make_unique<BoundUnwindOp>();
    unwind_op->list_expr = std::move(*bound_list);
    unwind_op->variable = unwind.variable;
    unwind_op->variable_column_index = var_col;
    unwind_op->child = std::move(*child);
    return unwind_op;
}

} // namespace binder
} // namespace eugraph
