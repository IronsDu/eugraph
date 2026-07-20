#include "query/planner/binder.hpp"

#include <unordered_set>

#include "common/types/temporal_value.hpp"
#include "query/function/batch_ops.hpp"
#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "query/planner/bound_expression/bound_map.hpp"
#include "query/planner/bound_expression/bound_slice.hpp"
#include "query/planner/bound_expression/bound_subscript.hpp"

namespace eugraph {
namespace binder {

namespace {
bool isTemporalType(BoundTypeKind k) {
    return k == BoundTypeKind::DATETIME || k == BoundTypeKind::TIME || k == BoundTypeKind::DURATION;
}

bool isAggregateFunctionName(const std::string& name) {
    return name == "count" || name == "sum" || name == "avg" || name == "min" || name == "max" || name == "collect" ||
           name == "percentile_cont" || name == "percentile_disc" || name == "st_dev" || name == "st_dev_p";
}

// Detect aggregate function calls inside a Cypher expression subtree.
// Used to reject aggregates in contexts where row-wise evaluation is required
// (e.g. inside a list comprehension projection / WHERE filter).
bool containsAggregateCall(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                if (isAggregateFunctionName(ptr->name))
                    return true;
                for (const auto& arg : ptr->args)
                    if (containsAggregateCall(arg))
                        return true;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return containsAggregateCall(ptr->left) || containsAggregateCall(ptr->right);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return containsAggregateCall(ptr->operand);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                return containsAggregateCall(ptr->object);
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries)
                    if (containsAggregateCall(v))
                        return true;
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& e : ptr->elements)
                    if (containsAggregateCall(e))
                        return true;
            } else if constexpr (std::is_same_v<Elem, cypher::AllExpr> || std::is_same_v<Elem, cypher::AnyExpr> ||
                                 std::is_same_v<Elem, cypher::NoneExpr> || std::is_same_v<Elem, cypher::SingleExpr>) {
                if (containsAggregateCall(ptr->list_expr))
                    return true;
                if (ptr->where_pred && containsAggregateCall(*ptr->where_pred))
                    return true;
            } else if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                if (containsAggregateCall(ptr->list_expr))
                    return true;
                if (ptr->where_pred && containsAggregateCall(*ptr->where_pred))
                    return true;
                if (ptr->projection && containsAggregateCall(*ptr->projection))
                    return true;
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                if (ptr->subject && containsAggregateCall(*ptr->subject))
                    return true;
                for (const auto& [w, t] : ptr->when_thens)
                    if (containsAggregateCall(w) || containsAggregateCall(t))
                        return true;
                if (ptr->else_expr && containsAggregateCall(*ptr->else_expr))
                    return true;
            } else if constexpr (std::is_same_v<Elem, cypher::SubscriptExpr>) {
                if (containsAggregateCall(ptr->list) || containsAggregateCall(ptr->index))
                    return true;
            } else if constexpr (std::is_same_v<Elem, cypher::SliceExpr>) {
                if (containsAggregateCall(ptr->list))
                    return true;
                if (ptr->from && containsAggregateCall(*ptr->from))
                    return true;
                if (ptr->to && containsAggregateCall(*ptr->to))
                    return true;
            }
            return false;
        },
        expr);
}
} // namespace

// ==================== Expression Binding ====================

