#include "query/planner/binder.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"

namespace eugraph {
namespace binder {

namespace {

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
    for (const auto& [name, expr] : props->entries) {
        if (containsParameter(expr))
            return true;
    }
    return false;
}

} // namespace

namespace {

bool bindMergeSetItem(const cypher::SetItem& item, BoundSetOp::SetItem& bound_item, const catalog::Catalog& catalog,
                      Binder& binder, std::string* error_out) {
    switch (item.kind) {
    case cypher::SetItemKind::SET_PROPERTY: {
        std::string var_name;
        std::string prop_name;
        std::optional<std::string> label_name;

        std::visit(
            [&](const auto& ptr) {
                using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
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
            if (error_out && error_out->empty())
                *error_out = "Cannot resolve SET target variable";
            return false;
        }

        if (!binder.lookupVariable(var_name)) {
            if (error_out && error_out->empty())
                *error_out = "UndefinedVariable: " + var_name;
            return false;
        }

        bound_item.kind = BoundSetOp::ItemKind::SET_PROPERTY;
        bound_item.target_variable = var_name;
        bound_item.prop_name = prop_name;

        if (label_name) {
            LabelId lid = catalog.labelNameToId(*label_name);
            if (lid == INVALID_LABEL_ID) {
                if (error_out && error_out->empty())
                    *error_out = "Label '" + *label_name + "' not found.";
                return false;
            }
            auto* pd = catalog.lookupProperty(lid, prop_name);
            if (!pd) {
                if (error_out && error_out->empty())
                    *error_out = "Property '" + prop_name + "' does not exist in label '" + *label_name + "'.";
                return false;
            }
            bound_item.prop_id = pd->id;
            bound_item.label_id = lid;
            bound_item.strong_mode = true;
        } else {
            bound_item.strong_mode = false;
        }

        if (item.value) {
            auto bound_val = binder.bindExpression(*item.value);
            if (bound_val)
                bound_item.value_expr = std::move(*bound_val);
        }
        break;
    }
    case cypher::SetItemKind::SET_LABELS: {
        bound_item.kind = BoundSetOp::ItemKind::SET_LABELS;
        LabelId lid = catalog.labelNameToId(item.label);
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
        if (!bound_item.target_variable.empty() && !binder.lookupVariable(bound_item.target_variable)) {
            if (error_out && error_out->empty())
                *error_out = "UndefinedVariable: " + bound_item.target_variable;
            return false;
        }
        break;
    }
    case cypher::SetItemKind::SET_PROPERTIES: {
        bound_item.kind = BoundSetOp::ItemKind::SET_PROPERTIES;
        bound_item.is_add_assign = item.is_add_assign;
        std::visit(
            [&](const auto& ptr) {
                using Elem = typename std::decay_t<decltype(ptr)>::element_type;
                if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                    bound_item.target_variable = ptr->name;
                }
            },
            item.target);
        if (bound_item.target_variable.empty()) {
            if (error_out && error_out->empty())
                *error_out = "Cannot resolve SET target variable";
            return false;
        }
        if (!binder.lookupVariable(bound_item.target_variable)) {
            if (error_out && error_out->empty())
                *error_out = "UndefinedVariable: " + bound_item.target_variable;
            return false;
        }
        if (item.value) {
            auto bound_val = binder.bindExpression(*item.value);
            if (bound_val)
                bound_item.value_expr = std::move(*bound_val);
        }
        break;
    }
    case cypher::SetItemKind::SET_DYNAMIC_PROPERTY:
        if (error_out && error_out->empty())
            *error_out = "SET dynamic property not yet supported";
        return false;
    }
    return true;
}

} // namespace

std::optional<BoundLogicalOperator> Binder::bindMerge(const cypher::MergeClause& merge,
                                                      std::optional<BoundLogicalOperator> child) {
    const auto& pp = merge.pattern;
    const auto& element = pp.element;

    // Create singleton source if no preceding clause
    if (!child) {
        child = BoundSingletonOp{};
    }

    // ── Validate pattern ──

    // Validate: no parameter use in MERGE node predicates
    if (propertiesContainParameter(element.node.properties)) {
        error("InvalidParameterUse: MERGE does not support parameter as node predicate");
        return std::nullopt;
    }
    for (const auto& [rel_pat, node_pat] : element.chain) {
        if (rel_pat.range.has_value()) {
            error("CreatingVarLength: MERGE does not support variable-length relationships");
            return std::nullopt;
        }
        // VariableAlreadyBound for relationship takes precedence over NoSingleRelationshipType
        if (rel_pat.variable.has_value() && ctx_.lookup(*rel_pat.variable)) {
            error("VariableAlreadyBound: variable '" + *rel_pat.variable + "' is already defined in this scope");
            return std::nullopt;
        }
        if (rel_pat.rel_types.size() != 1) {
            error("NoSingleRelationshipType: MERGE requires exactly one relationship type");
            return std::nullopt;
        }
        if (propertiesContainParameter(rel_pat.properties)) {
            error("InvalidParameterUse: MERGE does not support parameter as relationship predicate");
            return std::nullopt;
        }
        if (propertiesContainParameter(node_pat.properties)) {
            error("InvalidParameterUse: MERGE does not support parameter as node predicate");
            return std::nullopt;
        }
        // VariableAlreadyBound for end node with new predicates (labels/properties)
        if (node_pat.variable && ctx_.lookup(*node_pat.variable)) {
            if (!node_pat.labels.empty() || node_pat.properties.has_value()) {
                error("VariableAlreadyBound: variable '" + *node_pat.variable + "' is already defined in this scope");
                return std::nullopt;
            }
        }
    }

    // ── Bind start node ──
    std::string start_var;
    uint32_t start_col;
    std::vector<LabelId> start_labels;
    std::vector<uint16_t> start_prop_ids;
    bool start_pre_bound = false;

    if (element.node.variable && ctx_.lookup(*element.node.variable)) {
        if (element.chain.empty()) {
            error("VariableAlreadyBound: variable '" + *element.node.variable + "' is already defined in this scope");
            return std::nullopt;
        }
        start_pre_bound = true;
        auto* col = ctx_.lookup(*element.node.variable);
        start_col = col->column_index;
        start_var = *element.node.variable;
        start_labels = col->source_labels;
    } else {
        if (!bindNodePattern(element.node, start_var, start_col, start_labels, start_prop_ids))
            return std::nullopt;
    }

    // ── Bind property filters for start node ──
    std::vector<std::pair<uint16_t, BoundExpression>> start_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> start_pending_props;

    if (element.node.properties) {
        for (const auto& [prop_name, prop_expr] : element.node.properties->entries) {
            auto bound_val = bindExpression(prop_expr);
            if (!bound_val)
                continue;

            bool resolved = false;
            for (auto lid : start_labels) {
                if (catalog_.lookupProperty(lid, prop_name)) {
                    auto* pd = catalog_.lookupProperty(lid, prop_name);
                    start_prop_filters.emplace_back(pd->id, std::move(*bound_val));
                    resolved = true;
                    break;
                }
            }
            if (!resolved) {
                start_pending_props.emplace_back(prop_name, std::move(*bound_val));
            }
        }
    }

    // ── Bind relationship and end node ──
    bool has_relationship = !element.chain.empty();
    std::string edge_var;
    std::optional<EdgeLabelId> edge_label_id;
    std::optional<std::string> edge_label_name;
    cypher::RelationshipDirection direction = cypher::RelationshipDirection::LEFT_TO_RIGHT;
    std::vector<std::pair<uint16_t, BoundExpression>> edge_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> edge_pending_props;
    std::string end_var;
    bool end_pre_bound = false;
    std::vector<LabelId> end_labels;
    std::vector<std::pair<uint16_t, BoundExpression>> end_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> end_pending_props;

    if (has_relationship) {
        const auto& [rel_pat, node_pat] = element.chain[0];

        // Bind relationship
        uint32_t edge_col;
        std::vector<EdgeLabelId> edge_label_ids;
        std::vector<uint16_t> edge_prop_ids;
        if (!bindRelationshipPattern(rel_pat, edge_var, edge_col, edge_label_ids, edge_prop_ids))
            return std::nullopt;

        direction = rel_pat.direction;

        if (!edge_label_ids.empty()) {
            edge_label_id = edge_label_ids[0];
        }
        if (!rel_pat.rel_types.empty() && !edge_label_id.has_value()) {
            edge_label_name = rel_pat.rel_types[0];
        }

        // Bind edge property filters
        if (rel_pat.properties) {
            for (const auto& [prop_name, prop_expr] : rel_pat.properties->entries) {
                auto bound_val = bindExpression(prop_expr);
                if (!bound_val)
                    continue;
                if (edge_label_id.has_value()) {
                    auto* pd = catalog_.lookupEdgeLabelProperty(*edge_label_id, prop_name);
                    if (pd) {
                        edge_prop_filters.emplace_back(pd->id, std::move(*bound_val));
                        continue;
                    }
                }
                edge_pending_props.emplace_back(prop_name, std::move(*bound_val));
            }
        }

        // Bind end node
        uint32_t end_col;
        std::vector<uint16_t> end_prop_ids_ref;
        if (node_pat.variable && ctx_.lookup(*node_pat.variable)) {
            end_pre_bound = true;
            auto* col = ctx_.lookup(*node_pat.variable);
            end_col = col->column_index;
            end_var = *node_pat.variable;
            end_labels = col->source_labels;
        } else {
            if (!bindNodePattern(node_pat, end_var, end_col, end_labels, end_prop_ids_ref))
                return std::nullopt;
        }

        // Bind end node property filters
        if (node_pat.properties) {
            for (const auto& [prop_name, prop_expr] : node_pat.properties->entries) {
                auto bound_val = bindExpression(prop_expr);
                if (!bound_val)
                    continue;
                bool resolved = false;
                for (auto lid : end_labels) {
                    if (catalog_.lookupProperty(lid, prop_name)) {
                        auto* pd = catalog_.lookupProperty(lid, prop_name);
                        end_prop_filters.emplace_back(pd->id, std::move(*bound_val));
                        resolved = true;
                        break;
                    }
                }
                if (!resolved) {
                    end_pending_props.emplace_back(prop_name, std::move(*bound_val));
                }
            }
        }
    }

    // ── Bind ON CREATE/MATCH SET items ──
    std::vector<BoundSetOp::SetItem> on_create_items;
    std::vector<BoundSetOp::SetItem> on_match_items;
    size_t error_count_before = errors_.size();

    for (const auto& si : merge.on_create) {
        BoundSetOp::SetItem bound_item;
        std::string err;
        if (bindMergeSetItem(si, bound_item, catalog_, *this, &err))
            on_create_items.push_back(std::move(bound_item));
        else if (!err.empty())
            error(err);
    }
    for (const auto& si : merge.on_match) {
        BoundSetOp::SetItem bound_item;
        std::string err;
        if (bindMergeSetItem(si, bound_item, catalog_, *this, &err))
            on_match_items.push_back(std::move(bound_item));
        else if (!err.empty())
            error(err);
    }

    // Check if SET item binding produced any errors (e.g., undefined variables)
    if (errors_.size() > error_count_before)
        return std::nullopt;

    // ── Construct BoundMergeOp ──
    auto merge_op = std::make_unique<BoundMergeOp>();
    merge_op->path_variable = pp.variable;
    merge_op->start_var = start_var;
    merge_op->start_pre_bound = start_pre_bound;
    merge_op->start_labels = std::move(start_labels);
    merge_op->start_prop_filters = std::move(start_prop_filters);
    merge_op->start_pending_props = std::move(start_pending_props);
    merge_op->has_relationship = has_relationship;
    merge_op->edge_var = edge_var;
    merge_op->edge_label_id = edge_label_id;
    merge_op->edge_label_name = edge_label_name;
    merge_op->direction = direction;
    merge_op->edge_prop_filters = std::move(edge_prop_filters);
    merge_op->edge_pending_props = std::move(edge_pending_props);
    merge_op->end_var = end_var;
    merge_op->end_pre_bound = end_pre_bound;
    merge_op->end_labels = std::move(end_labels);
    merge_op->end_prop_filters = std::move(end_prop_filters);
    merge_op->end_pending_props = std::move(end_pending_props);
    merge_op->on_create_items = std::move(on_create_items);
    merge_op->on_match_items = std::move(on_match_items);
    merge_op->child = std::move(*child);

    return merge_op;
}

} // namespace binder
} // namespace eugraph
