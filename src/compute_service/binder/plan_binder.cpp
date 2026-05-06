#include "compute_service/binder/plan_binder.hpp"

#include "compute_service/logical_plan/operator/aggregate_op.hpp"
#include "compute_service/logical_plan/operator/all_node_scan_op.hpp"
#include "compute_service/logical_plan/operator/create_edge_op.hpp"
#include "compute_service/logical_plan/operator/create_node_op.hpp"
#include "compute_service/logical_plan/operator/expand_op.hpp"
#include "compute_service/logical_plan/operator/filter_op.hpp"
#include "compute_service/logical_plan/operator/label_scan_op.hpp"
#include "compute_service/logical_plan/operator/limit_op.hpp"
#include "compute_service/logical_plan/operator/path_build_op.hpp"
#include "compute_service/logical_plan/operator/project_op.hpp"
#include "compute_service/logical_plan/operator/remove_op.hpp"
#include "compute_service/logical_plan/operator/set_op.hpp"
#include "compute_service/logical_plan/operator/skip_op.hpp"
#include "compute_service/logical_plan/operator/sort_op.hpp"

namespace eugraph {
namespace binder {

using namespace eugraph::compute;

BoundPlanResult PlanBinder::bind(const LogicalPlan& plan) {
    errors_.clear();
    ctx_ = BindContext{};

    BoundPlanResult result;
    walkAndBind(plan.root, result);

    result.errors = std::move(errors_);
    result.context = std::move(ctx_);
    return result;
}

void PlanBinder::walkAndBind(const LogicalOperator& op, BoundPlanResult& result) {
    std::visit(
        [this, &result](const auto& ptr) {
            using T = std::decay_t<decltype(ptr)>;
            using OpType = typename T::element_type;

            // Recurse into children FIRST to register variables
            // before binding parent expressions that reference them.
            for (const auto& child : ptr->children) {
                walkAndBind(child, result);
            }

            // Register variables from scan/expand operators
            if constexpr (std::is_same_v<OpType, AllNodeScanOp>) {
                registerVariable(ptr->variable, BoundType::Vertex(), ctx_.next_column_index++);
            } else if constexpr (std::is_same_v<OpType, LabelScanOp>) {
                registerVariable(ptr->variable, BoundType::Vertex(), ctx_.next_column_index++);
                const LabelDef* ldef = catalog_.lookupLabel(ptr->label);
                if (!ldef) {
                    error("Label '" + ptr->label + "' not found");
                }
            } else if constexpr (std::is_same_v<OpType, ExpandOp>) {
                registerVariable(ptr->dst_variable, BoundType::Vertex(), ctx_.next_column_index++);
                if (!ptr->edge_variable.empty()) {
                    registerVariable(ptr->edge_variable, BoundType::Edge(), ctx_.next_column_index++);
                }
                for (const auto& et_name : ptr->rel_types) {
                    if (!catalog_.lookupEdgeLabel(et_name)) {
                        error("Edge type '" + et_name + "' not found");
                    }
                }
            } else if constexpr (std::is_same_v<OpType, PathBuildOp>) {
                registerVariable(ptr->path_variable, BoundType::Path(), ctx_.next_column_index++);
            } else if constexpr (std::is_same_v<OpType, CreateNodeOp>) {
                registerVariable(ptr->variable, BoundType::Vertex(), ctx_.next_column_index++);
                if (!ptr->labels.empty() && !catalog_.lookupLabel(ptr->labels[0])) {
                    error("Label '" + ptr->labels[0] + "' not found");
                }
            } else if constexpr (std::is_same_v<OpType, CreateEdgeOp>) {
                registerVariable(ptr->variable, BoundType::Edge(), ctx_.next_column_index++);
                if (!ptr->rel_types.empty() && !catalog_.lookupEdgeLabel(ptr->rel_types[0])) {
                    error("Edge type '" + ptr->rel_types[0] + "' not found");
                }
            }

            // Bind expressions within operators
            if constexpr (std::is_same_v<OpType, FilterOp>) {
                auto bound = bindExpression(ptr->predicate);
                if (bound) {
                    result.bound_predicates[ptr.get()] = std::move(*bound);
                }
            } else if constexpr (std::is_same_v<OpType, ProjectOp>) {
                std::vector<BoundExpression> bound_items;
                for (const auto& item : ptr->items) {
                    auto bound = bindExpression(item.expr);
                    if (bound) {
                        bound_items.push_back(std::move(*bound));
                    }
                    // Register alias as a column so ORDER BY / downstream can reference it.
                    std::string col_name = item.alias.value_or(cypher::expressionToString(item.expr));
                    registerVariable(col_name, bound ? getBoundExprType(*bound) : BoundType::Any(),
                                     ctx_.next_column_index++);
                }
                if (!bound_items.empty()) {
                    result.bound_projections[ptr.get()] = std::move(bound_items);
                }
            } else if constexpr (std::is_same_v<OpType, SortOp>) {
                std::vector<BoundExpression> bound_keys;
                for (const auto& si : ptr->sort_items) {
                    auto bound = bindExpression(si.expr);
                    if (bound) {
                        bound_keys.push_back(std::move(*bound));
                    }
                }
                if (!bound_keys.empty()) {
                    result.bound_sort_keys[ptr.get()] = std::move(bound_keys);
                }
            } else if constexpr (std::is_same_v<OpType, AggregateOp>) {
                for (const auto& gk : ptr->group_keys) {
                    bindExpression(gk); // validate, don't store
                }
                for (const auto& agg : ptr->aggregates) {
                    if (!func_registry_.exists(agg.func_name)) {
                        error("Aggregate function '" + agg.func_name + "' not found");
                    }
                    bindExpression(agg.arg); // validate
                }
            }
        },
        op);
}

std::optional<BoundExpression> PlanBinder::bindExpression(const cypher::Expression& expr) {
    return std::visit(
        [this](const auto& ptr) -> std::optional<BoundExpression> {
            using T = std::decay_t<decltype(ptr)>;
            using Elem = typename T::element_type;

            if constexpr (std::is_same_v<Elem, cypher::Literal>) {
                return BoundExpression(std::visit(
                    [](const auto& v) -> BoundExpression {
                        using V = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<V, cypher::NullValue>)
                            return BoundLiteral{};
                        else if constexpr (std::is_same_v<V, bool>)
                            return BoundLiteral(v);
                        else if constexpr (std::is_same_v<V, int64_t>)
                            return BoundLiteral(v);
                        else if constexpr (std::is_same_v<V, double>)
                            return BoundLiteral(v);
                        else if constexpr (std::is_same_v<V, std::string>)
                            return BoundLiteral(std::string(v));
                        return BoundLiteral{};
                    },
                    ptr->value));
            }
            if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                auto* col = lookupVariable(ptr->name);
                if (!col) {
                    error("Variable '" + ptr->name + "' not defined");
                    return std::nullopt;
                }
                uint32_t idx = 0;
                for (const auto& [n, info] : ctx_.symbols) {
                    if (n == ptr->name)
                        break;
                    ++idx;
                }
                return BoundExpression(BoundColumnRef(idx, col->type, ptr->name));
            }
            if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                auto left = bindExpression(ptr->left);
                auto right = bindExpression(ptr->right);
                if (!left || !right)
                    return std::nullopt;
                std::string err;
                BoundType rt = inferBinaryOpType(ptr->op, getBoundExprType(*left), getBoundExprType(*right), err);
                if (!err.empty())
                    error(err);
                auto bin = std::make_unique<BoundBinaryOp>();
                bin->op = ptr->op;
                bin->left = std::move(*left);
                bin->right = std::move(*right);
                bin->result_type = std::move(rt);
                return BoundExpression(std::move(bin));
            }
            if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                auto operand = bindExpression(ptr->operand);
                if (!operand)
                    return std::nullopt;
                std::string err;
                BoundType rt = inferUnaryOpType(ptr->op, getBoundExprType(*operand), err);
                if (!err.empty())
                    error(err);
                auto un = std::make_unique<BoundUnaryOp>();
                un->op = ptr->op;
                un->operand = std::move(*operand);
                un->result_type = std::move(rt);
                return BoundExpression(std::move(un));
            }
            if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                std::vector<BoundType> arg_types;
                std::vector<BoundExpression> bound_args;
                for (const auto& arg : ptr->args) {
                    auto ba = bindExpression(arg);
                    if (!ba)
                        return std::nullopt;
                    arg_types.push_back(getBoundExprType(*ba));
                    bound_args.push_back(std::move(*ba));
                }
                const auto* func = func_registry_.lookup(ptr->name, arg_types);
                if (!func) {
                    error("Function '" + ptr->name + "' not found for given argument types");
                    return std::nullopt;
                }
                auto fc = std::make_unique<BoundFunctionCall>();
                fc->func_def = func;
                fc->args = std::move(bound_args);
                fc->return_type = BoundType::clone(func->return_type);
                fc->distinct = ptr->distinct;
                return BoundExpression(std::move(fc));
            }
            if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                auto obj = bindExpression(ptr->object);
                if (!obj)
                    return std::nullopt;
                const BoundType& ot = getBoundExprType(*obj);

                auto prop_ref = std::make_unique<BoundPropertyRef>();
                prop_ref->object = std::move(*obj);

                if (ot.kind == BoundTypeKind::VERTEX) {
                    // Try to find property across all labels
                    LabelIdSet all_ids;
                    for (const auto& [lid, ldef] : catalog_.allLabels())
                        all_ids.insert(lid);
                    auto candidates = catalog_.lookupPropertyAcrossLabels(all_ids, ptr->property);
                    if (candidates.empty()) {
                        error("Property '" + ptr->property + "' not found on any label");
                        return std::nullopt;
                    }
                    BoundType merged = BoundType::Null();
                    for (auto& [lid, pd] : candidates) {
                        BoundPropertyRef::ResolvedProperty rp;
                        rp.label_id = lid;
                        rp.prop_id = pd->id;
                        rp.type = propertyTypeToBoundType(pd->type);
                        merged = BoundType::merge(merged, rp.type);
                        prop_ref->candidates.push_back(rp);
                    }
                    prop_ref->result_type = merged;
                } else if (ot.kind == BoundTypeKind::EDGE) {
                    prop_ref->result_type = BoundType::Any();
                } else {
                    error("Property access on non-vertex/non-edge type");
                    return std::nullopt;
                }
                return BoundExpression(std::move(prop_ref));
            }
            if constexpr (std::is_same_v<Elem, cypher::LabelCastExpr>) {
                auto obj = bindExpression(ptr->object);
                if (!obj)
                    return std::nullopt;
                LabelId lid = catalog_.labelNameToId(ptr->label);
                if (lid == INVALID_LABEL_ID) {
                    error("Label '" + ptr->label + "' does not exist");
                    return std::nullopt;
                }
                auto lc = std::make_unique<BoundLabelCast>();
                lc->object = std::move(*obj);
                lc->label_id = lid;
                return BoundExpression(std::move(lc));
            }
            // ListExpr, Parameter, etc.
            if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                auto bl = std::make_unique<BoundList>();
                BoundType merged = BoundType::Null();
                for (const auto& elem : ptr->elements) {
                    auto be = bindExpression(elem);
                    if (!be)
                        return std::nullopt;
                    merged = BoundType::merge(merged, getBoundExprType(*be));
                    bl->elements.push_back(std::move(*be));
                }
                bl->element_type = merged;
                bl->result_type = BoundType::List(BoundType::clone(merged));
                return BoundExpression(std::move(bl));
            }
            if constexpr (std::is_same_v<Elem, cypher::Parameter>) {
                return BoundExpression(BoundParameter(ptr->name));
            }
            // Other types not yet supported — skip silently
            return std::nullopt;
        },
        expr);
}