std::optional<BoundExpression> Binder::bindExpression(const cypher::Expression& expr) {
    return std::visit(
        [this](const auto& ptr) -> std::optional<BoundExpression> {
            using T = std::decay_t<decltype(ptr)>;
            using Elem = typename T::element_type;

            if constexpr (std::is_same_v<Elem, cypher::Literal>) {
                // Convert AST literal to bound literal
                return BoundExpression(std::visit(
                    [](const auto& v) -> BoundExpression {
                        using V = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<V, cypher::NullValue>) {
                            return BoundLiteral{};
                        } else if constexpr (std::is_same_v<V, bool>) {
                            return BoundLiteral(v);
                        } else if constexpr (std::is_same_v<V, int64_t>) {
                            return BoundLiteral(v);
                        } else if constexpr (std::is_same_v<V, double>) {
                            return BoundLiteral(v);
                        } else if constexpr (std::is_same_v<V, std::string>) {
                            return BoundLiteral(std::string(v));
                        }
                        return BoundLiteral{};
                    },
                    ptr->value));
            } else if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                auto* col = ctx_.lookup(ptr->name);
                if (!col) {
                    error("UndefinedVariable: Variable '" + ptr->name + "' not defined");
                    return std::nullopt;
                }
                return BoundExpression(BoundColumnRef(col->column_index, col->type, ptr->name, col->slot_id));
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                return bindExpression(ptr->inner);
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                auto left = bindExpression(ptr->left);
                auto right = bindExpression(ptr->right);
                if (!left || !right)
                    return std::nullopt;

                const BoundType& left_type = getBoundExprType(*left);
                const BoundType& right_type = getBoundExprType(*right);

                std::string err_msg;
                BoundType result_type = inferBinaryOpType(ptr->op, left_type, right_type, err_msg);
                if (!err_msg.empty()) {
                    error(err_msg);
                    return std::nullopt;
                }

                auto bin = std::make_unique<BoundBinaryOp>();
                bin->op = ptr->op;
                bin->left = std::move(*left);
                bin->right = std::move(*right);
                bin->result_type = std::move(result_type);
                bin->batch_fn = function::resolveBinaryBatchFn(bin->op, left_type.kind, right_type.kind);
                return BoundExpression(std::move(bin));
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                auto operand = bindExpression(ptr->operand);
                if (!operand)
                    return std::nullopt;

                const BoundType& operand_type = getBoundExprType(*operand);
                std::string err_msg;
                BoundType result_type = inferUnaryOpType(ptr->op, operand_type, err_msg);
                if (!err_msg.empty()) {
                    error(err_msg);
                    return std::nullopt;
                }

                auto un = std::make_unique<BoundUnaryOp>();
                un->op = ptr->op;
                un->operand = std::move(*operand);
                un->result_type = std::move(result_type);
                un->batch_fn = function::resolveUnaryBatchFn(un->op, operand_type.kind);
                return BoundExpression(std::move(un));
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                // Look up in function registry
                std::vector<binder::BoundType> arg_types;
                std::vector<BoundExpression> bound_args;
                for (const auto& arg : ptr->args) {
                    auto bound_arg = bindExpression(arg);
                    if (!bound_arg)
                        return std::nullopt;
                    arg_types.push_back(getBoundExprType(*bound_arg));
                    bound_args.push_back(std::move(*bound_arg));
                }

                const function::FunctionDef* func = func_registry_.lookup(ptr->name, arg_types);
                if (!func) {
                    std::string sig = ptr->name + "(";
                    for (size_t i = 0; i < arg_types.size(); ++i) {
                        if (i > 0)
                            sig += ", ";
                        sig += arg_types[i].toString();
                    }
                    sig += ")";
                    if (func_registry_.exists(ptr->name)) {
                        error("SyntaxError: InvalidArgumentType: no overload of '" + ptr->name + "' accepts (" + sig +
                              ")");
                    } else {
                        error("SyntaxError: UnknownFunction: Function not found: " + sig);
                    }
                    return std::nullopt;
                }

                auto fc = std::make_unique<BoundFunctionCall>();
                fc->func_def = func;
                fc->args = std::move(bound_args);
                fc->return_type = BoundType::clone(func->return_type);
                fc->distinct = ptr->distinct;

                // properties(n) / keys(n) need all vertex/edge properties loaded
                if ((func->name == "properties" || func->name == "keys") && fc->args.size() == 1) {
                    const BoundType& arg_type = getBoundExprType(fc->args[0]);
                    std::optional<std::string> var_name;
                    if (std::holds_alternative<BoundColumnRef>(fc->args[0])) {
                        var_name = std::get<BoundColumnRef>(fc->args[0]).name;
                    } else if (std::holds_alternative<BoundVariableRef>(fc->args[0])) {
                        var_name = std::get<BoundVariableRef>(fc->args[0]).name;
                    }
                    if (var_name) {
                        if (arg_type.kind == BoundTypeKind::VERTEX) {
                            for (const auto& [lid, ldef] : catalog_.allLabels()) {
                                for (const auto& pd : ldef.properties) {
                                    ctx_.addPropertyRequirement(*var_name, lid, pd.id);
                                }
                            }
                        } else if (arg_type.kind == BoundTypeKind::EDGE) {
                            for (const auto& [elid, eldef] : catalog_.allEdgeLabels()) {
                                for (const auto& pd : eldef.properties) {
                                    ctx_.addPropertyRequirement(*var_name, elid, pd.id);
                                }
                            }
                        }
                    }
                }

                // collect(vertex_var) / collect(edge_var): ensure all properties are
                // loaded on the source so that subsequent list comprehension or other
                // per-element property access sees them. Without this, the source
                // vertex would only have explicitly-referenced properties loaded,
                // leaving later x.prop accesses inside [x IN collect(v) | x.prop]
                // returning null.
                if (func->name == "collect" && fc->args.size() == 1 && fc->args.size() >= 1 && !ptr->distinct) {
                    const BoundType& arg_type = getBoundExprType(fc->args[0]);
                    std::optional<std::string> var_name;
                    if (std::holds_alternative<BoundColumnRef>(fc->args[0])) {
                        var_name = std::get<BoundColumnRef>(fc->args[0]).name;
                    } else if (std::holds_alternative<BoundVariableRef>(fc->args[0])) {
                        var_name = std::get<BoundVariableRef>(fc->args[0]).name;
                    }
                    if (var_name) {
                        if (arg_type.kind == BoundTypeKind::VERTEX) {
                            for (const auto& [lid, ldef] : catalog_.allLabels()) {
                                for (const auto& pd : ldef.properties) {
                                    ctx_.addPropertyRequirement(*var_name, lid, pd.id);
                                }
                            }
                        } else if (arg_type.kind == BoundTypeKind::EDGE) {
                            for (const auto& [elid, eldef] : catalog_.allEdgeLabels()) {
                                for (const auto& pd : eldef.properties) {
                                    ctx_.addPropertyRequirement(*var_name, elid, pd.id);
                                }
                            }
                        }
                    }
                }

                return BoundExpression(std::move(fc));
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                auto obj = bindExpression(ptr->object);
                if (!obj)
                    return std::nullopt;

                const BoundType& obj_type = getBoundExprType(*obj);

                if (obj_type.kind == BoundTypeKind::VERTEX) {
                    // Check if the object is a LabelCastExpr (strong type)
                    if (std::holds_alternative<std::unique_ptr<BoundLabelCast>>(*obj)) {
                        auto& lc = std::get<std::unique_ptr<BoundLabelCast>>(*obj);
                        auto* pd = catalog_.lookupProperty(lc->label_id, ptr->property);
                        if (!pd) {
                            const auto* ldef = catalog_.lookupLabel(lc->label_id);
                            std::string label_name = ldef ? ldef->name : "?";
                            error("Label '" + label_name + "' has no property '" + ptr->property + "'");
                            return std::nullopt;
                        }
                        auto prop_ref = std::make_unique<BoundPropertyRef>();
                        auto saved_label_id = lc->label_id;
                        // Extract variable name from label cast object before move
                        std::optional<std::string> saved_col_name;
                        if (std::holds_alternative<BoundColumnRef>(lc->object)) {
                            saved_col_name = std::get<BoundColumnRef>(lc->object).name;
                        }
                        prop_ref->object = std::move(*obj);
                        BoundPropertyRef::ResolvedProperty rp;
                        rp.label_id = saved_label_id;
                        rp.prop_id = pd->id;
                        rp.type = propertyTypeToBoundType(pd->type);
                        prop_ref->candidates.push_back(rp);
                        prop_ref->result_type = rp.type;

                        // Add projection requirement
                        if (saved_col_name) {
                            ctx_.addPropertyRequirement(*saved_col_name, saved_label_id, pd->id);
                        }

                        return BoundExpression(std::move(prop_ref));
                    }

                    // Weak type: check if object is a ColumnRef
                    if (std::holds_alternative<BoundColumnRef>(*obj)) {
                        auto& col_ref = std::get<BoundColumnRef>(*obj);

                        // For CREATE variables, avoid picking up wrong candidates from
                        // unrelated labels. Use source_labels if set, otherwise dynamic.
                        auto* col_info = ctx_.lookup(col_ref.name);
                        if (col_info && col_info->is_create_variable) {
                            if (!col_info->source_labels.empty()) {
                                auto prop_ref = std::make_unique<BoundPropertyRef>();
                                BoundType merged = BoundType::Null();
                                for (auto lid : col_info->source_labels) {
                                    auto* pd = catalog_.lookupProperty(lid, ptr->property);
                                    if (pd) {
                                        BoundPropertyRef::ResolvedProperty rp;
                                        rp.label_id = lid;
                                        rp.prop_id = pd->id;
                                        rp.type = propertyTypeToBoundType(pd->type);
                                        merged = BoundType::merge(merged, rp.type);
                                        prop_ref->candidates.push_back(rp);
                                        ctx_.addPropertyRequirement(col_ref.name, lid, pd->id);
                                    }
                                }
                                if (!prop_ref->candidates.empty()) {
                                    prop_ref->object = std::move(*obj);
                                    prop_ref->result_type = merged;
                                    return BoundExpression(std::move(prop_ref));
                                }
                            }
                            // No source_labels or property not yet registered → dynamic
                            auto dyn_ref = std::make_unique<BoundDynamicPropertyRef>();
                            dyn_ref->object = std::move(*obj);
                            dyn_ref->property = ptr->property;
                            dyn_ref->result_type = BoundType::Any();
                            return BoundExpression(std::move(dyn_ref));
                        }

                        // Non-CREATE: scan ALL labels in the catalog
                        LabelIdSet all_labels;
                        for (const auto& [lid, ldef] : catalog_.allLabels()) {
                            all_labels.insert(lid);
                        }

                        auto candidates = catalog_.lookupPropertyAcrossLabels(all_labels, ptr->property);
                        if (candidates.empty()) {
                            // Fallback: runtime property lookup by name
                            auto dyn_ref = std::make_unique<BoundDynamicPropertyRef>();
                            dyn_ref->object = std::move(*obj);
                            dyn_ref->property = ptr->property;
                            dyn_ref->result_type = BoundType::Any();
                            return BoundExpression(std::move(dyn_ref));
                        }

                        auto saved_var_name = col_ref.name;
                        auto prop_ref = std::make_unique<BoundPropertyRef>();
                        prop_ref->object = std::move(*obj);
                        prop_ref->property_name = ptr->property;
                        BoundType merged = BoundType::Null();
                        for (auto& [lid, pd] : candidates) {
                            BoundPropertyRef::ResolvedProperty rp;
                            rp.label_id = lid;
                            rp.prop_id = pd->id;
                            rp.type = propertyTypeToBoundType(pd->type);
                            merged = BoundType::merge(merged, rp.type);
                            prop_ref->candidates.push_back(rp);

                            // Add projection requirement
                            ctx_.addPropertyRequirement(saved_var_name, lid, pd->id);
                        }
                        prop_ref->result_type = merged;

                        return BoundExpression(std::move(prop_ref));
                    }

                    // obj is VERTEX but not a LabelCast / ColumnRef (e.g. function
                    // call result like startNode(r)): use dynamic property ref so
                    // the evaluator resolves the property name at runtime against
                    // the VertexValue's labels. Lazy loading in evalDynamicPropertyRef
                    // will populate bare VertexValues with actual properties.
                    auto dyn_ref = std::make_unique<BoundDynamicPropertyRef>();
                    dyn_ref->object = std::move(*obj);
                    dyn_ref->property = ptr->property;
                    dyn_ref->result_type = BoundType::Any();
                    return BoundExpression(std::move(dyn_ref));
                }

                if (obj_type.kind == BoundTypeKind::EDGE) {
                    auto prop_ref = std::make_unique<BoundPropertyRef>();
                    prop_ref->object = std::move(*obj);
                    prop_ref->property_name = ptr->property;

                    // Structural fields of EdgeValue
                    if (ptr->property == "id" || ptr->property == "src_id" || ptr->property == "dst_id") {
                        prop_ref->result_type = BoundType::Int64();
                        return BoundExpression(std::move(prop_ref));
                    }
                    if (ptr->property == "label_id") {
                        prop_ref->result_type = BoundType::Int64();
                        return BoundExpression(std::move(prop_ref));
                    }

                    // Extract variable name for property requirement pushdown
                    std::optional<std::string> var_name;
                    if (std::holds_alternative<BoundColumnRef>(prop_ref->object))
                        var_name = std::get<BoundColumnRef>(prop_ref->object).name;
                    else if (std::holds_alternative<BoundVariableRef>(prop_ref->object))
                        var_name = std::get<BoundVariableRef>(prop_ref->object).name;

                    // Look up the property across all edge labels
                    BoundType merged = BoundType::Null();
                    for (const auto& [elid, eldef] : catalog_.allEdgeLabels()) {
                        for (const auto& pd : eldef.properties) {
                            if (pd.name == ptr->property) {
                                BoundPropertyRef::ResolvedProperty rp;
                                rp.label_id = static_cast<LabelId>(elid);
                                rp.prop_id = pd.id;
                                rp.type = propertyTypeToBoundType(pd.type);
                                merged = BoundType::merge(merged, rp.type);
                                prop_ref->candidates.push_back(rp);
                                if (var_name)
                                    ctx_.addPropertyRequirement(*var_name, static_cast<LabelId>(elid), pd.id);
                            }
                        }
                    }
                    prop_ref->result_type = prop_ref->candidates.empty() ? BoundType::Any() : merged;
                    return BoundExpression(std::move(prop_ref));
                }

                if (obj_type.kind == BoundTypeKind::MAP) {
                    // Map property access: convert map.key to subscript map["key"]
                    auto sub = std::make_unique<BoundSubscript>();
                    sub->list = std::move(*obj);
                    sub->index = BoundLiteral(std::string(ptr->property));
                    sub->result_type =
                        obj_type.map_value_type ? BoundType::clone(*obj_type.map_value_type) : BoundType::Any();
                    return BoundExpression(std::move(sub));
                }

                // Temporal member access for datetime/time/duration/any types.
                // For ANY: if the property name matches a temporal field, use the
                // typed accessor. If not, fall through to the subscript path below
                // so runtime map access (or null propagation) can handle it.
                if ((obj_type.kind == BoundTypeKind::DATETIME || obj_type.kind == BoundTypeKind::TIME ||
                     obj_type.kind == BoundTypeKind::DURATION) ||
                    obj_type.kind == BoundTypeKind::ANY) {
                    int64_t field_int = -1;
                    bool returns_string = false;

                    // Look up field respecting object type to avoid enum value clashes:
                    // DateTimeField::HOUR (9) ≠ TimeField::HOUR (0), etc.
                    if (obj_type.kind == BoundTypeKind::TIME) {
                        if (auto tf = timeFieldFromString(ptr->property)) {
                            field_int = static_cast<int64_t>(*tf);
                            returns_string = timeFieldReturnsString(*tf);
                        }
                    } else if (obj_type.kind == BoundTypeKind::DURATION) {
                        if (auto df = durationFieldFromString(ptr->property)) {
                            field_int = static_cast<int64_t>(*df);
                        }
                    } else {
                        // DATETIME or ANY: try DateTimeField first, then TimeField, then DurationField
                        if (auto dtf = dateTimeFieldFromString(ptr->property)) {
                            field_int = static_cast<int64_t>(*dtf);
                            returns_string = dateTimeFieldReturnsString(*dtf);
                        } else if (auto tf = timeFieldFromString(ptr->property)) {
                            field_int = static_cast<int64_t>(*tf);
                            returns_string = timeFieldReturnsString(*tf);
                        } else if (auto df = durationFieldFromString(ptr->property)) {
                            field_int = static_cast<int64_t>(*df);
                        }
                    }

                    if (field_int >= 0) {
                        auto* func = func_registry_.lookup("__temporal_field__", {obj_type, BoundType::Int64()});
                        if (!func) {
                            error("Temporal field accessor not registered");
                            return std::nullopt;
                        }
                        auto fc = std::make_unique<BoundFunctionCall>();
                        fc->func_def = func;
                        fc->args.push_back(std::move(*obj));
                        fc->args.push_back(BoundLiteral(field_int));
                        fc->return_type = returns_string ? BoundType::String() : BoundType::Int64();
                        return BoundExpression(std::move(fc));
                    }
                    // For ANY: no temporal field matched, fall through to subscript
                    if (obj_type.kind != BoundTypeKind::ANY) {
                        error("Unknown temporal field: '" + ptr->property + "'");
                        return std::nullopt;
                    }
                }

                // NULL_TYPE and ANY (non-temporal): use dynamic property ref
                // so the evaluator can handle VertexValue/EdgeValue at runtime.
                if (obj_type.kind == BoundTypeKind::NULL_TYPE || obj_type.kind == BoundTypeKind::ANY) {
                    auto dyn_ref = std::make_unique<BoundDynamicPropertyRef>();
                    dyn_ref->object = std::move(*obj);
                    dyn_ref->property = ptr->property;
                    dyn_ref->result_type = BoundType::Any();
                    return BoundExpression(std::move(dyn_ref));
                }

                error("TypeError: InvalidArgumentType");
                return std::nullopt;
            } else if constexpr (std::is_same_v<Elem, cypher::LabelCastExpr>) {
                auto obj = bindExpression(ptr->object);
                if (!obj)
                    return std::nullopt;

                LabelId lid = catalog_.labelNameToId(ptr->label);
                if (lid == INVALID_LABEL_ID) {
                    error("Label '" + ptr->label + "' not found");
                    return std::nullopt;
                }

                auto lc = std::make_unique<BoundLabelCast>();
                lc->object = std::move(*obj);
                lc->label_id = lid;
                lc->result_type = BoundType::Vertex();
                return BoundExpression(std::move(lc));
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                auto bl = std::make_unique<BoundList>();
                BoundType merged_elem = BoundType::Null();
                for (const auto& elem : ptr->elements) {
                    auto bound_elem = bindExpression(elem);
                    if (!bound_elem)
                        return std::nullopt;
                    merged_elem = BoundType::merge(merged_elem, getBoundExprType(*bound_elem));
                    bl->elements.push_back(std::move(*bound_elem));
                }
                bl->element_type = merged_elem;
                bl->result_type = BoundType::List(BoundType::clone(merged_elem));

                // When a VERTEX or EDGE variable is placed in a heterogeneous
                // list (any non-graph element mixed in), the element type
                // degrades to ANY.  Downstream dynamic property accesses like
                // (list[i]).prop then can't resolve which properties to load.
                // Pre-load all properties for those graph-element variables so
                // the runtime evaluator can find them.
                if (merged_elem.kind == BoundTypeKind::ANY) {
                    for (const auto& element : bl->elements) {
                        if (std::holds_alternative<BoundColumnRef>(element)) {
                            const auto& col_ref = std::get<BoundColumnRef>(element);
                            const BoundType& elem_type = getBoundExprType(element);
                            if (elem_type.kind == BoundTypeKind::VERTEX) {
                                for (const auto& [lid, ldef] : catalog_.allLabels()) {
                                    for (const auto& pd : ldef.properties) {
                                        ctx_.addPropertyRequirement(col_ref.name, lid, pd.id);
                                    }
                                }
                            } else if (elem_type.kind == BoundTypeKind::EDGE) {
                                for (const auto& [elid, eldef] : catalog_.allEdgeLabels()) {
                                    for (const auto& pd : eldef.properties) {
                                        ctx_.addPropertyRequirement(col_ref.name, elid, pd.id);
                                    }
                                }
                            }
                        }
                    }
                }

                return BoundExpression(std::move(bl));
            } else if constexpr (std::is_same_v<Elem, cypher::Parameter>) {
                auto it = params_.find(ptr->name);
                if (it != params_.end()) {
                    return BoundExpression(BoundLiteral(it->second));
                }
                return BoundExpression(BoundLiteral{});
            } else if constexpr (std::is_same_v<Elem, cypher::AllExpr> || std::is_same_v<Elem, cypher::AnyExpr> ||
                                 std::is_same_v<Elem, cypher::NoneExpr> || std::is_same_v<Elem, cypher::SingleExpr>) {
                auto list_expr = bindExpression(ptr->list_expr);
                if (!list_expr)
                    return std::nullopt;

                const BoundType& list_type = getBoundExprType(*list_expr);
                BoundType elem_type = (list_type.kind == BoundTypeKind::LIST && list_type.element_type)
                                          ? BoundType::clone(*list_type.element_type)
                                          : BoundType::Any();

                // Temporarily register loop variable in BindContext
                uint32_t loop_col = nextColumnIndex();
                ctx_.symbols[ptr->variable] = makeColumnInfo(ptr->variable, BoundType::clone(elem_type));

                std::optional<BoundExpression> where_pred;
                if (ptr->where_pred) {
                    where_pred = bindExpression(*ptr->where_pred);
                    if (!where_pred)
                        return std::nullopt;
                }

                // Remove loop variable from BindContext (scope ends)
                ctx_.symbols.erase(ptr->variable);

                BoundExpression result;
                BoundType bool_type = BoundType::Bool();
                if constexpr (std::is_same_v<Elem, cypher::AllExpr>) {
                    auto e = std::make_unique<BoundAllExpr>();
                    e->variable = ptr->variable;
                    e->loop_column_index = loop_col;
                    e->list_expr = std::move(*list_expr);
                    e->where_pred = std::move(where_pred);
                    e->result_type = std::move(bool_type);
                    result = BoundExpression(std::move(e));
                } else if constexpr (std::is_same_v<Elem, cypher::AnyExpr>) {
                    auto e = std::make_unique<BoundAnyExpr>();
                    e->variable = ptr->variable;
                    e->loop_column_index = loop_col;
                    e->list_expr = std::move(*list_expr);
                    e->where_pred = std::move(where_pred);
                    e->result_type = std::move(bool_type);
                    result = BoundExpression(std::move(e));
                } else if constexpr (std::is_same_v<Elem, cypher::NoneExpr>) {
                    auto e = std::make_unique<BoundNoneExpr>();
                    e->variable = ptr->variable;
                    e->loop_column_index = loop_col;
                    e->list_expr = std::move(*list_expr);
                    e->where_pred = std::move(where_pred);
                    e->result_type = std::move(bool_type);
                    result = BoundExpression(std::move(e));
                } else {
                    auto e = std::make_unique<BoundSingleExpr>();
                    e->variable = ptr->variable;
                    e->loop_column_index = loop_col;
                    e->list_expr = std::move(*list_expr);
                    e->where_pred = std::move(where_pred);
                    e->result_type = std::move(bool_type);
                    result = BoundExpression(std::move(e));
                }
                return result;
            } else if constexpr (std::is_same_v<Elem, cypher::ExistsExpr>) {
                error("EXISTS is only supported in WHERE clauses");
                return std::nullopt;
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                auto bc = std::make_unique<BoundCase>();

                if (ptr->subject) {
                    auto bound_subject = bindExpression(*ptr->subject);
                    if (!bound_subject)
                        return std::nullopt;
                    bc->subject = std::move(*bound_subject);
                }

                for (const auto& [when_expr, then_expr] : ptr->when_thens) {
                    auto bound_when = bindExpression(when_expr);
                    auto bound_then = bindExpression(then_expr);
                    if (!bound_when || !bound_then)
                        return std::nullopt;
                    bc->when_thens.emplace_back(std::move(*bound_when), std::move(*bound_then));
                }

                if (ptr->else_expr) {
                    auto bound_else = bindExpression(*ptr->else_expr);
                    if (!bound_else)
                        return std::nullopt;
                    bc->else_expr = std::move(*bound_else);
                }

                bc->result_type = BoundType::Any();
                return BoundExpression(std::move(bc));
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                auto bm = std::make_unique<BoundMap>();
                BoundType merged_value_type = BoundType::Null();
                std::unordered_set<std::string> seen_keys;
                for (const auto& [key, expr] : ptr->entries) {
                    if (!seen_keys.insert(key).second) {
                        error("SemanticError: Duplicate map key '" + key + "'");
                        return std::nullopt;
                    }
                    auto bound_expr = bindExpression(expr);
                    if (!bound_expr)
                        return std::nullopt;
                    merged_value_type = BoundType::merge(merged_value_type, getBoundExprType(*bound_expr));
                    bm->entries.push_back({key, std::move(*bound_expr)});
                }
                bm->result_type = BoundType::Map(BoundType::String(), std::move(merged_value_type));
                return BoundExpression(std::move(bm));
            } else if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                // openCypher forbids aggregate calls inside the row-wise parts of a
                // list comprehension (WHERE filter and projection). Aggregates are
                // permitted in the list_expr (iteration source), e.g. `[x IN collect(p) | ...]`.
                if ((ptr->where_pred && containsAggregateCall(*ptr->where_pred)) ||
                    (ptr->projection && containsAggregateCall(*ptr->projection))) {
                    error("SyntaxError: InvalidAggregation: aggregation functions are not allowed "
                          "inside a list comprehension's WHERE filter or projection");
                    return std::nullopt;
                }

                auto list_expr = bindExpression(ptr->list_expr);
                if (!list_expr)
                    return std::nullopt;

                const BoundType& list_type = getBoundExprType(*list_expr);
                BoundType elem_type = (list_type.kind == BoundTypeKind::LIST && list_type.element_type)
                                          ? BoundType::clone(*list_type.element_type)
                                          : BoundType::Any();

                // Register loop variable in BindContext
                uint32_t loop_col = nextColumnIndex();
                ctx_.symbols[ptr->variable] = makeColumnInfo(ptr->variable, BoundType::clone(elem_type));

                std::optional<BoundExpression> where_pred;
                if (ptr->where_pred) {
                    where_pred = bindExpression(*ptr->where_pred);
                    if (!where_pred) {
                        ctx_.symbols.erase(ptr->variable);
                        return std::nullopt;
                    }
                }

                std::optional<BoundExpression> projection;
                if (ptr->projection) {
                    projection = bindExpression(*ptr->projection);
                } else {
                    auto var_expr = cypher::Expression(std::make_unique<cypher::Variable>(ptr->variable));
                    projection = bindExpression(var_expr);
                }
                if (!projection) {
                    ctx_.symbols.erase(ptr->variable);
                    return std::nullopt;
                }

                // Remove loop variable from scope
                ctx_.symbols.erase(ptr->variable);

                auto lc = std::make_unique<BoundListComprehension>();
                lc->variable = ptr->variable;
                lc->loop_column_index = loop_col;
                lc->list_expr = std::move(*list_expr);
                lc->where_pred = std::move(where_pred);
                lc->projection = std::move(*projection);
                lc->result_type = BoundType::List(BoundType::clone(getBoundExprType(lc->projection)));
                return BoundExpression(std::move(lc));
            } else if constexpr (std::is_same_v<Elem, cypher::SubscriptExpr>) {
                auto bound_list = bindExpression(ptr->list);
                auto bound_index = bindExpression(ptr->index);
                if (!bound_list || !bound_index)
                    return std::nullopt;
                const BoundType& list_type = getBoundExprType(*bound_list);

                // For vertices/edges, n['name'] is dynamic property access.
                if ((list_type.kind == BoundTypeKind::VERTEX || list_type.kind == BoundTypeKind::EDGE ||
                     list_type.kind == BoundTypeKind::ANY || list_type.kind == BoundTypeKind::NULL_TYPE)) {
                    // Extract property name from string literal or parameter
                    std::optional<std::string> prop_name;
                    if (std::holds_alternative<BoundLiteral>(*bound_index)) {
                        const auto& lit = std::get<BoundLiteral>(*bound_index);
                        if (auto* s = std::get_if<std::string>(&lit.value))
                            prop_name = *s;
                    } else if (std::holds_alternative<BoundParameter>(*bound_index)) {
                        // Parameter — defer to runtime; use empty name as sentinel
                        auto dyn_ref = std::make_unique<BoundDynamicPropertyRef>();
                        dyn_ref->object = std::move(*bound_list);
                        dyn_ref->property = std::get<BoundParameter>(*bound_index).name;
                        dyn_ref->result_type = BoundType::Any();
                        return BoundExpression(std::move(dyn_ref));
                    }
                    if (prop_name) {
                        auto dyn_ref = std::make_unique<BoundDynamicPropertyRef>();
                        dyn_ref->object = std::move(*bound_list);
                        dyn_ref->property = *prop_name;
                        dyn_ref->result_type = BoundType::Any();
                        return BoundExpression(std::move(dyn_ref));
                    }
                }

                auto sub = std::make_unique<BoundSubscript>();
                if (list_type.kind == BoundTypeKind::LIST) {
                    sub->result_type =
                        list_type.element_type ? BoundType::clone(*list_type.element_type) : BoundType::Any();
                } else if (list_type.kind == BoundTypeKind::MAP) {
                    sub->result_type =
                        list_type.map_value_type ? BoundType::clone(*list_type.map_value_type) : BoundType::Any();
                } else {
                    sub->result_type = BoundType::Any();
                }
                sub->list = std::move(*bound_list);
                sub->index = std::move(*bound_index);
                return BoundExpression(std::move(sub));
            } else if constexpr (std::is_same_v<Elem, cypher::SliceExpr>) {
                auto bound_list = bindExpression(ptr->list);
                if (!bound_list)
                    return std::nullopt;
                auto sl = std::make_unique<BoundSlice>();
                const BoundType& list_type = getBoundExprType(*bound_list);
                sl->result_type = BoundType::clone(list_type);
                sl->list = std::move(*bound_list);
                if (ptr->from) {
                    auto bound_from = bindExpression(*ptr->from);
                    if (!bound_from)
                        return std::nullopt;
                    sl->from = std::move(*bound_from);
                }
                if (ptr->to) {
                    auto bound_to = bindExpression(*ptr->to);
                    if (!bound_to)
                        return std::nullopt;
                    sl->to = std::move(*bound_to);
                }
                return BoundExpression(std::move(sl));
            } else {
                // Other types not yet supported
                return std::nullopt;
            }
        },
        expr);
}

