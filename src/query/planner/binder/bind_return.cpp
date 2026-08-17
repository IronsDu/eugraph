#include "query/planner/binder.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>

namespace eugraph {
namespace binder {

// ==================== Helpers ====================

static cypher::Expression cloneAstExpression(const cypher::Expression& expr);

static cypher::PropertiesMap cloneProperties(const cypher::PropertiesMap& props) {
    cypher::PropertiesMap out;
    for (const auto& [name, value] : props.entries)
        out.entries.emplace_back(name, cloneAstExpression(value));
    return out;
}

static cypher::NodePattern cloneNodePattern(const cypher::NodePattern& node) {
    cypher::NodePattern out;
    out.variable = node.variable;
    out.labels = node.labels;
    if (node.properties)
        out.properties = cloneProperties(*node.properties);
    return out;
}

static cypher::RelationshipPattern cloneRelationshipPattern(const cypher::RelationshipPattern& rel) {
    cypher::RelationshipPattern out;
    out.variable = rel.variable;
    out.rel_types = rel.rel_types;
    if (rel.properties)
        out.properties = cloneProperties(*rel.properties);
    out.direction = rel.direction;
    if (rel.range) {
        cypher::Expression first = cloneAstExpression(rel.range->first);
        cypher::Expression second = cloneAstExpression(rel.range->second);
        out.range = std::make_pair(std::move(first), std::move(second));
    }
    return out;
}

static cypher::PatternPart clonePatternPart(const cypher::PatternPart& part) {
    cypher::PatternPart out;
    out.variable = part.variable;
    out.element.node = cloneNodePattern(part.element.node);
    for (const auto& [rel, node] : part.element.chain)
        out.element.chain.emplace_back(cloneRelationshipPattern(rel), cloneNodePattern(node));
    return out;
}

static cypher::Expression cloneAstExpression(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> cypher::Expression {
            using T = std::decay_t<decltype(ptr)>;
            using Elem = typename T::element_type;
            if constexpr (std::is_same_v<Elem, cypher::Literal> || std::is_same_v<Elem, cypher::Variable> ||
                          std::is_same_v<Elem, cypher::Parameter>) {
                return std::make_unique<Elem>(*ptr);
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                auto out = std::make_unique<cypher::ParenExpr>();
                out->inner = cloneAstExpression(ptr->inner);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                auto out = std::make_unique<cypher::BinaryOp>();
                out->op = ptr->op;
                out->left = cloneAstExpression(ptr->left);
                out->right = cloneAstExpression(ptr->right);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                auto out = std::make_unique<cypher::UnaryOp>();
                out->op = ptr->op;
                out->operand = cloneAstExpression(ptr->operand);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                auto out = std::make_unique<cypher::FunctionCall>();
                out->name = ptr->name;
                out->distinct = ptr->distinct;
                for (const auto& arg : ptr->args)
                    out->args.push_back(cloneAstExpression(arg));
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                auto out = std::make_unique<cypher::PropertyAccess>();
                out->object = cloneAstExpression(ptr->object);
                out->property = ptr->property;
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::LabelCastExpr>) {
                auto out = std::make_unique<cypher::LabelCastExpr>();
                out->object = cloneAstExpression(ptr->object);
                out->label = ptr->label;
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                auto out = std::make_unique<cypher::ListExpr>();
                for (const auto& elem : ptr->elements)
                    out->elements.push_back(cloneAstExpression(elem));
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                auto out = std::make_unique<cypher::MapExpr>();
                for (const auto& [key, value] : ptr->entries)
                    out->entries.emplace_back(key, cloneAstExpression(value));
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                auto out = std::make_unique<cypher::CaseExpr>();
                if (ptr->subject)
                    out->subject = cloneAstExpression(*ptr->subject);
                for (const auto& [when, then] : ptr->when_thens)
                    out->when_thens.emplace_back(cloneAstExpression(when), cloneAstExpression(then));
                if (ptr->else_expr)
                    out->else_expr = cloneAstExpression(*ptr->else_expr);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::SubscriptExpr>) {
                auto out = std::make_unique<cypher::SubscriptExpr>();
                out->list = cloneAstExpression(ptr->list);
                out->index = cloneAstExpression(ptr->index);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::SliceExpr>) {
                auto out = std::make_unique<cypher::SliceExpr>();
                out->list = cloneAstExpression(ptr->list);
                if (ptr->from)
                    out->from = cloneAstExpression(*ptr->from);
                if (ptr->to)
                    out->to = cloneAstExpression(*ptr->to);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                auto out = std::make_unique<cypher::ListComprehension>();
                out->variable = ptr->variable;
                out->list_expr = cloneAstExpression(ptr->list_expr);
                if (ptr->where_pred)
                    out->where_pred = cloneAstExpression(*ptr->where_pred);
                if (ptr->projection)
                    out->projection = cloneAstExpression(*ptr->projection);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::PatternComprehension>) {
                auto out = std::make_unique<cypher::PatternComprehension>();
                out->variable = ptr->variable;
                for (const auto& pattern : ptr->patterns)
                    out->patterns.push_back(clonePatternPart(pattern));
                if (ptr->where_pred)
                    out->where_pred = cloneAstExpression(*ptr->where_pred);
                if (ptr->projection)
                    out->projection = cloneAstExpression(*ptr->projection);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::AllExpr> || std::is_same_v<Elem, cypher::AnyExpr> ||
                                 std::is_same_v<Elem, cypher::NoneExpr> || std::is_same_v<Elem, cypher::SingleExpr>) {
                auto out = std::make_unique<Elem>();
                out->variable = ptr->variable;
                out->list_expr = cloneAstExpression(ptr->list_expr);
                if (ptr->where_pred)
                    out->where_pred = cloneAstExpression(*ptr->where_pred);
                return out;
            } else if constexpr (std::is_same_v<Elem, cypher::ExistsExpr>) {
                // Full EXISTS subqueries are rejected outside WHERE, so the
                // full_query branch is intentionally not deep-cloned here.
                auto out = std::make_unique<cypher::ExistsExpr>();
                out->patterns.reserve(ptr->patterns.size());
                for (const auto& pattern : ptr->patterns)
                    out->patterns.push_back(clonePatternPart(pattern));
                if (ptr->where_pred)
                    out->where_pred = cloneAstExpression(*ptr->where_pred);
                out->is_bare_predicate = ptr->is_bare_predicate;
                return out;
            }
            return std::make_unique<cypher::Literal>();
        },
        expr);
}

static bool expressionReferencesVariableImpl(const cypher::Expression& expr, const std::string& name);

static bool expressionReferencesVariable(const cypher::Expression& expr, const std::string& name) {
    return expressionReferencesVariableImpl(expr, name);
}

static bool expressionReferencesVariableImpl(const cypher::Expression& expr, const std::string& name) {
    return std::visit(
        [&](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                return ptr->name == name;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return expressionReferencesVariableImpl(ptr->left, name) ||
                       expressionReferencesVariableImpl(ptr->right, name);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return expressionReferencesVariableImpl(ptr->operand, name);
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                for (const auto& arg : ptr->args)
                    if (expressionReferencesVariableImpl(arg, name))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                return expressionReferencesVariableImpl(ptr->object, name);
            } else if constexpr (std::is_same_v<Elem, cypher::SubscriptExpr>) {
                return expressionReferencesVariableImpl(ptr->list, name) ||
                       expressionReferencesVariableImpl(ptr->index, name);
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                if (ptr->subject && expressionReferencesVariableImpl(*ptr->subject, name))
                    return true;
                for (const auto& [cond, res] : ptr->when_thens) {
                    if (expressionReferencesVariableImpl(cond, name) || expressionReferencesVariableImpl(res, name))
                        return true;
                }
                if (ptr->else_expr && expressionReferencesVariableImpl(*ptr->else_expr, name))
                    return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& elem : ptr->elements)
                    if (expressionReferencesVariableImpl(elem, name))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries)
                    if (expressionReferencesVariableImpl(v, name))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                return expressionReferencesVariableImpl(ptr->list_expr, name) ||
                       (ptr->where_pred && expressionReferencesVariableImpl(*ptr->where_pred, name));
            } else {
                return false;
            }
        },
        expr);
}

// ==================== Aggregate Detection Helper ====================

static std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool isAggregateFunctionName(const std::string& name) {
    const std::string lower = toLowerAscii(name);
    return lower == "count" || lower == "sum" || lower == "avg" || lower == "min" || lower == "max" ||
           lower == "collect" || lower == "percentile_cont" || lower == "percentile_disc" ||
           lower == "percentilecont" || lower == "percentiledisc" || lower == "st_dev" || lower == "st_dev_p";
}

static bool hasAggregate(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                if (isAggregateFunctionName(ptr->name))
                    return true;
                for (const auto& arg : ptr->args)
                    if (hasAggregate(arg))
                        return true;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return hasAggregate(ptr->left) || hasAggregate(ptr->right);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return hasAggregate(ptr->operand);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                return hasAggregate(ptr->object);
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries)
                    if (hasAggregate(v))
                        return true;
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& e : ptr->elements)
                    if (hasAggregate(e))
                        return true;
            } else if constexpr (std::is_same_v<Elem, cypher::AllExpr> || std::is_same_v<Elem, cypher::AnyExpr> ||
                                 std::is_same_v<Elem, cypher::NoneExpr> || std::is_same_v<Elem, cypher::SingleExpr>) {
                if (hasAggregate(ptr->list_expr))
                    return true;
                if (ptr->where_pred && hasAggregate(*ptr->where_pred))
                    return true;
            } else if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                if (hasAggregate(ptr->list_expr))
                    return true;
                if (ptr->where_pred && hasAggregate(*ptr->where_pred))
                    return true;
                if (ptr->projection && hasAggregate(*ptr->projection))
                    return true;
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                if (ptr->subject && hasAggregate(*ptr->subject))
                    return true;
                for (const auto& [w, t] : ptr->when_thens)
                    if (hasAggregate(w) || hasAggregate(t))
                        return true;
                if (ptr->else_expr && hasAggregate(*ptr->else_expr))
                    return true;
            } else if constexpr (std::is_same_v<Elem, cypher::SubscriptExpr>) {
                return hasAggregate(ptr->list) || hasAggregate(ptr->index);
            } else if constexpr (std::is_same_v<Elem, cypher::SliceExpr>) {
                if (hasAggregate(ptr->list))
                    return true;
                if (ptr->from && hasAggregate(*ptr->from))
                    return true;
                if (ptr->to && hasAggregate(*ptr->to))
                    return true;
            }
            return false;
        },
        expr);
}