BoundType PlanBinder::inferBinaryOpType(cypher::BinaryOperator op, const BoundType& left, const BoundType& right,
                                        std::string& err) {
    switch (op) {
    case cypher::BinaryOperator::EQ:
    case cypher::BinaryOperator::NEQ:
    case cypher::BinaryOperator::LT:
    case cypher::BinaryOperator::GT:
    case cypher::BinaryOperator::LTE:
    case cypher::BinaryOperator::GTE:
        if (left.canCastTo(right) || right.canCastTo(left))
            return BoundType::Bool();
        err = "Cannot compare " + left.toString() + " with " + right.toString();
        return BoundType::Any();
    case cypher::BinaryOperator::AND:
    case cypher::BinaryOperator::OR:
        return BoundType::Bool();
    case cypher::BinaryOperator::ADD:
        if (left == BoundType::Int64() && right == BoundType::Int64())
            return BoundType::Int64();
        if ((left.kind == BoundTypeKind::INT64 || left.kind == BoundTypeKind::DOUBLE) &&
            (right.kind == BoundTypeKind::INT64 || right.kind == BoundTypeKind::DOUBLE))
            return BoundType::Double();
        if (left.kind == BoundTypeKind::STRING && right.kind == BoundTypeKind::STRING)
            return BoundType::String();
        err = "Addition requires numeric or string operands";
        return BoundType::Any();
    case cypher::BinaryOperator::SUB:
    case cypher::BinaryOperator::MUL:
    case cypher::BinaryOperator::DIV:
    default:
        break;
    }
    return BoundType::Any();
}