// ==================== Type Inference ====================

BoundType Binder::inferBinaryOpType(cypher::BinaryOperator op, const BoundType& left_type, const BoundType& right_type,
                                    std::string& error_msg) {
    switch (op) {
    case cypher::BinaryOperator::EQ:
    case cypher::BinaryOperator::NEQ:
    case cypher::BinaryOperator::LT:
    case cypher::BinaryOperator::GT:
    case cypher::BinaryOperator::LTE:
    case cypher::BinaryOperator::GTE:
        // Comparison: always allowed. Cross-type comparisons return false/null at runtime
        // (EQ → false, NEQ → true, ordered → null for incompatible types).
        return BoundType::Bool();

    case cypher::BinaryOperator::AND:
    case cypher::BinaryOperator::OR:
    case cypher::BinaryOperator::XOR: {
        auto ok = [](const BoundType& t) {
            return t.kind == BoundTypeKind::BOOL || t.kind == BoundTypeKind::NULL_TYPE || t.kind == BoundTypeKind::ANY;
        };
        if (ok(left_type) && ok(right_type))
            return BoundType::Bool();
        error_msg = "InvalidArgumentType: Logical operators require boolean operands";
        return BoundType::Any();
    }

    case cypher::BinaryOperator::ADD: {
        // String concatenation
        if (left_type.kind == BoundTypeKind::STRING && right_type.kind == BoundTypeKind::STRING)
            return BoundType::String();
        if ((left_type.kind == BoundTypeKind::STRING || left_type.kind == BoundTypeKind::ANY ||
             left_type.kind == BoundTypeKind::NULL_TYPE) &&
            (right_type.kind == BoundTypeKind::STRING || right_type.kind == BoundTypeKind::ANY ||
             right_type.kind == BoundTypeKind::NULL_TYPE) &&
            (left_type.kind == BoundTypeKind::STRING || right_type.kind == BoundTypeKind::STRING))
            return BoundType::String();
        // List concatenation: LIST + LIST
        if (left_type.kind == BoundTypeKind::LIST && right_type.kind == BoundTypeKind::LIST)
            return BoundType::Any();
        // List append: LIST + scalar or scalar + LIST
        if (left_type.kind == BoundTypeKind::LIST || right_type.kind == BoundTypeKind::LIST)
            return BoundType::Any();
        // ANY/NULL: defer to runtime
        if (left_type.kind == BoundTypeKind::ANY || left_type.kind == BoundTypeKind::NULL_TYPE ||
            right_type.kind == BoundTypeKind::ANY || right_type.kind == BoundTypeKind::NULL_TYPE)
            return BoundType::Any();
        // Temporal arithmetic: datetime+duration→datetime, time+duration→time, duration+duration→duration
        if (isTemporalType(left_type.kind) && isTemporalType(right_type.kind)) {
            if (left_type.kind == BoundTypeKind::DATETIME && right_type.kind == BoundTypeKind::DURATION)
                return BoundType::DateTime();
            if (left_type.kind == BoundTypeKind::DURATION && right_type.kind == BoundTypeKind::DATETIME)
                return BoundType::DateTime();
            if (left_type.kind == BoundTypeKind::TIME && right_type.kind == BoundTypeKind::DURATION)
                return BoundType::Time();
            if (left_type.kind == BoundTypeKind::DURATION && right_type.kind == BoundTypeKind::TIME)
                return BoundType::Time();
            if (left_type.kind == BoundTypeKind::DURATION && right_type.kind == BoundTypeKind::DURATION)
                return BoundType::Duration();
            return BoundType::Any();
        }
        // Numeric arithmetic (fallthrough)
        if (left_type == BoundType::Int64() && right_type == BoundType::Int64())
            return BoundType::Int64();
        if ((left_type.kind == BoundTypeKind::INT64 || left_type.kind == BoundTypeKind::DOUBLE) &&
            (right_type.kind == BoundTypeKind::INT64 || right_type.kind == BoundTypeKind::DOUBLE))
            return BoundType::Double();
        error_msg = "Cannot apply + to " + left_type.toString() + " and " + right_type.toString();
        return BoundType::Any();
    }
    case cypher::BinaryOperator::SUB:
    case cypher::BinaryOperator::MUL:
    case cypher::BinaryOperator::DIV:
    case cypher::BinaryOperator::MOD:
    case cypher::BinaryOperator::POW:
        // Temporal subtraction: datetime-datetime→duration, time-time→duration,
        // datetime-duration→datetime, time-duration→time, duration-duration→duration
        if (op == cypher::BinaryOperator::SUB && isTemporalType(left_type.kind) && isTemporalType(right_type.kind)) {
            if (left_type.kind == BoundTypeKind::DATETIME && right_type.kind == BoundTypeKind::DATETIME)
                return BoundType::Duration();
            if (left_type.kind == BoundTypeKind::TIME && right_type.kind == BoundTypeKind::TIME)
                return BoundType::Duration();
            if (left_type.kind == BoundTypeKind::DATETIME && right_type.kind == BoundTypeKind::DURATION)
                return BoundType::DateTime();
            if (left_type.kind == BoundTypeKind::TIME && right_type.kind == BoundTypeKind::DURATION)
                return BoundType::Time();
            if (left_type.kind == BoundTypeKind::DURATION && right_type.kind == BoundTypeKind::DURATION)
                return BoundType::Duration();
            return BoundType::Any();
        }
        // Duration * number / Duration / number
        if ((op == cypher::BinaryOperator::MUL || op == cypher::BinaryOperator::DIV) &&
            left_type.kind == BoundTypeKind::DURATION &&
            (right_type.kind == BoundTypeKind::INT64 || right_type.kind == BoundTypeKind::DOUBLE))
            return BoundType::Duration();
        // Number * duration
        if (op == cypher::BinaryOperator::MUL &&
            (left_type.kind == BoundTypeKind::INT64 || left_type.kind == BoundTypeKind::DOUBLE) &&
            right_type.kind == BoundTypeKind::DURATION)
            return BoundType::Duration();
        // POW always returns DOUBLE even for integer operands (Cypher spec)
        if (op == cypher::BinaryOperator::POW)
            return BoundType::Double();
        // Arithmetic: INT64 or DOUBLE, with implicit conversion
        if (left_type == BoundType::Int64() && right_type == BoundType::Int64())
            return BoundType::Int64();
        if ((left_type.kind == BoundTypeKind::INT64 || left_type.kind == BoundTypeKind::DOUBLE) &&
            (right_type.kind == BoundTypeKind::INT64 || right_type.kind == BoundTypeKind::DOUBLE))
            return BoundType::Double();
        // ANY/NULL: defer to runtime
        if (left_type.kind == BoundTypeKind::ANY || left_type.kind == BoundTypeKind::NULL_TYPE ||
            right_type.kind == BoundTypeKind::ANY || right_type.kind == BoundTypeKind::NULL_TYPE)
            return BoundType::Any();
        error_msg = "InvalidArgumentType: Arithmetic requires numeric operands: got " + left_type.toString() + " and " +
                    right_type.toString();
        return BoundType::Any();

    case cypher::BinaryOperator::STARTS_WITH:
    case cypher::BinaryOperator::ENDS_WITH:
    case cypher::BinaryOperator::CONTAINS:
        // Always allowed. Non-string operands return null at runtime.
        return BoundType::Bool();

    case cypher::BinaryOperator::IN:
        if (right_type.kind == BoundTypeKind::LIST || right_type.kind == BoundTypeKind::ANY ||
            right_type.kind == BoundTypeKind::NULL_TYPE)
            return BoundType::Bool();
        error_msg = "InvalidArgumentType: IN requires a list on the right side, got " + right_type.toString();
        return BoundType::Any();

    case cypher::BinaryOperator::LIST_CONCAT: {
        auto is_listy = [](const BoundType& t) {
            return t.kind == BoundTypeKind::LIST || t.kind == BoundTypeKind::ANY || t.kind == BoundTypeKind::NULL_TYPE;
        };
        if (is_listy(left_type) && is_listy(right_type))
            return BoundType::Any();
        error_msg = "List concatenation requires list operands";
        return BoundType::Any();
    }

    default:
        error_msg = "Operator not implemented";
        return BoundType::Any();
    }
}

BoundType Binder::inferUnaryOpType(cypher::UnaryOperator op, const BoundType& operand_type, std::string& error_msg) {
    switch (op) {
    case cypher::UnaryOperator::NOT:
        if (operand_type.kind == BoundTypeKind::BOOL || operand_type.kind == BoundTypeKind::NULL_TYPE ||
            operand_type.kind == BoundTypeKind::ANY)
            return BoundType::Bool();
        error_msg = "InvalidArgumentType: NOT requires boolean operand, got " + operand_type.toString();
        return BoundType::Any();

    case cypher::UnaryOperator::NEGATE:
        if (operand_type.kind == BoundTypeKind::INT64)
            return BoundType::Int64();
        if (operand_type.kind == BoundTypeKind::DOUBLE)
            return BoundType::Double();
        error_msg = "Negation requires numeric operand, got " + operand_type.toString();
        return BoundType::Any();

    case cypher::UnaryOperator::IS_NULL:
    case cypher::UnaryOperator::IS_NOT_NULL:
        return BoundType::Bool();

    default:
        error_msg = "Unary operator not implemented";
        return BoundType::Any();
    }
}

} // namespace binder
} // namespace eugraph