// ==================== Pattern Comprehension Hoisting ====================

// Walk a Cypher AST and collect pointers to every PatternComprehension node.
// Used to drive the hoisting pass that turns each comprehension into a
// BoundPatternComprehensionApplyOp stacked above the input child.
static void collectPatternComprehensionsAST(const cypher::Expression& expr,
                                            std::vector<const cypher::PatternComprehension*>& out) {
    std::visit(
        [&](const auto& ptr) {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::PatternComprehension>) {
                out.push_back(ptr.get());
                // The projection / where_pred could in turn contain nested
                // pattern comprehensions; recurse to be safe.
                if (ptr->projection)
                    collectPatternComprehensionsAST(*ptr->projection, out);
                if (ptr->where_pred)
                    collectPatternComprehensionsAST(*ptr->where_pred, out);
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                collectPatternComprehensionsAST(ptr->left, out);
                collectPatternComprehensionsAST(ptr->right, out);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                collectPatternComprehensionsAST(ptr->operand, out);
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                for (const auto& arg : ptr->args)
                    collectPatternComprehensionsAST(arg, out);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                collectPatternComprehensionsAST(ptr->object, out);
            } else if constexpr (std::is_same_v<Elem, cypher::SubscriptExpr>) {
                collectPatternComprehensionsAST(ptr->list, out);
                collectPatternComprehensionsAST(ptr->index, out);
            } else if constexpr (std::is_same_v<Elem, cypher::SliceExpr>) {
                collectPatternComprehensionsAST(ptr->list, out);
                if (ptr->from)
                    collectPatternComprehensionsAST(*ptr->from, out);
                if (ptr->to)
                    collectPatternComprehensionsAST(*ptr->to, out);
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                if (ptr->subject)
                    collectPatternComprehensionsAST(*ptr->subject, out);
                for (const auto& [w, t] : ptr->when_thens) {
                    collectPatternComprehensionsAST(w, out);
                    collectPatternComprehensionsAST(t, out);
                }
                if (ptr->else_expr)
                    collectPatternComprehensionsAST(*ptr->else_expr, out);
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& e : ptr->elements)
                    collectPatternComprehensionsAST(e, out);
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries)
                    collectPatternComprehensionsAST(v, out);
            } else if constexpr (std::is_same_v<Elem, cypher::AllExpr> || std::is_same_v<Elem, cypher::AnyExpr> ||
                                 std::is_same_v<Elem, cypher::NoneExpr> || std::is_same_v<Elem, cypher::SingleExpr>) {
                collectPatternComprehensionsAST(ptr->list_expr, out);
                if (ptr->where_pred)
                    collectPatternComprehensionsAST(*ptr->where_pred, out);
            } else if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                collectPatternComprehensionsAST(ptr->list_expr, out);
                if (ptr->where_pred)
                    collectPatternComprehensionsAST(*ptr->where_pred, out);
                if (ptr->projection)
                    collectPatternComprehensionsAST(*ptr->projection, out);
            }
        },
        expr);
}

