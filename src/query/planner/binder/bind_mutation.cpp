#include "query/planner/binder.hpp"

#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"

namespace eugraph {
namespace binder {

namespace {

/// Bind inline properties for a multi-label CREATE node.
/// Returns (label_properties, pending_props). Sets error_out on ambiguous property names.
std::pair<std::vector<std::pair<LabelId, std::vector<std::pair<uint16_t, BoundExpression>>>>,
          std::vector<std::pair<std::string, BoundExpression>>>
bindCreateNodeProperties(const cypher::NodePattern& node, const std::vector<LabelId>& label_ids,
                         const catalog::Catalog& catalog, Binder& binder, std::string* error_out) {
    std::vector<std::pair<LabelId, std::vector<std::pair<uint16_t, BoundExpression>>>> label_properties;
    std::vector<std::pair<std::string, BoundExpression>> pending_props;

    // Initialize per-label property vectors for each label
    for (auto lid : label_ids) {
        label_properties.emplace_back(lid, std::vector<std::pair<uint16_t, BoundExpression>>{});
    }

    if (!node.properties)
        return {std::move(label_properties), std::move(pending_props)};

    for (const auto& [prop_name, prop_expr] : node.properties->entries) {
        // Count how many specified labels define this property (without binding yet).
        LabelId matched_lid = INVALID_LABEL_ID;
        int match_count = 0;
        for (const auto& [lid, props_vec] : label_properties) {
            if (catalog.lookupProperty(lid, prop_name)) {
                if (match_count == 0)
                    matched_lid = lid;
                match_count++;
            }
        }

        if (match_count > 1) {
            if (error_out && error_out->empty())
                *error_out = "Ambiguous property '" + prop_name + "' found in multiple labels. Use ::Label to specify.";
            continue;
        }

        auto bound_val = binder.bindExpression(prop_expr);
        if (!bound_val)
            continue;

        if (match_count == 1) {
            for (auto& [lid, props_vec] : label_properties) {
                if (lid == matched_lid) {
                    auto* pd = catalog.lookupProperty(lid, prop_name);
                    props_vec.emplace_back(pd->id, std::move(*bound_val));
                    break;
                }
            }
        } else {
            pending_props.emplace_back(prop_name, std::move(*bound_val));
        }
    }

    return {std::move(label_properties), std::move(pending_props)};
}

} // namespace

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
        std::vector<LabelId> start_labels;
        std::vector<uint16_t> start_prop_ids;
        bool start_exists = element.node.variable.has_value() && ctx_.lookup(*element.node.variable) != nullptr;
        if (!bindNodePattern(element.node, start_var, start_col, start_labels, start_prop_ids, start_exists))
            return std::nullopt;

        if (start_exists) {
            // Variable already bound (e.g. from MATCH) — don't create a new node,
            // just pass through the existing child data.
            if (pi == 0 && child) {
                current = std::move(*child);
            }
            // If pi > 0, current is already set from previous pattern
        } else {
            auto create_node = std::make_unique<BoundCreateNodeOp>();
            create_node->variable = start_var;
            if (start_labels.empty()) {
                auto anon_id = catalog_.getAnonLabelId();
                if (anon_id != INVALID_LABEL_ID)
                    create_node->label_ids.push_back(anon_id);
            } else {
                create_node->label_ids = start_labels;
            }

            std::string prop_error;
            auto [label_props, pending] =
                bindCreateNodeProperties(element.node, create_node->label_ids, catalog_, *this, &prop_error);
            if (!prop_error.empty()) {
                error(prop_error);
                return std::nullopt;
            }
            create_node->label_properties = std::move(label_props);
            create_node->pending_props = std::move(pending);

            if (pi == 0 && child) {
                create_node->child = std::move(*child);
            } else if (pi > 0 && current) {
                create_node->child = std::move(*current);
            }
            current = std::move(create_node);
        }