BoundType PlanBinder::inferUnaryOpType(cypher::UnaryOperator op, const BoundType& operand, std::string& err) {
    switch (op) {
    case cypher::UnaryOperator::NOT:
        return BoundType::Bool();
    case cypher::UnaryOperator::NEGATE:
        if (operand.kind == BoundTypeKind::INT64)
            return BoundType::Int64();
        if (operand.kind == BoundTypeKind::DOUBLE)
            return BoundType::Double();
        err = "Negation requires numeric operand";
        return BoundType::Any();
    case cypher::UnaryOperator::IS_NULL:
    case cypher::UnaryOperator::IS_NOT_NULL:
        return BoundType::Bool();
    default:
        return BoundType::Any();
    }
}

BoundType PlanBinder::propertyTypeToBoundType(PropertyType pt) {
    switch (pt) {
    case PropertyType::BOOL:
        return BoundType::Bool();
    case PropertyType::INT64:
        return BoundType::Int64();
    case PropertyType::DOUBLE:
        return BoundType::Double();
    case PropertyType::STRING:
        return BoundType::String();
    default:
        return BoundType::Any();
    }
}

void PlanBinder::registerVariable(const std::string& name, BoundType type, uint32_t col_idx) {
    if (name.empty())
        return;
    ColumnInfo info;
    info.name = name;
    info.type = std::move(type);
    ctx_.symbols[name] = info;
}

const ColumnInfo* PlanBinder::lookupVariable(const std::string& name) const {
    auto it = ctx_.symbols.find(name);
    return it != ctx_.symbols.end() ? &it->second : nullptr;
}

} // namespace binder
} // namespace eugraph