// Patch every BoundPatternComprehension placeholder in a bound expression
// tree with the slot/name/type it should resolve to. The placeholder was
// created with only an AST pointer; the hoisting pass later learned where
// the corresponding Apply op emits its list column. We mutate in place
// because the placeholder is a value-type variant member.
static void patchPatternComprehensionPlaceholders(
    binder::BoundExpression& expr,
    const std::unordered_map<const cypher::PatternComprehension*,
                             std::tuple<binder::SlotId, std::string, binder::BoundType>>& patch_map) {
    std::visit(
        [&](auto& ptr) {
            using T = std::decay_t<decltype(ptr)>;
            if constexpr (std::is_same_v<T, binder::BoundPatternComprehension>) {
                auto it = patch_map.find(ptr.ast);
                if (it != patch_map.end()) {
                    ptr.output_slot = std::get<0>(it->second);
                    ptr.output_name = std::get<1>(it->second);
                    ptr.result_type = std::move(std::get<2>(it->second));
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                patchPatternComprehensionPlaceholders(ptr->left, patch_map);
                patchPatternComprehensionPlaceholders(ptr->right, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                patchPatternComprehensionPlaceholders(ptr->operand, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                for (auto& arg : ptr->args)
                    patchPatternComprehensionPlaceholders(arg, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                patchPatternComprehensionPlaceholders(ptr->object, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                patchPatternComprehensionPlaceholders(ptr->object, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (auto& elem : ptr->elements)
                    patchPatternComprehensionPlaceholders(elem, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (auto& [k, v] : ptr->entries)
                    patchPatternComprehensionPlaceholders(v, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (ptr->subject)
                    patchPatternComprehensionPlaceholders(*ptr->subject, patch_map);
                for (auto& [w, t] : ptr->when_thens) {
                    patchPatternComprehensionPlaceholders(w, patch_map);
                    patchPatternComprehensionPlaceholders(t, patch_map);
                }
                if (ptr->else_expr)
                    patchPatternComprehensionPlaceholders(*ptr->else_expr, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                patchPatternComprehensionPlaceholders(ptr->list, patch_map);
                patchPatternComprehensionPlaceholders(ptr->index, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                patchPatternComprehensionPlaceholders(ptr->list, patch_map);
                if (ptr->from)
                    patchPatternComprehensionPlaceholders(*ptr->from, patch_map);
                if (ptr->to)
                    patchPatternComprehensionPlaceholders(*ptr->to, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                patchPatternComprehensionPlaceholders(ptr->list_expr, patch_map);
                if (ptr->where_pred)
                    patchPatternComprehensionPlaceholders(*ptr->where_pred, patch_map);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                patchPatternComprehensionPlaceholders(ptr->list_expr, patch_map);
                if (ptr->where_pred)
                    patchPatternComprehensionPlaceholders(*ptr->where_pred, patch_map);
                patchPatternComprehensionPlaceholders(ptr->projection, patch_map);
            }
        },
        expr);
}

// Hoist every PatternComprehension referenced from `items` (and optionally
// `order_by_items`) above `child` as a stack of BoundPatternComprehensionApplyOp
// nodes. Returns the new top operator and fills `patch_map` so the caller can
// subsequently rewrite the placeholders in bound projection expressions.
//
// `Item` must have `.expr` of type cypher::Expression. We accept a generic
// template so the same helper serves RETURN items and WITH items.
template <typename Item>
bool hoistPatternComprehensions(
    Binder& binder, BoundLogicalOperator& child, const std::vector<Item>& items,
    const std::vector<cypher::OrderBy::SortItem>* order_by_items,
    std::unordered_map<const cypher::PatternComprehension*, std::tuple<binder::SlotId, std::string, binder::BoundType>>&
        patch_map) {
    std::vector<const cypher::PatternComprehension*> pc_asts;
    for (const auto& item : items)
        collectPatternComprehensionsAST(item.expr, pc_asts);
    if (order_by_items) {
        for (const auto& si : *order_by_items)
            collectPatternComprehensionsAST(si.expr, pc_asts);
    }
    if (pc_asts.empty())
        return true;

    // Dedupe by pointer (same AST node referenced twice would otherwise hoist
    // twice; TCK never does this, but cheap to guard).
    std::set<const cypher::PatternComprehension*> seen;
    for (const auto* pc : pc_asts) {
        if (!seen.insert(pc).second)
            continue;
        std::vector<std::pair<binder::SlotId, binder::SlotId>> corr;
        binder::SlotId out_slot = binder::INVALID_SLOT_ID;
        std::string out_name;
        binder::BoundType out_elem_type;
        auto apply_op = binder.bindPatternComprehension(*pc, std::move(child), corr, out_slot, out_name, out_elem_type);
        if (!apply_op) {
            return false;
        }
        child = std::move(*apply_op);
        patch_map[pc] = std::make_tuple(out_slot, out_name, binder::BoundType::List(out_elem_type));
    }
    return true;
}

// Detect ambiguous aggregation in ORDER BY expressions of aggregating
// queries: mixing aggregate and non-aggregate operands at the same BinaryOp
// level, where the non-aggregate side is itself a complex BinaryOp expression.
// e.g. `me.age + you.age + count(*)` → ADD(ADD(me.age, you.age), count(*))
// has a non-aggregate BinaryOp left and aggregate right → ambiguous.
// Simple non-aggregates (Variable, Literal, Parameter, PropertyAccess) mixed
// with aggregates are allowed (e.g. `age + count(*)`, `1 + avg(x)`).
// Caller must first confirm the expression contains an aggregate.
static bool isAmbiguousAggregationExpr(const cypher::Expression& expr) {
    auto* bin = std::get_if<std::unique_ptr<cypher::BinaryOp>>(&expr);
    if (!bin || !*bin)
        return false;
    bool left_has_agg = hasAggregate((*bin)->left);
    bool right_has_agg = hasAggregate((*bin)->right);
    if (left_has_agg != right_has_agg) {
        const auto& non_agg = left_has_agg ? (*bin)->right : (*bin)->left;
        // Ambiguous only when the non-aggregate side is a complex BinaryOp
        // (e.g. `me.age + you.age`). Simple expressions are allowed.
        if (std::get_if<std::unique_ptr<cypher::BinaryOp>>(&non_agg))
            return true;
    }
    return isAmbiguousAggregationExpr((*bin)->left) || isAmbiguousAggregationExpr((*bin)->right);
}

// Collect all variable names appearing anywhere in an expression.
static void collectAllVariables(const cypher::Expression& expr, std::set<std::string>& vars) {
    std::visit(
        [&](const auto& ptr) {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if (!ptr)
                return;
            if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                vars.insert(ptr->name);
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                collectAllVariables(ptr->left, vars);
                collectAllVariables(ptr->right, vars);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                collectAllVariables(ptr->operand, vars);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                collectAllVariables(ptr->object, vars);
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                for (const auto& arg : ptr->args)
                    collectAllVariables(arg, vars);
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                collectAllVariables(ptr->inner, vars);
            }
        },
        expr);
}

bool Binder::lowerListComprehensionWithPatternComprehension(const cypher::ListComprehension& lc,
                                                            BoundLogicalOperator& child, SlotId& out_slot,
                                                            std::string& out_name, BoundType& out_elem_type) {
    auto saved = ctx_.save();

    // Variables referenced from outside the list comprehension (the loop
    // variable itself is produced by the inner UNWIND).
    std::set<std::string> outer_vars;
    collectAllVariables(lc.list_expr, outer_vars);
    if (lc.where_pred)
        collectAllVariables(*lc.where_pred, outer_vars);
    if (lc.projection)
        collectAllVariables(*lc.projection, outer_vars);
    // collectAllVariables intentionally does not descend into pattern
    // comprehensions; gather outer references from their patterns/expressions
    // explicitly so patterns like `(n)-->(x)` can correlate on n as well.
    std::vector<const cypher::PatternComprehension*> nested_pcs;
    if (lc.where_pred)
        collectPatternComprehensionsAST(*lc.where_pred, nested_pcs);
    if (lc.projection)
        collectPatternComprehensionsAST(*lc.projection, nested_pcs);
    for (const auto* pc : nested_pcs) {
        if (pc->variable)
            outer_vars.insert(*pc->variable);
        for (const auto& pp : pc->patterns) {
            if (pp.element.node.variable)
                outer_vars.insert(*pp.element.node.variable);
            for (const auto& [rel, node] : pp.element.chain) {
                if (rel.variable)
                    outer_vars.insert(*rel.variable);
                if (node.variable)
                    outer_vars.insert(*node.variable);
            }
        }
        if (pc->where_pred)
            collectAllVariables(*pc->where_pred, outer_vars);
        if (pc->projection)
            collectAllVariables(*pc->projection, outer_vars);
    }
    outer_vars.erase(lc.variable);

    // Build the right sub-plan in an independent scope. The correlated source
    // keeps the outer slots and semantic types so expressions like nodes(p)
    // see the same values as the original row-wise list comprehension.
    ctx_.beginSubScope();
    BoundCorrelatedSourceOp source;
    std::vector<std::pair<SlotId, SlotId>> correlation;
    uint32_t col = 0;
    for (const auto& name : outer_vars) {
        auto it = saved.symbols.find(name);
        if (it == saved.symbols.end()) {
            ctx_.restore(saved);
            error("UndefinedVariable: Variable '" + name + "' not defined");
            return false;
        }
        ColumnInfo ci = it->second;
        ci.column_index = col++;
        ci.slot_id = it->second.slot_id;
        ctx_.symbols[name] = ci;

        source.variables.push_back(name);
        source.types.push_back(BoundType::clone(ci.type));
        source.column_indices.push_back(ci.column_index);
        source.slot_ids.push_back(ci.slot_id);
        correlation.emplace_back(ci.slot_id, ci.slot_id);
    }

    // UNWIND list_expr AS loop_var.
    auto bound_list = bindExpression(lc.list_expr);
    if (!bound_list) {
        ctx_.restore(saved);
        return false;
    }
    const BoundType& list_type = getBoundExprType(*bound_list);
    BoundType elem_type = (list_type.kind == BoundTypeKind::LIST && list_type.element_type)
                              ? BoundType::clone(*list_type.element_type)
                              : BoundType::Any();
    uint32_t var_col = nextColumnIndex();
    ctx_.symbols[lc.variable] = makeColumnInfo(lc.variable, BoundType::clone(elem_type));

    auto unwind = std::make_unique<BoundUnwindOp>();
    unwind->list_expr = std::move(*bound_list);
    unwind->variable = lc.variable;
    unwind->variable_column_index = var_col;
    unwind->child = BoundLogicalOperator(std::move(source));
    BoundLogicalOperator current = std::move(unwind);

    // Clone WHERE / projection once; hoisting and subsequent binding must use
    // the same AST nodes so the PatternComprehension patch map keys match.
    struct AstItem {
        cypher::Expression expr;
    };
    std::vector<AstItem> pc_items;
    std::optional<size_t> where_idx;
    std::optional<size_t> proj_idx;
    if (lc.where_pred) {
        AstItem item;
        item.expr = cloneAstExpression(*lc.where_pred);
        where_idx = pc_items.size();
        pc_items.push_back(std::move(item));
    }
    if (lc.projection) {
        AstItem item;
        item.expr = cloneAstExpression(*lc.projection);
        proj_idx = pc_items.size();
        pc_items.push_back(std::move(item));
    }

    std::unordered_map<const cypher::PatternComprehension*, std::tuple<SlotId, std::string, BoundType>> pc_patch_map;
    if (!hoistPatternComprehensions(*this, current, pc_items, nullptr, pc_patch_map)) {
        ctx_.restore(saved);
        return false;
    }

    // WHERE predicate is evaluated after the inner pattern comprehensions have
    // been computed for the current list element.
    if (where_idx) {
        auto bound_where = bindExpression(pc_items[*where_idx].expr);
        if (!bound_where) {
            ctx_.restore(saved);
            return false;
        }
        patchPatternComprehensionPlaceholders(*bound_where, pc_patch_map);
        auto filter = std::make_unique<BoundFilterOp>();
        filter->predicate = std::move(*bound_where);
        filter->child = std::move(current);
        current = std::move(filter);
    }

    BoundExpression proj_expr;
    if (proj_idx) {
        auto bound_proj = bindExpression(pc_items[*proj_idx].expr);
        if (!bound_proj) {
            ctx_.restore(saved);
            return false;
        }
        patchPatternComprehensionPlaceholders(*bound_proj, pc_patch_map);
        proj_expr = std::move(*bound_proj);
    } else {
        cypher::Expression var_expr(std::make_unique<cypher::Variable>(lc.variable));
        auto bound_proj = bindExpression(var_expr);
        if (!bound_proj) {
            ctx_.restore(saved);
            return false;
        }
        proj_expr = std::move(*bound_proj);
    }
    BoundType proj_type = getBoundExprType(proj_expr);

    // collect(projection) collapses the per-element results into one list per
    // outer row. No group keys: the right sub-plan is executed once per outer
    // row by PatternComprehensionApplyPhysicalOp.
    const function::FunctionDef* collect_fn = func_registry_.lookup("collect", {proj_type});
    if (!collect_fn)
        collect_fn = func_registry_.lookup("collect", {BoundType::Any()});
    if (!collect_fn) {
        ctx_.restore(saved);
        error("ListComprehension: collect() not registered");
        return false;
    }

    BoundAggregateOp::AggregateItem agg_item;
    agg_item.func_def = collect_fn;
    agg_item.function_name = "collect";
    agg_item.arguments.push_back(std::move(proj_expr));
    agg_item.alias = "__lc_agg_" + std::to_string(nextAnonId());
    agg_item.result_type = BoundType::List(BoundType::clone(proj_type));
    agg_item.keeps_nulls = true;

    auto agg = std::make_unique<BoundAggregateOp>();
    agg->aggregates.push_back(std::move(agg_item));
    agg->output_names.push_back(agg->aggregates.back().alias);
    agg->child = std::move(current);
    current = std::move(agg);

    ctx_.restore(saved);

    // Wrap the right sub-plan in the same Apply operator used by ordinary
    // pattern comprehensions; the final list column is then a normal column
    // for downstream RETURN / WITH binding.
    out_name = "__lc_" + std::to_string(nextAnonId());
    out_slot = allocateNamedSlot(out_name);
    out_elem_type = BoundType::clone(proj_type);

    auto apply = std::make_unique<BoundPatternComprehensionApplyOp>();
    apply->left = std::move(child);
    apply->right = std::move(current);
    apply->correlation = std::move(correlation);
    BoundPatternComprehensionApplyOp::Output out;
    out.slot_id = out_slot;
    out.name = out_name;
    out.element_type = BoundType::clone(proj_type);
    apply->outputs.push_back(std::move(out));
    child = std::move(apply);

    ColumnInfo out_info;
    out_info.name = out_name;
    out_info.type = BoundType::List(BoundType::clone(proj_type));
    out_info.column_index = nextColumnIndex();
    out_info.slot_id = out_slot;
    ctx_.symbols[out_name] = std::move(out_info);
    return true;
}

// Collect variables from expressions OUTSIDE aggregate function calls.
// Variables inside aggregate calls (e.g. `n` in `collect(n)`) are skipped
// because they are properly aggregated.
static void collectNonAggregateVariables(const cypher::Expression& expr, std::set<std::string>& vars,
                                         const std::set<std::string>& grouping_key_strs) {
    std::visit(
        [&](const auto& ptr) {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if (!ptr)
                return;
            if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                if (isAggregateFunctionName(ptr->name))
                    return; // aggregate call: skip variables inside
                for (const auto& arg : ptr->args)
                    collectNonAggregateVariables(arg, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                vars.insert(ptr->name);
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                collectNonAggregateVariables(ptr->left, vars, grouping_key_strs);
                collectNonAggregateVariables(ptr->right, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                collectNonAggregateVariables(ptr->operand, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                collectNonAggregateVariables(ptr->object, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                collectNonAggregateVariables(ptr->inner, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& e : ptr->elements)
                    collectNonAggregateVariables(e, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries)
                    collectNonAggregateVariables(v, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                if (ptr->projection)
                    collectNonAggregateVariables(*ptr->projection, vars, grouping_key_strs);
                if (ptr->where_pred)
                    collectNonAggregateVariables(*ptr->where_pred, vars, grouping_key_strs);
                // Don't collect the loop variable
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                if (ptr->subject)
                    collectNonAggregateVariables(*ptr->subject, vars, grouping_key_strs);
                for (const auto& [w, t] : ptr->when_thens) {
                    collectNonAggregateVariables(w, vars, grouping_key_strs);
                    collectNonAggregateVariables(t, vars, grouping_key_strs);
                }
                if (ptr->else_expr)
                    collectNonAggregateVariables(*ptr->else_expr, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::SubscriptExpr>) {
                collectNonAggregateVariables(ptr->list, vars, grouping_key_strs);
                collectNonAggregateVariables(ptr->index, vars, grouping_key_strs);
            } else if constexpr (std::is_same_v<Elem, cypher::SliceExpr>) {
                collectNonAggregateVariables(ptr->list, vars, grouping_key_strs);
                if (ptr->from)
                    collectNonAggregateVariables(*ptr->from, vars, grouping_key_strs);
                if (ptr->to)
                    collectNonAggregateVariables(*ptr->to, vars, grouping_key_strs);
            }
        },
        expr);
}

// Validate ORDER BY expression for aggregating WITH.  Rules:
// - Aggregate calls matching a projection aggregate (by expression string) → OK
// - Aggregate calls NOT matching → arguments' variables must be projected names
// - Non-aggregate expressions matching a grouping key expression → OK
// - Other Variables → must be projected names, else UndefinedVariable
// Returns false and sets err_var on failure.
static bool validateAggOrderByExpr(const cypher::Expression& expr, const std::set<std::string>& projection_aggs,
                                   const std::set<std::string>& grouping_key_exprs,
                                   const std::set<std::string>& projected_names, std::string& err_var) {
    return std::visit(
        [&](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if (!ptr)
                return true;

            if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                if (isAggregateFunctionName(ptr->name)) {
                    if (projection_aggs.count(cypher::expressionToString(expr)))
                        return true;
                    // Non-matching aggregate: arguments must use projected names only
                    for (const auto& arg : ptr->args) {
                        std::set<std::string> vars;
                        collectAllVariables(arg, vars);
                        for (const auto& v : vars) {
                            if (!projected_names.count(v)) {
                                err_var = v;
                                return false;
                            }
                        }
                    }
                    return true;
                }
                for (const auto& arg : ptr->args)
                    if (!validateAggOrderByExpr(arg, projection_aggs, grouping_key_exprs, projected_names, err_var))
                        return false;
                return true;
            } else if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                if (projected_names.count(ptr->name))
                    return true;
                err_var = ptr->name;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                if (grouping_key_exprs.count(cypher::expressionToString(expr)))
                    return true;
                return validateAggOrderByExpr(ptr->left, projection_aggs, grouping_key_exprs, projected_names,
                                              err_var) &&
                       validateAggOrderByExpr(ptr->right, projection_aggs, grouping_key_exprs, projected_names,
                                              err_var);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return validateAggOrderByExpr(ptr->operand, projection_aggs, grouping_key_exprs, projected_names,
                                              err_var);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                if (grouping_key_exprs.count(cypher::expressionToString(expr)))
                    return true;
                return validateAggOrderByExpr(ptr->object, projection_aggs, grouping_key_exprs, projected_names,
                                              err_var);
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                return validateAggOrderByExpr(ptr->inner, projection_aggs, grouping_key_exprs, projected_names,
                                              err_var);
            }
            return true; // Literals, parameters, etc.
        },
        expr);
}

// ==================== Aggregate Extraction & Replacement ====================

/// Walk a BoundExpression tree, replacing every aggregate BoundFunctionCall with
/// a BoundVariableRef to an anonymous column (__agg_0, __agg_1, ...).
/// The extracted aggregate info is pushed into `out_aggs`.
static void walkAndReplaceAggCalls(binder::BoundExpression& expr,
                                   std::vector<BoundAggregateOp::AggregateItem>& out_aggs, uint32_t& agg_idx,
                                   size_t group_keys_size) {
    std::visit(
        [&](auto& ptr) {
            using T = std::decay_t<decltype(ptr)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                if (ptr->func_def->is_aggregate) {
                    // Extract aggregate info.
                    BoundAggregateOp::AggregateItem item;
                    item.func_def = ptr->func_def;
                    item.distinct = ptr->distinct;
                    item.result_type = ptr->return_type;
                    item.function_name = ptr->func_def->name;
                    for (auto& arg : ptr->args)
                        item.arguments.push_back(std::move(arg));
                    out_aggs.push_back(std::move(item));

                    // Replace this node with a BoundColumnRef pointing at the new
                    // aggregate's output column. Index = group_keys_size + position
                    // of this item in out_aggs (just pushed, so size-1).
                    std::string anon_name = "__agg_" + std::to_string(agg_idx++);
                    auto ret_type = out_aggs.back().result_type;
                    out_aggs.back().alias = anon_name;
                    uint32_t col_idx = static_cast<uint32_t>(group_keys_size + out_aggs.size() - 1);
                    expr = binder::BoundColumnRef{col_idx, std::move(ret_type), std::move(anon_name), INVALID_SLOT_ID};
                    return; // MUST return: ptr is now a dangling reference
                } else {
                    // Non-aggregate function: recurse into args.
                    for (auto& arg : ptr->args)
                        walkAndReplaceAggCalls(arg, out_aggs, agg_idx, group_keys_size);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                walkAndReplaceAggCalls(ptr->left, out_aggs, agg_idx, group_keys_size);
                walkAndReplaceAggCalls(ptr->right, out_aggs, agg_idx, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                walkAndReplaceAggCalls(ptr->operand, out_aggs, agg_idx, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (auto& elem : ptr->elements)
                    walkAndReplaceAggCalls(elem, out_aggs, agg_idx, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (auto& [k, v] : ptr->entries)
                    walkAndReplaceAggCalls(v, out_aggs, agg_idx, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                walkAndReplaceAggCalls(ptr->list_expr, out_aggs, agg_idx, group_keys_size);
                if (ptr->where_pred)
                    walkAndReplaceAggCalls(*ptr->where_pred, out_aggs, agg_idx, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                walkAndReplaceAggCalls(ptr->list_expr, out_aggs, agg_idx, group_keys_size);
                if (ptr->where_pred)
                    walkAndReplaceAggCalls(*ptr->where_pred, out_aggs, agg_idx, group_keys_size);
                walkAndReplaceAggCalls(ptr->projection, out_aggs, agg_idx, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                walkAndReplaceAggCalls(ptr->list, out_aggs, agg_idx, group_keys_size);
                walkAndReplaceAggCalls(ptr->index, out_aggs, agg_idx, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                walkAndReplaceAggCalls(ptr->list, out_aggs, agg_idx, group_keys_size);
                if (ptr->from)
                    walkAndReplaceAggCalls(*ptr->from, out_aggs, agg_idx, group_keys_size);
                if (ptr->to)
                    walkAndReplaceAggCalls(*ptr->to, out_aggs, agg_idx, group_keys_size);
            }
        },
        expr);
}

// ==================== SKIP/LIMIT Helper ====================

std::optional<int64_t> Binder::bindSkipLimit(const cypher::Expression& expr, const char* clause_name) {
    auto bound = bindExpression(expr);
    if (!bound)
        return std::nullopt;
    if (!std::holds_alternative<BoundLiteral>(*bound)) {
        error(std::string("SemanticError: ") + clause_name + " must be a constant expression");
        return std::nullopt;
    }
    auto& lit = std::get<BoundLiteral>(*bound);
    if (!std::holds_alternative<int64_t>(lit.value)) {
        error(std::string("SemanticError: ") + clause_name + " must be an integer");
        return std::nullopt;
    }
    auto val = std::get<int64_t>(lit.value);
    if (val < 0) {
        error(std::string("SemanticError: ") + clause_name + " must be a non-negative integer");
        return std::nullopt;
    }
    return val;
}

// ==================== RETURN Binding ====================

std::optional<BoundLogicalOperator> Binder::bindReturn(const cypher::ReturnClause& ret, BoundLogicalOperator child) {
    // Handle RETURN *: build projection items from all variables in scope
    // We cannot modify the const ret, so we build the projection inline
    // and then fall through to the normal bindReturn logic.
    // Instead, we build the items vector here and use it directly.
    if (ret.return_all && ret.items.empty()) {
        // Build a simple projection over all symbols, ordered by column index
        auto proj = std::make_unique<BoundProjectOp>();
        std::vector<const ColumnInfo*> sorted_symbols;
        for (const auto& [name, col_info] : ctx_.symbols) {
            // Skip anonymous internal edge variables (__anon_edge_N).
            if (name.starts_with("__anon_edge_"))
                continue;
            sorted_symbols.push_back(&col_info);
        }
        // VERTEX before EDGE before others (Neo4j convention for RETURN *).
        std::sort(sorted_symbols.begin(), sorted_symbols.end(), [](const ColumnInfo* a, const ColumnInfo* b) {
            auto typeRank = [](BoundTypeKind k) {
                if (k == BoundTypeKind::VERTEX)
                    return 0;
                if (k == BoundTypeKind::EDGE)
                    return 1;
                return 2;
            };
            int ra = typeRank(a->type.kind);
            int rb = typeRank(b->type.kind);
            if (ra != rb)
                return ra < rb;
            return a->column_index < b->column_index;
        });
        for (const auto* col_info : sorted_symbols) {
            auto bound_expr = std::make_optional<BoundExpression>(
                BoundColumnRef(col_info->column_index, col_info->type, col_info->name, col_info->slot_id));
            BoundProjectOp::ProjectItem proj_item;
            proj_item.expr = std::move(*bound_expr);
            proj_item.alias = col_info->name;
            proj_item.result_type = getBoundExprType(proj_item.expr);
            proj->items.push_back(std::move(proj_item));

            ColumnInfo out_info;
            out_info.name = col_info->name;
            out_info.type = proj_item.result_type;
            out_info.column_index = static_cast<uint32_t>(proj->items.size() - 1);
            ctx_.return_columns.push_back(std::move(out_info));
        }
        proj->child = std::move(child);

        BoundLogicalOperator current = std::move(proj);

        // ORDER BY
        if (ret.order_by) {
            auto sort = std::make_unique<BoundSortOp>();
            for (const auto& si : ret.order_by->items) {
                auto bound_key = bindExpression(si.expr);
                if (!bound_key)
                    continue;
                BoundSortOp::SortItem sort_item;
                sort_item.expr = std::move(*bound_key);
                sort_item.direction = si.direction;
                sort->items.push_back(std::move(sort_item));
            }
            sort->child = std::move(current);
            current = std::move(sort);
        }
        if (ret.skip) {
            auto count = bindSkipLimit(*ret.skip, "SKIP");
            if (!count)
                return std::nullopt;
            auto skip = std::make_unique<BoundSkipOp>();
            skip->count = *count;
            skip->child = std::move(current);
            current = std::move(skip);
        }
        if (ret.limit) {
            auto count = bindSkipLimit(*ret.limit, "LIMIT");
            if (!count)
                return std::nullopt;
            auto limit = std::make_unique<BoundLimitOp>();
            limit->count = *count;
            limit->child = std::move(current);
            current = std::move(limit);
        }
        if (ret.distinct) {
            auto distinct = std::make_unique<BoundDistinctOp>();
            distinct->child = std::move(current);
            current = std::move(distinct);
        }
        return current;
    }

    // Lower top-level list comprehensions that contain pattern comprehensions.
    // Their loop variable only exists inside the row-wise list comprehension,
    // so the ordinary PC hoisting pass (which runs at plan level) cannot
    // correlate on it.
    struct ReturnItemView {
        const cypher::Expression& expr;
        const std::optional<std::string>& alias;
    };
    std::vector<cypher::Expression> lowered_exprs;
    lowered_exprs.reserve(ret.items.size());
    std::vector<ReturnItemView> items;
    items.reserve(ret.items.size());
    for (const auto& item : ret.items) {
        auto* lc = std::get_if<std::unique_ptr<cypher::ListComprehension>>(&item.expr);
        std::vector<const cypher::PatternComprehension*> pc_asts;
        if (lc && *lc) {
            if ((*lc)->where_pred)
                collectPatternComprehensionsAST(*(*lc)->where_pred, pc_asts);
            if ((*lc)->projection)
                collectPatternComprehensionsAST(*(*lc)->projection, pc_asts);
        }
        if (!pc_asts.empty()) {
            SlotId out_slot = INVALID_SLOT_ID;
            std::string out_name;
            BoundType out_elem_type;
            if (!lowerListComprehensionWithPatternComprehension(**lc, child, out_slot, out_name, out_elem_type))
                return std::nullopt;
            auto var = std::make_unique<cypher::Variable>();
            var->name = out_name;
            lowered_exprs.push_back(cypher::Expression(std::move(var)));
            items.push_back({lowered_exprs.back(), item.alias});
        } else {
            items.push_back({item.expr, item.alias});
        }
    }

    // Check for aggregate functions in return items (recursively)
    bool has_aggregate = false;
    for (const auto& item : items) {
        if (hasAggregate(item.expr)) {
            has_aggregate = true;
            break;
        }
    }

    // ── Pattern comprehension hoisting ──
    // Stack one BoundPatternComprehensionApplyOp per PatternComprehension AST
    // node above `child` so each list result is precomputed as a column before
    // any projection / aggregate sees it. The patch_map lets later-bound
    // projection expressions rewrite their BoundPatternComprehension
    // placeholders into BoundColumnRef via column_rewrite.cpp.
    std::unordered_map<const cypher::PatternComprehension*, std::tuple<binder::SlotId, std::string, binder::BoundType>>
        pc_patch_map;
    {
        std::vector<const cypher::PatternComprehension*> pc_asts;
        for (const auto& item : items)
            collectPatternComprehensionsAST(item.expr, pc_asts);
    }
    if (!hoistPatternComprehensions(*this, child, items, ret.order_by ? &ret.order_by->items : nullptr, pc_patch_map)) {
        return std::nullopt;
    }

    if (has_aggregate) {
        // ── Aggregate + Project strategy ──
        // AggregateOp handles only simple aggregate functions, outputting them
        // as anonymous columns (__agg_0, __agg_1...). Complex expressions like
        // count(a)+3 are split: the aggregate goes to AggregateOp as an anonymous
        // column, and the scalar part (+3) is handled by a downstream ProjectOp.
        auto agg = std::make_unique<BoundAggregateOp>();

        // Items for the downstream ProjectOp — one per user-visible RETURN
        // item (group keys, simple aggregates, complex aggregate expressions).
        // A ProjectOp is ALWAYS built above the AggregateOp so that the
        // post-Aggregate ProjectionExtract's appended `__pe_*` columns (used to
        // construct graph-variable objects for whole-object demand) do not
        // leak into the user-visible output. The Project's items redirect
        // graph-variable refs to their `__pe_*` slots via the column-rewrite
        // pass.
        struct ProjItem {
            binder::BoundExpression expr;
            std::string alias;
        };
        std::vector<ProjItem> proj_items;

        // Column index tracker for anonymous aggregate columns.
        uint32_t anon_idx = 0;

        for (const auto& item : items) {
            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            // "Simple" aggregate = the entire item is a single top-level aggregate call
            // (e.g. `count(*)`, `collect(x)`). Expressions like `size(collect(x))` are
            // NOT simple — size() is scalar, collect() is the inner aggregate, so they
            // must go through walkAndReplaceAggCalls to be split into an internal
            // aggregate column + a ProjectOp expression.
            bool is_simple_agg = false;
            if (auto* fc = std::get_if<std::unique_ptr<cypher::FunctionCall>>(&item.expr)) {
                if (fc && *fc && isAggregateFunctionName((*fc)->name))
                    is_simple_agg = true;
            }

            auto bound_expr = bindExpression(item.expr);
            if (!bound_expr)
                continue;
            patchPatternComprehensionPlaceholders(*bound_expr, pc_patch_map);

            if (is_simple_agg) {
                // Top-level aggregate function: e.g., RETURN count(a).
                BoundAggregateOp::AggregateItem agg_item;
                agg_item.alias = alias;
                if (std::holds_alternative<std::unique_ptr<BoundFunctionCall>>(*bound_expr)) {
                    auto& bfc = std::get<std::unique_ptr<BoundFunctionCall>>(*bound_expr);
                    agg_item.func_def = bfc->func_def;
                    agg_item.function_name = bfc->func_def->name;
                    agg_item.distinct = bfc->distinct;
                    agg_item.result_type = bfc->return_type;
                    for (auto& arg : bfc->args)
                        agg_item.arguments.push_back(std::move(arg));
                }
                agg->aggregates.push_back(std::move(agg_item));
                agg->output_names.push_back(alias);

                ColumnInfo info;
                info.name = alias;
                info.type = BoundType::clone(agg->aggregates.back().result_type);
                info.column_index = static_cast<uint32_t>(agg->group_keys.size() + agg->aggregates.size() - 1);
                // Allocate (or reuse) a slot_id so the passthrough
                // ProjectItem's BoundColumnRef resolves against the physical
                // TupleSlotLayout. Without this, the slot defaults to
                // INVALID_SLOT_ID and ExpressionCompiler cannot map it to a
                // column index — the binder's stale column_index then points
                // at the wrong column.
                auto existing = ctx_.symbols.find(alias);
                if (existing != ctx_.symbols.end() && existing->second.slot_id != INVALID_SLOT_ID)
                    info.slot_id = existing->second.slot_id;
                else
                    info.slot_id = allocateNamedSlot(alias);
                ctx_.symbols[alias] = std::move(info);

                // Re-expose the simple aggregate as a passthrough ProjectItem
                // so it survives the top-level projection. Without this, the
                // top-level ProjectOp would drop the simple aggregate column.
                binder::BoundExpression passthrough =
                    binder::BoundColumnRef{ctx_.symbols[alias].column_index, BoundType::clone(ctx_.symbols[alias].type),
                                           alias, ctx_.symbols[alias].slot_id};
                proj_items.push_back({std::move(passthrough), alias});
            } else if (hasAggregate(item.expr)) {
                // Complex expression containing aggregates: e.g., RETURN count(a) + 3.
                // Walk the bound expression, extract each aggregate call into
                // AggregateOp as an anonymous column, and replace the call in the
                // expression tree with a BoundVariableRef to that column.
                auto full_expr = std::move(*bound_expr);
                walkAndReplaceAggCalls(full_expr, agg->aggregates, anon_idx, agg->group_keys.size());

                // Register anonymous aggregate columns in the symbol table
                // so the ProjectOp and ColumnResolver can find them.
                for (size_t i = 0; i < agg->aggregates.size(); ++i) {
                    auto& ai = agg->aggregates[i];
                    if (ai.alias.starts_with("__agg_")) {
                        if (ctx_.symbols.find(ai.alias) == ctx_.symbols.end()) {
                            ColumnInfo info;
                            info.name = ai.alias;
                            info.type = BoundType::clone(ai.result_type);
                            info.column_index = static_cast<uint32_t>(agg->group_keys.size() + i);
                            ctx_.symbols[ai.alias] = std::move(info);
                        }
                        agg->output_names.push_back(ai.alias);
                    }
                }

                proj_items.push_back({std::move(full_expr), alias});
            } else {
                // Group key: non-aggregate expression.
                agg->group_keys.push_back(std::move(*bound_expr));
                agg->output_names.push_back(alias);

                ColumnInfo info;
                info.name = alias;
                info.type = getBoundExprType(agg->group_keys.back());
                info.column_index = static_cast<uint32_t>(agg->group_keys.size() - 1);
                // Preserve the existing slot_id when the alias matches an
                // already-bound variable (e.g. group key `a` reuses the slot
                // from MATCH). Otherwise allocate a fresh slot so downstream
                // refs resolve correctly through the TupleSlotLayout.
                auto existing = ctx_.symbols.find(alias);
                if (existing != ctx_.symbols.end() && existing->second.slot_id != INVALID_SLOT_ID) {
                    info.slot_id = existing->second.slot_id;
                    info.source_labels = existing->second.source_labels;
                    info.source_prop_id = existing->second.source_prop_id;
                    info.strong_typed = existing->second.strong_typed;
                } else {
                    info.slot_id = allocateNamedSlot(alias);
                }
                ctx_.symbols[alias] = info;

                // Re-expose the group key as a passthrough ProjectItem so it
                // survives the top-level projection. When the group key is a
                // graph variable (VERTEX/EDGE), the ProjectItem's BoundColumnRef
                // is redirected to the `__pe_*` slot by the column-rewrite pass
                // so the user-visible value is the constructed VertexValue /
                // EdgeValue rather than the topology-stage reference.
                binder::BoundExpression passthrough =
                    binder::BoundColumnRef{info.column_index, BoundType::clone(info.type), alias, info.slot_id};
                proj_items.push_back({std::move(passthrough), alias});
            }
        }

        agg->child = std::move(child);

        // Register all AggregateOp columns (group keys + aggregates) in symbol table.
        for (size_t i = 0; i < agg->output_names.size(); ++i) {
            if (ctx_.symbols.find(agg->output_names[i]) == ctx_.symbols.end()) {
                ColumnInfo info;
                info.name = agg->output_names[i];
                info.column_index = static_cast<uint32_t>(i);
                // Determine type: group keys first, then aggregates.
                if (i < agg->group_keys.size())
                    info.type = getBoundExprType(agg->group_keys[i]);
                else
                    info.type = BoundType::clone(agg->aggregates[i - agg->group_keys.size()].result_type);
                ctx_.symbols[agg->output_names[i]] = std::move(info);
            }
        }

        // Always build a ProjectOp above AggregateOp. The Project's items are
        // the user-visible RETURN items (group keys + simple aggregates +
        // complex aggregate expressions). Internal `__agg_*` columns and
        // post-Aggregate PE's `__pe_*` columns are filtered out by not being
        // referenced in any ProjectItem.
        BoundLogicalOperator current;
        auto proj = std::make_unique<BoundProjectOp>();
        for (auto& pi : proj_items) {
            BoundProjectOp::ProjectItem pi_item;
            pi_item.expr = std::move(pi.expr);
            pi_item.alias = std::move(pi.alias);
            pi_item.result_type = getBoundExprType(pi_item.expr);
            proj->items.push_back(std::move(pi_item));

            const auto& pushed = proj->items.back();
            ColumnInfo out_info;
            out_info.name = pushed.alias;
            out_info.type = pushed.result_type;
            out_info.column_index = static_cast<uint32_t>(proj->items.size() - 1);
            ctx_.return_columns.push_back(out_info);

            // Register the alias in the binder symbol table pointing at the
            // ProjectOp's output column. Without this, downstream clauses
            // (Sort / Skip / Limit / DISTINCT) that reference the alias (e.g.,
            // `RETURN count(a)+1 AS x ORDER BY x`) cannot resolve it — the
            // complex-aggregate branch above only registers internal `__agg_*`
            // columns. For simple aggregates the entry already exists pointing
            // at the Aggregate column index, which equals the ProjectOp column
            // index (passthrough), so overwriting is a no-op there.
            auto existing = ctx_.symbols.find(pushed.alias);
            if (existing != ctx_.symbols.end() && existing->second.slot_id != INVALID_SLOT_ID) {
                existing->second.column_index = out_info.column_index;
                existing->second.type = BoundType::clone(out_info.type);
            } else {
                ColumnInfo sym_info;
                sym_info.name = pushed.alias;
                sym_info.type = BoundType::clone(out_info.type);
                sym_info.column_index = out_info.column_index;
                sym_info.slot_id = allocateNamedSlot(pushed.alias);
                ctx_.symbols[pushed.alias] = std::move(sym_info);
            }
        }
        proj->child = std::move(agg);
        current = std::move(proj);

        // ORDER BY, SKIP, LIMIT, DISTINCT

        if (ret.order_by) {
            // Validate ORDER BY for aggregating RETURN (same rules as bindWith).
            std::set<std::string> projection_aggs;
            std::set<std::string> grouping_key_exprs;
            std::set<std::string> projected_names;
            for (const auto& item : items) {
                std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
                projected_names.insert(alias);
                if (hasAggregate(item.expr))
                    projection_aggs.insert(cypher::expressionToString(item.expr));
                else
                    grouping_key_exprs.insert(cypher::expressionToString(item.expr));
            }
            for (const auto& si : ret.order_by->items) {
                if (hasAggregate(si.expr) && isAmbiguousAggregationExpr(si.expr)) {
                    error("SyntaxError: AmbiguousAggregationExpression: ORDER BY expression mixes "
                          "aggregate and non-aggregate operations");
                    return std::nullopt;
                }
                std::string err_var;
                if (!validateAggOrderByExpr(si.expr, projection_aggs, grouping_key_exprs, projected_names, err_var)) {
                    error("SyntaxError: UndefinedVariable: Variable '" + err_var +
                          "' not defined in aggregating RETURN scope");
                    return std::nullopt;
                }
            }

            auto sort = std::make_unique<BoundSortOp>();
            for (const auto& si : ret.order_by->items) {
                // Sort sits above the top-level Project, so its input is the
                // Project's output (group keys + aggregates). Re-binding the
                // raw AST expression (e.g. `max(n.age)`) would create a fresh
                // aggregate that the runtime evaluator cannot compute on
                // post-aggregate rows. Resolve ORDER BY against the Project's
                // output columns instead: if the expression matches a RETURN
                // item (by AST string), emit a BoundColumnRef to that column.
                std::string ob_key_str = cypher::expressionToString(si.expr);
                std::optional<BoundExpression> resolved;
                for (size_t i = 0; i < items.size(); ++i) {
                    std::string item_str = cypher::expressionToString(items[i].expr);
                    bool match = (item_str == ob_key_str);
                    if (!match && items[i].alias && *items[i].alias == ob_key_str)
                        match = true;
                    if (!match)
                        continue;
                    std::string alias = items[i].alias ? *items[i].alias : item_str;
                    auto sym_it = ctx_.symbols.find(alias);
                    if (sym_it != ctx_.symbols.end()) {
                        const auto& info = sym_it->second;
                        resolved = BoundColumnRef{info.column_index, BoundType::clone(info.type), alias, info.slot_id};
                    }
                    break;
                }
                if (resolved) {
                    BoundSortOp::SortItem sort_item;
                    sort_item.expr = std::move(*resolved);
                    sort_item.direction = si.direction;
                    sort->items.push_back(std::move(sort_item));
                } else {
                    auto bound_key = bindExpression(si.expr);
                    if (!bound_key)
                        continue;
                    BoundSortOp::SortItem sort_item;
                    sort_item.expr = std::move(*bound_key);
                    sort_item.direction = si.direction;
                    sort->items.push_back(std::move(sort_item));
                }
            }
            sort->child = std::move(current);
            current = std::move(sort);
        }
        if (ret.skip) {
            auto count = bindSkipLimit(*ret.skip, "SKIP");
            if (!count)
                return std::nullopt;
            auto skip = std::make_unique<BoundSkipOp>();
            skip->count = *count;
            skip->child = std::move(current);
            current = std::move(skip);
        }
        if (ret.limit) {
            auto count = bindSkipLimit(*ret.limit, "LIMIT");
            if (!count)
                return std::nullopt;
            auto limit = std::make_unique<BoundLimitOp>();
            limit->count = *count;
            limit->child = std::move(current);
            current = std::move(limit);
        }
        if (ret.distinct) {
            auto distinct = std::make_unique<BoundDistinctOp>();
            distinct->child = std::move(current);
            current = std::move(distinct);
        }
        return current;
    }

    // Non-aggregate: simple projection
    auto proj = std::make_unique<BoundProjectOp>();
    for (const auto& item : items) {
        auto bound_expr = bindExpression(item.expr);
        if (!bound_expr)
            continue;
        patchPatternComprehensionPlaceholders(*bound_expr, pc_patch_map);

        // Detect whole-variable return (e.g., RETURN n) to add all property requirements
        if (std::holds_alternative<BoundColumnRef>(*bound_expr)) {
            auto& col_ref = std::get<BoundColumnRef>(*bound_expr);
            if (col_ref.type.kind == BoundTypeKind::VERTEX) {
                addAllPropertiesForVariable(col_ref.name);
            }
        } else if (std::holds_alternative<std::unique_ptr<BoundLabelCast>>(*bound_expr)) {
            auto& lc = std::get<std::unique_ptr<BoundLabelCast>>(*bound_expr);
            if (std::holds_alternative<BoundColumnRef>(lc->object)) {
                auto var_name = std::get<BoundColumnRef>(lc->object).name;
                const auto* ldef = catalog_.lookupLabel(lc->label_id);
                if (ldef) {
                    for (const auto& pd : ldef->properties)
                        ctx_.addPropertyRequirement(var_name, lc->label_id, pd.id);
                }
            }
        }

        std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
        BoundProjectOp::ProjectItem proj_item;
        proj_item.expr = std::move(*bound_expr);
        proj_item.alias = std::move(alias);
        proj_item.result_type = getBoundExprType(proj_item.expr);
        proj->items.push_back(std::move(proj_item));
    }
    proj->child = std::move(child);

    // Register RETURN aliases so ORDER BY can reference them.
    // Build alias → original expression map for ORDER BY resolution.
    // (Sort is placed before projection, so aliases need to be resolved
    // to the original expression, not the projected column index.)
    std::unordered_map<std::string, size_t> alias_to_proj_idx;
    for (size_t i = 0; i < proj->items.size(); ++i) {
        const auto& proj_item = proj->items[i];
        alias_to_proj_idx[proj_item.alias] = i;
        if (ctx_.symbols.find(proj_item.alias) == ctx_.symbols.end()) {
            ColumnInfo info;
            info.name = proj_item.alias;
            info.type = proj_item.result_type;
            info.column_index = static_cast<uint32_t>(i);
            ctx_.symbols[proj_item.alias] = std::move(info);
        }
        // Populate return_columns for output_schema
        ColumnInfo out_info;
        out_info.name = proj_item.alias;
        out_info.type = proj_item.result_type;
        out_info.column_index = static_cast<uint32_t>(i);
        ctx_.return_columns.push_back(std::move(out_info));
    }

    // Build child pipeline: sort before projection so ORDER BY can reference
    // original columns (e.g., r.id where r is an EdgeValue from MATCH).
    BoundLogicalOperator child_op = std::move(proj->child);

    // ORDER BY
    if (ret.order_by) {
        auto sort = std::make_unique<BoundSortOp>();
        for (const auto& si : ret.order_by->items) {
            // If the ORDER BY expression is a simple variable that matches a
            // RETURN alias, re-bind the original RETURN expression.
            auto* var = std::get_if<std::unique_ptr<cypher::Variable>>(&si.expr);
            if (var && *var && alias_to_proj_idx.count((*var)->name)) {
                size_t idx = alias_to_proj_idx[(*var)->name];
                // Re-bind the original expression from the RETURN item.
                // This works because the child columns are still available
                // (sort is before projection).
                auto bound_key = bindExpression(items[idx].expr);
                if (bound_key) {
                    BoundSortOp::SortItem sort_item;
                    sort_item.expr = std::move(*bound_key);
                    sort_item.direction = si.direction;
                    sort->items.push_back(std::move(sort_item));
                }
            } else {
                auto bound_key = bindExpression(si.expr);
                if (!bound_key)
                    continue;
                BoundSortOp::SortItem sort_item;
                sort_item.expr = std::move(*bound_key);
                sort_item.direction = si.direction;
                sort->items.push_back(std::move(sort_item));
            }
        }
        sort->child = std::move(child_op);
        child_op = std::move(sort);
    }

    // Projection after sort
    proj->child = std::move(child_op);
    BoundLogicalOperator current = std::move(proj);

    // SKIP, LIMIT, DISTINCT
    if (ret.skip) {
        auto count = bindSkipLimit(*ret.skip, "SKIP");
        if (!count)
            return std::nullopt;
        auto skip = std::make_unique<BoundSkipOp>();
        skip->count = *count;
        skip->child = std::move(current);
        current = std::move(skip);
    }
    if (ret.limit) {
        auto count = bindSkipLimit(*ret.limit, "LIMIT");
        if (!count)
            return std::nullopt;
        auto limit = std::make_unique<BoundLimitOp>();
        limit->count = *count;
        limit->child = std::move(current);
        current = std::move(limit);
    }
    if (ret.distinct) {
        auto distinct = std::make_unique<BoundDistinctOp>();
        distinct->child = std::move(current);
        current = std::move(distinct);
    }

    return current;
}

// ==================== WITH Binding ====================

std::optional<BoundLogicalOperator> Binder::bindWith(const cypher::WithClause& wc, BoundLogicalOperator child) {
    // Same top-level list-comprehension lowering as RETURN.
    struct ReturnItemView {
        const cypher::Expression& expr;
        const std::optional<std::string>& alias;
    };
    std::vector<cypher::Expression> lowered_exprs;
    lowered_exprs.reserve(wc.items.size());
    std::vector<ReturnItemView> items;
    items.reserve(wc.items.size());
    for (const auto& item : wc.items) {
        auto* lc = std::get_if<std::unique_ptr<cypher::ListComprehension>>(&item.expr);
        std::vector<const cypher::PatternComprehension*> pc_asts;
        if (lc && *lc) {
            if ((*lc)->where_pred)
                collectPatternComprehensionsAST(*(*lc)->where_pred, pc_asts);
            if ((*lc)->projection)
                collectPatternComprehensionsAST(*(*lc)->projection, pc_asts);
        }
        if (!pc_asts.empty()) {
            SlotId out_slot = INVALID_SLOT_ID;
            std::string out_name;
            BoundType out_elem_type;
            if (!lowerListComprehensionWithPatternComprehension(**lc, child, out_slot, out_name, out_elem_type))
                return std::nullopt;
            auto var = std::make_unique<cypher::Variable>();
            var->name = out_name;
            lowered_exprs.push_back(cypher::Expression(std::move(var)));
            items.push_back({lowered_exprs.back(), item.alias});
        } else {
            items.push_back({item.expr, item.alias});
        }
    }

    // Check for aggregate functions in WITH items (recursively)
    bool has_aggregate = false;
    for (const auto& item : items) {
        if (hasAggregate(item.expr)) {
            has_aggregate = true;
            break;
        }
    }

    // ── Pattern comprehension hoisting ── (see bindReturn for rationale)
    std::unordered_map<const cypher::PatternComprehension*, std::tuple<binder::SlotId, std::string, binder::BoundType>>
        wc_pc_patch_map;
    if (!hoistPatternComprehensions(*this, child, items, wc.order_by ? &wc.order_by->items : nullptr,
                                    wc_pc_patch_map)) {
        return std::nullopt;
    }

    // Tracks whether WHERE references a projected variable; used to decide
    // filter placement (before vs after projection).
    bool where_has_projected_ref = false;

    // Collect output names and types for scope reset
    std::vector<std::pair<std::string, BoundType>> with_outputs;

    // ── Semantic checks ──

    // Check for duplicate aliases (applies to both aggregating and non-aggregating WITH).
    {
        std::set<std::string> seen;
        for (const auto& item : items) {
            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            if (!seen.insert(alias).second) {
                error("SyntaxError: ColumnNameConflict: duplicate column alias '" + alias + "' in WITH");
                return std::nullopt;
            }
        }
    }

    BoundLogicalOperator current;

    if (has_aggregate) {
        // Build the set of grouping key expressions (items without aggregate).
        std::set<std::string> grouping_key_strs;
        for (const auto& item : items) {
            if (!hasAggregate(item.expr))
                grouping_key_strs.insert(cypher::expressionToString(item.expr));
        }

        // Check for AmbiguousAggregationExpression (With6 [8][9]):
        // expressions that mix aggregate with non-aggregate operands
        // that are NOT already established as grouping keys.
        for (const auto& item : items) {
            if (auto* fc = std::get_if<std::unique_ptr<cypher::FunctionCall>>(&item.expr)) {
                if (fc && *fc && isAggregateFunctionName((*fc)->name))
                    continue; // simple aggregate — ok
            }
            // List comprehension wraps its internal aggregate — valid.
            if (std::holds_alternative<std::unique_ptr<cypher::ListComprehension>>(item.expr))
                continue;
            if (hasAggregate(item.expr)) {
                // Gather all variables in the expression, then check whether
                // any non-aggregate-path variable is NOT a grouping key.
                // Variables that appear only inside aggregate function
                // arguments are fine (they are aggregated).
                std::set<std::string> non_agg_vars;
                collectNonAggregateVariables(item.expr, non_agg_vars, grouping_key_strs);
                // Check each non-aggregate variable against grouping keys.
                for (const auto& v : non_agg_vars) {
                    if (!grouping_key_strs.count(v)) {
                        error("SyntaxError: AmbiguousAggregationExpression: expression mixes "
                              "aggregate and non-aggregate operations");
                        return std::nullopt;
                    }
                }
            }
        }

        auto agg = std::make_unique<BoundAggregateOp>();

        struct ProjItem {
            binder::BoundExpression expr;
            std::string alias;
        };
        std::vector<ProjItem> proj_items;
        uint32_t anon_idx = 0;

        // First pass: detect whether any item is a *complex* aggregate so we
        // know whether to build a ProjectOp above the AggregateOp. See
        // bindReturn for the rationale.
        bool has_complex_agg = false;
        for (const auto& item : items) {
            if (auto* fc = std::get_if<std::unique_ptr<cypher::FunctionCall>>(&item.expr)) {
                if (fc && *fc && isAggregateFunctionName((*fc)->name))
                    continue;
            }
            if (hasAggregate(item.expr)) {
                has_complex_agg = true;
                break;
            }
        }

        for (const auto& item : items) {
            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            // "Simple" aggregate = the entire item is a single top-level aggregate call.
            // See bindReturn for the rationale.
            bool is_simple_agg = false;
            if (auto* fc = std::get_if<std::unique_ptr<cypher::FunctionCall>>(&item.expr)) {
                if (fc && *fc && isAggregateFunctionName((*fc)->name))
                    is_simple_agg = true;
            }

            auto bound_expr = bindExpression(item.expr);
            if (!bound_expr)
                continue;
            patchPatternComprehensionPlaceholders(*bound_expr, wc_pc_patch_map);

            if (is_simple_agg) {
                BoundAggregateOp::AggregateItem agg_item;
                agg_item.alias = alias;
                if (std::holds_alternative<std::unique_ptr<BoundFunctionCall>>(*bound_expr)) {
                    auto& fc = std::get<std::unique_ptr<BoundFunctionCall>>(*bound_expr);
                    agg_item.func_def = fc->func_def;
                    agg_item.function_name = fc->func_def->name;
                    agg_item.distinct = fc->distinct;
                    agg_item.result_type = fc->return_type;
                    for (auto& arg : fc->args)
                        agg_item.arguments.push_back(std::move(arg));
                }
                agg->aggregates.push_back(std::move(agg_item));
                agg->output_names.push_back(alias);
                uint32_t col_idx = static_cast<uint32_t>(agg->group_keys.size() + agg->aggregates.size() - 1);
                with_outputs.emplace_back(alias, BoundType::clone(agg->aggregates.back().result_type));

                // Only build a passthrough ProjectItem when there is a sibling
                // complex aggregate forcing the ProjectOp path; otherwise the
                // AggregateOp output is the final result and adding a
                // passthrough would drop group keys from the schema.
                if (has_complex_agg) {
                    binder::BoundExpression passthrough = binder::BoundColumnRef{
                        col_idx, BoundType::clone(agg->aggregates.back().result_type), alias, INVALID_SLOT_ID};
                    proj_items.push_back({std::move(passthrough), alias});
                }
            } else if (hasAggregate(item.expr)) {
                auto full_expr = std::move(*bound_expr);
                walkAndReplaceAggCalls(full_expr, agg->aggregates, anon_idx, agg->group_keys.size());

                for (auto& ai : agg->aggregates) {
                    if (ai.alias.starts_with("__agg_")) {
                        agg->output_names.push_back(ai.alias);
                        with_outputs.emplace_back(ai.alias, BoundType::clone(ai.result_type));
                    }
                }
                proj_items.push_back({std::move(full_expr), alias});
            } else {
                agg->group_keys.push_back(std::move(*bound_expr));
                agg->output_names.push_back(alias);
                with_outputs.emplace_back(alias, getBoundExprType(agg->group_keys.back()));
            }
        }

        // WHERE on WITH: if it references projected (aggregate-output) variables,
        // it must be placed AFTER the aggregation. Otherwise (pure old-scope refs),
        // bind and insert it BEFORE the aggregation for efficiency.
        where_has_projected_ref = false;
        if (wc.where_pred) {
            for (const auto& item : items) {
                std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
                if (expressionReferencesVariable(*wc.where_pred, alias)) {
                    where_has_projected_ref = true;
                    break;
                }
            }
        }
        if (wc.where_pred && !where_has_projected_ref) {
            auto where_op = bindWhere(*wc.where_pred, std::move(child));
            if (!where_op)
                return std::nullopt;
            child = std::move(*where_op);
        }

        agg->child = std::move(child);

        if (!proj_items.empty()) {
            // ProjectOp output names are the proj_item aliases — the user-facing
            // names (e.g. `p` for `WITH [x IN collect(p) | ...] AS p`).
            // The internal __agg_* names belong to the AggregateOp below; downstream
            // clauses must NOT see them. Rebuild with_outputs to mirror ProjectOp.
            with_outputs.clear();
            auto proj = std::make_unique<BoundProjectOp>();
            for (auto& pi : proj_items) {
                BoundProjectOp::ProjectItem proj_item;
                proj_item.expr = std::move(pi.expr);
                proj_item.alias = std::move(pi.alias);
                proj_item.result_type = getBoundExprType(proj_item.expr);
                with_outputs.emplace_back(proj_item.alias, proj_item.result_type);
                proj->items.push_back(std::move(proj_item));
            }
            proj->child = std::move(agg);
            current = std::move(proj);
        } else {
            current = std::move(agg);
        }
    } else {
        // Check that expressions need explicit aliases (With4 [5]).
        for (const auto& item : items) {
            if (item.alias)
                continue;
            if (std::holds_alternative<std::unique_ptr<cypher::Variable>>(item.expr))
                continue;
            error("SyntaxError: NoExpressionAlias: expression in WITH must be aliased "
                  "(use AS <name>)");
            return std::nullopt;
        }

        // Compute projected aliases upfront (without binding expressions,
        // just extracting alias names from the AST) so we can check whether
        // WHERE references them.
        where_has_projected_ref = false;
        if (wc.where_pred) {
            for (const auto& item : items) {
                std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
                if (expressionReferencesVariable(*wc.where_pred, alias)) {
                    where_has_projected_ref = true;
                    break;
                }
            }
        }

        // If WHERE only uses old-scope variables, bind and insert it BEFORE
        // the projection (e.g. WITH types[i] AS x WHERE i <> j).
        if (wc.where_pred && !where_has_projected_ref) {
            auto where_op = bindWhere(*wc.where_pred, std::move(child));
            if (!where_op)
                return std::nullopt;
            child = std::move(*where_op);
        }

        // Simple projection
        auto proj = std::make_unique<BoundProjectOp>();
        if (wc.return_all && wc.items.empty()) {
            // WITH *: pass through all variables from current scope.
            // Mirrors bindReturn's RETURN * handler (line 220-242).
            std::vector<const ColumnInfo*> sorted_symbols;
            for (const auto& [name, col_info] : ctx_.symbols) {
                if (name.starts_with("__anon_edge_"))
                    continue;
                sorted_symbols.push_back(&col_info);
            }
            // VERTEX before EDGE before others (Neo4j convention for WITH *).
            std::sort(sorted_symbols.begin(), sorted_symbols.end(), [](const ColumnInfo* a, const ColumnInfo* b) {
                auto typeRank = [](BoundTypeKind k) {
                    if (k == BoundTypeKind::VERTEX)
                        return 0;
                    if (k == BoundTypeKind::EDGE)
                        return 1;
                    return 2;
                };
                int ra = typeRank(a->type.kind);
                int rb = typeRank(b->type.kind);
                if (ra != rb)
                    return ra < rb;
                return a->column_index < b->column_index;
            });
            for (const auto* col_info : sorted_symbols) {
                auto bound_expr =
                    BoundColumnRef(col_info->column_index, col_info->type, col_info->name, col_info->slot_id);
                BoundProjectOp::ProjectItem proj_item;
                proj_item.expr = std::move(bound_expr);
                proj_item.alias = col_info->name;
                proj_item.result_type = getBoundExprType(proj_item.expr);
                proj_item.input_slot = col_info->slot_id;
                proj->items.push_back(std::move(proj_item));
                with_outputs.emplace_back(col_info->name, proj_item.result_type);
            }
        } else
            for (const auto& item : items) {
                auto bound_expr = bindExpression(item.expr);
                if (!bound_expr)
                    continue;
                patchPatternComprehensionPlaceholders(*bound_expr, wc_pc_patch_map);

                // Detect whole-variable pass-through to add property requirements
                if (std::holds_alternative<BoundColumnRef>(*bound_expr)) {
                    auto& col_ref = std::get<BoundColumnRef>(*bound_expr);
                    if (col_ref.type.kind == BoundTypeKind::VERTEX) {
                        addAllPropertiesForVariable(col_ref.name);
                    }
                }

                std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
                BoundProjectOp::ProjectItem proj_item;
                proj_item.expr = std::move(*bound_expr);
                proj_item.alias = alias;
                proj_item.result_type = getBoundExprType(proj_item.expr);
                proj->items.push_back(std::move(proj_item));
                with_outputs.emplace_back(alias, proj_item.result_type);
            }

        // ORDER BY for non-aggregating WITH: bind Sort expressions against
        // the input scope (ctx_.symbols still holds the previous scope here)
        // and place Sort BEFORE Project so ORDER BY can reference variables
        // not included in the projection.
        // Projected-alias references are transparently substituted by
        // populating order_by_alias_subs_ (see bind_expression.cpp).
        if (wc.order_by) {
            // Reject aggregate functions in ORDER BY of non-aggregating WITH.
            for (const auto& si : wc.order_by->items) {
                if (hasAggregate(si.expr)) {
                    error("SyntaxError: InvalidAggregation: Cannot use aggregate functions in "
                          "ORDER BY of a non-aggregating WITH clause");
                    return std::nullopt;
                }
            }
            // Populate alias substitutions so bindExpression re-binds projected
            // aliases to their original expressions (resolving against input scope).
            order_by_alias_subs_.clear();
            if (!wc.return_all) {
                for (const auto& item : items) {
                    std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
                    order_by_alias_subs_[alias] = &item.expr;
                }
            }
            auto sort = std::make_unique<BoundSortOp>();
            for (const auto& si : wc.order_by->items) {
                auto bound_key = bindExpression(si.expr);
                if (!bound_key)
                    continue;
                BoundSortOp::SortItem sort_item;
                sort_item.expr = std::move(*bound_key);
                sort_item.direction = si.direction;
                sort->items.push_back(std::move(sort_item));
            }
            order_by_alias_subs_.clear();
            sort->child = std::move(child);
            proj->child = std::move(sort);
        } else {
            proj->child = std::move(child);
        }
        current = std::move(proj);
    }

    // Register WITH aliases temporarily so ORDER BY / SKIP / LIMIT can resolve them
    // AND so that post-projection WHERE can reference projected variables.
    // Preserve metadata (source_label, etc.) from any prior registration so that
    // downstream SET/REMOVE can resolve property IDs.
    for (size_t i = 0; i < with_outputs.size(); ++i) {
        ColumnInfo info;
        info.name = with_outputs[i].first;
        info.type = BoundType::clone(with_outputs[i].second);
        info.column_index = static_cast<uint32_t>(i);
        // Allocate (or reuse) a slot_id so downstream BoundColumnRef.slot_id
        // resolves against the physical TupleSlotLayout. Without this, the
        // slot defaults to INVALID_SLOT_ID and ExpressionCompiler cannot map
        // it to a column index — the binder's stale column_index then points
        // at the wrong column.
        auto existing = ctx_.symbols.find(with_outputs[i].first);
        if (existing != ctx_.symbols.end()) {
            info.source_labels = existing->second.source_labels;
            info.source_prop_id = existing->second.source_prop_id;
            info.strong_typed = existing->second.strong_typed;
            if (existing->second.slot_id != INVALID_SLOT_ID)
                info.slot_id = existing->second.slot_id;
            else
                info.slot_id = allocateNamedSlot(with_outputs[i].first);
        } else {
            info.slot_id = allocateNamedSlot(with_outputs[i].first);
        }
        ctx_.symbols[with_outputs[i].first] = std::move(info);
    }

    // WHERE that references projected variables (e.g. WITH x+1 AS y WHERE y > 0)
    // must be placed AFTER the projection so projected columns are visible.
    if (where_has_projected_ref && wc.where_pred) {
        auto where_op = bindWhere(*wc.where_pred, std::move(current));
        if (!where_op)
            return std::nullopt;
        current = std::move(*where_op);
    }

    // ORDER BY for aggregating WITH.  (Non-aggregating WITH handles ORDER BY
    // inside the projection branch above, placing Sort before Project so that
    // input-scope variables remain visible.)
    if (wc.order_by && has_aggregate) {
        // Build validation sets from the projection.
        std::set<std::string> projection_aggs;
        std::set<std::string> grouping_key_exprs;
        std::set<std::string> projected_names;
        for (const auto& item : items) {
            std::string alias = item.alias ? *item.alias : cypher::expressionToString(item.expr);
            projected_names.insert(alias);
            if (hasAggregate(item.expr)) {
                projection_aggs.insert(cypher::expressionToString(item.expr));
            } else {
                grouping_key_exprs.insert(cypher::expressionToString(item.expr));
            }
        }

        for (const auto& si : wc.order_by->items) {
            // [20] AmbiguousAggregationExpression
            if (hasAggregate(si.expr) && isAmbiguousAggregationExpr(si.expr)) {
                error("SyntaxError: AmbiguousAggregationExpression: ORDER BY expression mixes "
                      "aggregate and non-aggregate operations");
                return std::nullopt;
            }
            // [13][14][19] UndefinedVariable
            std::string err_var;
            if (!validateAggOrderByExpr(si.expr, projection_aggs, grouping_key_exprs, projected_names, err_var)) {
                error("SyntaxError: UndefinedVariable: Variable '" + err_var +
                      "' not defined in "
                      "aggregating WITH scope");
                return std::nullopt;
            }
        }

        auto sort = std::make_unique<BoundSortOp>();
        for (const auto& si : wc.order_by->items) {
            auto bound_key = bindExpression(si.expr);
            if (!bound_key)
                continue;
            BoundSortOp::SortItem sort_item;
            sort_item.expr = std::move(*bound_key);
            sort_item.direction = si.direction;
            sort->items.push_back(std::move(sort_item));
        }
        sort->child = std::move(current);
        current = std::move(sort);
    }

    // SKIP
    if (wc.skip) {
        auto count = bindSkipLimit(*wc.skip, "SKIP");
        if (!count)
            return std::nullopt;
        auto skip = std::make_unique<BoundSkipOp>();
        skip->count = *count;
        skip->child = std::move(current);
        current = std::move(skip);
    }

    // LIMIT
    if (wc.limit) {
        auto count = bindSkipLimit(*wc.limit, "LIMIT");
        if (!count)
            return std::nullopt;
        auto limit = std::make_unique<BoundLimitOp>();
        limit->count = *count;
        limit->child = std::move(current);
        current = std::move(limit);
    }

    // DISTINCT
    if (wc.distinct) {
        auto distinct = std::make_unique<BoundDistinctOp>();
        distinct->child = std::move(current);
        current = std::move(distinct);
    }

    // WHERE: must be bound against the union of old-scope and projected
    // variables. WITH types[i] AS lhs WHERE i <> j references old vars;
    // WITH x+1 AS y WHERE y > 0 references projected vars.
    // WHERE was already bound and inserted before the projection.
    // The code above consumes wc.where_pred and places the filter correctly.
    // This block is intentionally empty — do NOT bind WHERE here.

    // Scope reset: only WITH output columns are visible after this point.
    // Preserve metadata (source_label, source_prop_id, strong_typed) from old
    // symbols so that downstream SET/REMOVE can resolve property IDs.
    auto old_symbols = std::move(ctx_.symbols);
    ctx_.next_column_index = 0;
    for (size_t i = 0; i < with_outputs.size(); ++i) {
        ColumnInfo info;
        info.name = with_outputs[i].first;
        info.type = std::move(with_outputs[i].second);
        info.column_index = static_cast<uint32_t>(i);
        // Carry forward metadata and slot_id if this variable was known before WITH.
        auto it = old_symbols.find(with_outputs[i].first);
        if (it != old_symbols.end()) {
            info.source_labels = it->second.source_labels;
            info.source_prop_id = it->second.source_prop_id;
            info.strong_typed = it->second.strong_typed;
            info.slot_id = it->second.slot_id;
        }
        ctx_.symbols[with_outputs[i].first] = std::move(info);
    }
    ctx_.next_column_index = static_cast<uint32_t>(with_outputs.size());

    return current;
}

} // namespace binder
} // namespace eugraph