        // Mark as CREATE variable so property resolution uses source_labels
        auto* sym = ctx_.lookup(start_var);
        if (sym)
            ctx_.symbols[start_var].is_create_variable = true;
        for (const auto& [rel_pat, node_pat] : element.chain) {
            if (!current)
                break;

            // Bind target node FIRST so column indices match physical plan order:
            // (start_node, dst_node, edge) — the edge column is always appended last.
            std::string dst_var;
            uint32_t dst_col;
            std::vector<LabelId> dst_labels;
            std::vector<uint16_t> dst_prop_ids;
            bool dst_exists = node_pat.variable.has_value() && ctx_.lookup(*node_pat.variable) != nullptr;
            if (!bindNodePattern(node_pat, dst_var, dst_col, dst_labels, dst_prop_ids, dst_exists))
                return std::nullopt;

            // Bind relationship AFTER dst node (CREATE context: allow unknown edge types)
            std::string edge_var;
            uint32_t edge_col;
            std::vector<EdgeLabelId> edge_label_ids;
            std::vector<uint16_t> edge_prop_ids;
            if (!bindRelationshipPattern(rel_pat, edge_var, edge_col, edge_label_ids, edge_prop_ids))
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
            } else if (dst_exists) {
                // Variable already bound — don't create a new node.
                create_edge->child = std::move(*current);
            } else {
                // Create target node
                auto create_dst = std::make_unique<BoundCreateNodeOp>();
                create_dst->variable = dst_var;
                if (dst_labels.empty()) {
                    auto anon_id = catalog_.getAnonLabelId();
                    if (anon_id != INVALID_LABEL_ID)
                        create_dst->label_ids.push_back(anon_id);
                } else {
                    create_dst->label_ids = dst_labels;
                }

                std::string dst_prop_error;
                auto [dst_label_props, dst_pending] =
                    bindCreateNodeProperties(node_pat, create_dst->label_ids, catalog_, *this, &dst_prop_error);
                if (!dst_prop_error.empty()) {
                    error(dst_prop_error);
                    return std::nullopt;
                }
                create_dst->label_properties = std::move(dst_label_props);
                create_dst->pending_props = std::move(dst_pending);

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
                // Strong mode (::Label): validate at compile time
                LabelId lid = catalog_.labelNameToId(*label_name);
                if (lid == INVALID_LABEL_ID) {
                    error("Label '" + *label_name + "' not found.");
                    continue;
                }
                auto* pd = catalog_.lookupProperty(lid, prop_name);
                if (!pd) {
                    error("Property '" + prop_name + "' does not exist in label '" + *label_name +
                          "'. Use DDL to add the property, or use " + var_name + "." + prop_name +
                          " (without ::) to write to the weak mode space.");
                    continue;
                }
                bound_item.prop_id = pd->id;
                bound_item.label_id = lid;
                bound_item.strong_mode = true;
            } else {
                // Convenience mode: runtime inference, don't resolve at bind time
                bound_item.strong_mode = false;
            }
            bound_item.prop_name = prop_name;

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
        case cypher::SetItemKind::SET_PROPERTIES: {
            bound_item.kind = BoundSetOp::ItemKind::SET_PROPERTIES;
            bound_item.is_add_assign = item.is_add_assign;
            // Extract variable name from target
            std::visit(
                [&](const auto& ptr) {
                    using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                    if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                        bound_item.target_variable = ptr->name;
                    }
                },
                item.target);
            if (bound_item.target_variable.empty()) {
                error("Cannot resolve SET target variable");
                continue;
            }
            // Bind the map expression
            if (item.value) {
                auto bound_val = bindExpression(*item.value);
                if (bound_val)
                    bound_item.value_expr = std::move(*bound_val);
            }
            break;
        }
        case cypher::SetItemKind::SET_DYNAMIC_PROPERTY:
            error("SET dynamic property not yet supported in binder");
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
            bound_item.prop_name = item.name;
            LabelId lid = catalog_.labelNameToId(item.name);
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
        } else {
            bound_item.kind = BoundRemoveOp::ItemKind::REMOVE_PROPERTY;
            bound_item.prop_name = item.name;
            // Handle PropertyAccess(Variable, prop) or PropertyAccess(LabelCastExpr(Variable, label), prop)
            std::visit(
                [&](const auto& ptr) {
                    using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                    if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                        // Extract variable name from object (Variable or LabelCastExpr)
                        std::visit(
                            [&](const auto& obj) {
                                using ObjElem = typename std::decay_t<decltype(obj)>::element_type;
                                if constexpr (std::is_same_v<ObjElem, cypher::Variable>) {
                                    bound_item.target_variable = obj->name;
                                    bound_item.strong_mode = false;
                                } else if constexpr (std::is_same_v<ObjElem, cypher::LabelCastExpr>) {
                                    if (auto* inner_var =
                                            std::get_if<std::unique_ptr<cypher::Variable>>(&obj->object)) {
                                        bound_item.target_variable = (*inner_var)->name;
                                    }
                                    // Strong mode: resolve label_id and prop_id
                                    LabelId lid = catalog_.labelNameToId(obj->label);
                                    if (lid != INVALID_LABEL_ID) {
                                        auto* pd = catalog_.lookupProperty(lid, item.name);
                                        if (pd) {
                                            bound_item.prop_id = pd->id;
                                            bound_item.label_id = lid;
                                            bound_item.strong_mode = true;
                                        } else {
                                            error("Property '" + item.name + "' does not exist in label '" +
                                                  obj->label + "'.");
                                        }
                                    } else {
                                        error("Label '" + obj->label + "' not found.");
                                    }
                                }
                            },
                            ptr->object);
                    } else if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                        bound_item.target_variable = ptr->name;
                    }
                },
                item.target);
        }
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
            error("InvalidDelete: DELETE requires variable references, not expressions");
            continue;
        }

        // Validate the variable exists and determine its type
        auto* col = ctx_.lookup(var_name);
        if (!col) {
            error("UndefinedVariable: variable '" + var_name + "' not defined");
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
