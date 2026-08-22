#include "query/planner/binder.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>
#include <unordered_set>

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

static bool expressionReferencesAnyVariableImpl(const cypher::Expression& expr);

static bool expressionReferencesAnyVariable(const cypher::Expression& expr) {
    return expressionReferencesAnyVariableImpl(expr);
}

static bool expressionReferencesAnyVariableImpl(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if constexpr (std::is_same_v<Elem, cypher::Variable>) {
                return true;
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                return expressionReferencesAnyVariableImpl(ptr->inner);
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return expressionReferencesAnyVariableImpl(ptr->left) ||
                       expressionReferencesAnyVariableImpl(ptr->right);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return expressionReferencesAnyVariableImpl(ptr->operand);
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                for (const auto& arg : ptr->args)
                    if (expressionReferencesAnyVariableImpl(arg))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                return expressionReferencesAnyVariableImpl(ptr->object);
            } else if constexpr (std::is_same_v<Elem, cypher::LabelCastExpr>) {
                return expressionReferencesAnyVariableImpl(ptr->object);
            } else if constexpr (std::is_same_v<Elem, cypher::SubscriptExpr>) {
                return expressionReferencesAnyVariableImpl(ptr->list) ||
                       expressionReferencesAnyVariableImpl(ptr->index);
            } else if constexpr (std::is_same_v<Elem, cypher::SliceExpr>) {
                if (expressionReferencesAnyVariableImpl(ptr->list))
                    return true;
                if (ptr->from && expressionReferencesAnyVariableImpl(*ptr->from))
                    return true;
                if (ptr->to && expressionReferencesAnyVariableImpl(*ptr->to))
                    return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::CaseExpr>) {
                if (ptr->subject && expressionReferencesAnyVariableImpl(*ptr->subject))
                    return true;
                for (const auto& [cond, res] : ptr->when_thens) {
                    if (expressionReferencesAnyVariableImpl(cond) || expressionReferencesAnyVariableImpl(res))
                        return true;
                }
                if (ptr->else_expr && expressionReferencesAnyVariableImpl(*ptr->else_expr))
                    return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& elem : ptr->elements)
                    if (expressionReferencesAnyVariableImpl(elem))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries) {
                    (void)k;
                    if (expressionReferencesAnyVariableImpl(v))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                // The loop variable is local to the comprehension; only the
                // source list expression is evaluated in the outer scope.
                return expressionReferencesAnyVariableImpl(ptr->list_expr);
            } else if constexpr (std::is_same_v<Elem, cypher::AllExpr> || std::is_same_v<Elem, cypher::AnyExpr> ||
                                 std::is_same_v<Elem, cypher::NoneExpr> || std::is_same_v<Elem, cypher::SingleExpr>) {
                return expressionReferencesAnyVariableImpl(ptr->list_expr);
            } else if constexpr (std::is_same_v<Elem, cypher::ExistsExpr> ||
                                 std::is_same_v<Elem, cypher::PatternComprehension>) {
                // Pattern subqueries are not valid SKIP/LIMIT arguments.
                return true;
            } else {
                return false;
            }
        },
        expr);
}

/// Variant used by the in-function ReturnItemView (RETURN/WITH keep only an
/// expression reference + alias after list-comprehension lowering).
template <typename ItemView> static std::string projectionAliasOf(const ItemView& item) {
    if (item.alias)
        return *item.alias;
    if (!item.source_text.empty())
        return item.source_text;
    return cypher::expressionToString(item.expr);
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

/// True when any aggregate function call appears inside another aggregate
/// call's argument list (e.g. count(count(*))).
static bool hasNestedAggregate(const cypher::Expression& expr, bool inside_aggregate = false);

static bool hasNestedAggregate(const cypher::Expression& expr, bool inside_aggregate) {
    return std::visit(
        [&](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if (!ptr)
                return false;
            if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                if (isAggregateFunctionName(ptr->name)) {
                    if (inside_aggregate)
                        return true;
                    for (const auto& arg : ptr->args)
                        if (hasNestedAggregate(arg, true))
                            return true;
                    return false;
                }
                for (const auto& arg : ptr->args)
                    if (hasNestedAggregate(arg, inside_aggregate))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return hasNestedAggregate(ptr->left, inside_aggregate) ||
                       hasNestedAggregate(ptr->right, inside_aggregate);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return hasNestedAggregate(ptr->operand, inside_aggregate);
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                return hasNestedAggregate(ptr->inner, inside_aggregate);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                return hasNestedAggregate(ptr->object, inside_aggregate);
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& e : ptr->elements)
                    if (hasNestedAggregate(e, inside_aggregate))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries) {
                    (void)k;
                    if (hasNestedAggregate(v, inside_aggregate))
                        return true;
                }
                return false;
            } else {
                return false;
            }
        },
        expr);
}

/// True when `rand()` appears inside an aggregate function's arguments.
/// Cypher treats non-deterministic functions in aggregations as
/// NonConstantExpression.
static bool hasRandInsideAggregate(const cypher::Expression& expr, bool inside_aggregate = false);

static bool hasRandInsideAggregate(const cypher::Expression& expr, bool inside_aggregate) {
    return std::visit(
        [&](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if (!ptr)
                return false;
            if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                std::string lower;
                lower.reserve(ptr->name.size());
                for (unsigned char c : ptr->name)
                    lower.push_back(static_cast<char>(std::tolower(c)));
                if (inside_aggregate && lower == "rand")
                    return true;
                bool now_inside = inside_aggregate || isAggregateFunctionName(ptr->name);
                for (const auto& arg : ptr->args)
                    if (hasRandInsideAggregate(arg, now_inside))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return hasRandInsideAggregate(ptr->left, inside_aggregate) ||
                       hasRandInsideAggregate(ptr->right, inside_aggregate);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return hasRandInsideAggregate(ptr->operand, inside_aggregate);
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                return hasRandInsideAggregate(ptr->inner, inside_aggregate);
            } else {
                return false;
            }
        },
        expr);
}

/// True when a list comprehension appears anywhere in the expression.
/// List comprehensions introduce their own local variable scope; aggregates
/// inside them are already scoped to the comprehension, so they must not be
/// treated as ambiguous against the outer RETURN/WITH scope.
static bool containsListComprehension(const cypher::Expression& expr);

static bool containsListComprehension(const cypher::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using Elem = typename std::decay_t<decltype(ptr)>::element_type;
            if (!ptr)
                return false;
            if constexpr (std::is_same_v<Elem, cypher::ListComprehension>) {
                return true;
            } else if constexpr (std::is_same_v<Elem, cypher::FunctionCall>) {
                for (const auto& arg : ptr->args)
                    if (containsListComprehension(arg))
                        return true;
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::BinaryOp>) {
                return containsListComprehension(ptr->left) || containsListComprehension(ptr->right);
            } else if constexpr (std::is_same_v<Elem, cypher::UnaryOp>) {
                return containsListComprehension(ptr->operand);
            } else if constexpr (std::is_same_v<Elem, cypher::ParenExpr>) {
                return containsListComprehension(ptr->inner);
            } else if constexpr (std::is_same_v<Elem, cypher::PropertyAccess>) {
                return containsListComprehension(ptr->object);
            } else if constexpr (std::is_same_v<Elem, cypher::MapExpr>) {
                for (const auto& [k, v] : ptr->entries) {
                    (void)k;
                    if (containsListComprehension(v))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Elem, cypher::ListExpr>) {
                for (const auto& e : ptr->elements)
                    if (containsListComprehension(e))
                        return true;
                return false;
            } else {
                return false;
            }
        },
        expr);
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

/// Recompute the physical column index and slot of internal `__agg_N`
/// references once the final number of group keys and the aggregate slots are
/// known. AggregateOp outputs all group keys first, then every aggregate
/// column in the order it was collected.
static void retargetAggregateColumnRefs(binder::BoundExpression& expr,
                                        const std::unordered_map<std::string, size_t>& internal_ordinals,
                                        const std::unordered_map<std::string, SlotId>& internal_slots,
                                        size_t group_keys_size) {
    std::visit(
        [&](auto& ptr) {
            using T = std::decay_t<decltype(ptr)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                auto it = internal_ordinals.find(ptr.name);
                if (it != internal_ordinals.end()) {
                    ptr.column_index = static_cast<uint32_t>(group_keys_size + it->second);
                    auto slot_it = internal_slots.find(ptr.name);
                    if (slot_it != internal_slots.end())
                        ptr.slot_id = slot_it->second;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                retargetAggregateColumnRefs(ptr->left, internal_ordinals, internal_slots, group_keys_size);
                retargetAggregateColumnRefs(ptr->right, internal_ordinals, internal_slots, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                retargetAggregateColumnRefs(ptr->operand, internal_ordinals, internal_slots, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                for (auto& arg : ptr->args)
                    retargetAggregateColumnRefs(arg, internal_ordinals, internal_slots, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (auto& elem : ptr->elements)
                    retargetAggregateColumnRefs(elem, internal_ordinals, internal_slots, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (auto& [k, v] : ptr->entries) {
                    (void)k;
                    retargetAggregateColumnRefs(v, internal_ordinals, internal_slots, group_keys_size);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (ptr->subject)
                    retargetAggregateColumnRefs(*ptr->subject, internal_ordinals, internal_slots, group_keys_size);
                for (auto& [w, t] : ptr->when_thens) {
                    retargetAggregateColumnRefs(w, internal_ordinals, internal_slots, group_keys_size);
                    retargetAggregateColumnRefs(t, internal_ordinals, internal_slots, group_keys_size);
                }
                if (ptr->else_expr)
                    retargetAggregateColumnRefs(*ptr->else_expr, internal_ordinals, internal_slots, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                retargetAggregateColumnRefs(ptr->list, internal_ordinals, internal_slots, group_keys_size);
                retargetAggregateColumnRefs(ptr->index, internal_ordinals, internal_slots, group_keys_size);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                retargetAggregateColumnRefs(ptr->list, internal_ordinals, internal_slots, group_keys_size);
                if (ptr->from)
                    retargetAggregateColumnRefs(*ptr->from, internal_ordinals, internal_slots, group_keys_size);
                if (ptr->to)
                    retargetAggregateColumnRefs(*ptr->to, internal_ordinals, internal_slots, group_keys_size);
            }
        },
        expr);
}

// ==================== SKIP/LIMIT Helper ====================
std::optional<SkipLimitValue> Binder::bindSkipLimit(const cypher::Expression& expr, const char* clause_name) {
    // Literals are validated at compile time; every other accepted form
    // (parameters, variable-free constant expressions) is evaluated and
    // validated once at runtime. Parameters keep their runtime error phase.
    if (std::holds_alternative<std::unique_ptr<cypher::Literal>>(expr)) {
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
        SkipLimitValue result;
        result.is_constant = true;
        result.constant = val;
        return result;
    }

    if (expressionReferencesAnyVariable(expr)) {
        error(std::string("SemanticError: ") + clause_name + " must be a constant expression");
        return std::nullopt;
    }

    auto bound = bindExpression(expr);
    if (!bound)
        return std::nullopt;

    SkipLimitValue result;
    result.is_constant = false;
    result.runtime_expr = std::move(*bound);
    return result;
}

static std::unique_ptr<BoundSkipOp> makeBoundSkipOp(SkipLimitValue value) {
    auto op = std::make_unique<BoundSkipOp>();
    if (value.is_constant) {
        op->constant = value.constant;
    } else {
        op->expr = std::move(value.runtime_expr);
    }
    return op;
}

static std::unique_ptr<BoundLimitOp> makeBoundLimitOp(SkipLimitValue value) {
    auto op = std::make_unique<BoundLimitOp>();
    if (value.is_constant) {
        op->constant = value.constant;
    } else {
        op->expr = std::move(value.runtime_expr);
    }
    return op;
}

// ==================== RETURN Binding ====================

std::optional<BoundLogicalOperator> Binder::bindReturn(const cypher::ReturnClause& ret, BoundLogicalOperator child) {
    // Handle RETURN *: build projection items from all variables in scope
    // We cannot modify the const ret, so we build the projection inline
    // and then fall through to the normal bindReturn logic.
    // Instead, we build the items vector here and use it directly.
    if (ret.return_all && ret.items.empty()) {
        // Build a projection over all visible variables. Cypher orders the
        // RETURN * columns lexicographically by variable name; anonymous
        // internal bindings are not user-visible.
        auto proj = std::make_unique<BoundProjectOp>();
        std::vector<const ColumnInfo*> sorted_symbols;
        for (const auto& [name, col_info] : ctx_.symbols) {
            if (name.starts_with("__anon_"))
                continue;
            sorted_symbols.push_back(&col_info);
        }
        if (sorted_symbols.empty()) {
            error("SyntaxError: NoVariablesInScope: RETURN * requires at least one visible variable");
            return std::nullopt;
        }
        std::sort(sorted_symbols.begin(), sorted_symbols.end(),
                  [](const ColumnInfo* a, const ColumnInfo* b) { return a->name < b->name; });
        for (const auto* col_info : sorted_symbols) {
            auto bound_expr = std::make_optional<BoundExpression>(BoundColumnRef(
                col_info->column_index, col_info->type, col_info->name, col_info->slot_id, col_info->scope_id));
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

        // Same pipeline as the regular non-aggregating RETURN path:
        // child → Sort → Project → Skip → Limit → Distinct. Sort runs before
        // Project so ORDER BY can reference the original in-scope variables.
        BoundLogicalOperator child_op = std::move(child);
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
            sort->child = std::move(child_op);
            child_op = std::move(sort);
        }
        proj->child = std::move(child_op);
        BoundLogicalOperator current = std::move(proj);

        if (ret.skip) {
            auto skip_spec = bindSkipLimit(*ret.skip, "SKIP");
            if (!skip_spec)
                return std::nullopt;
            auto skip = makeBoundSkipOp(std::move(*skip_spec));
            skip->child = std::move(current);
            current = std::move(skip);
        }
        if (ret.limit) {
            auto limit_spec = bindSkipLimit(*ret.limit, "LIMIT");
            if (!limit_spec)
                return std::nullopt;
            auto limit = makeBoundLimitOp(std::move(*limit_spec));
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

    // Cypher forbids duplicate RETURN column names, including the implicit
    // name derived from the written expression (`RETURN 1+1, 1+1` is allowed
    // only if the resulting names differ — same name is a conflict).
    {
        std::set<std::string> seen;
        for (const auto& item : ret.items) {
            std::string alias = projectionAliasOf(item);
            if (!seen.insert(alias).second) {
                error("SyntaxError: ColumnNameConflict: duplicate column alias '" + alias + "' in RETURN");
                return std::nullopt;
            }
        }
    }

    // Lower top-level list comprehensions that contain pattern comprehensions.
    // Their loop variable only exists inside the row-wise list comprehension,
    // so the ordinary PC hoisting pass (which runs at plan level) cannot
    // correlate on it.
    struct ReturnItemView {
        const cypher::Expression& expr;
        const std::optional<std::string>& alias;
        std::string source_text;
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
            items.push_back({lowered_exprs.back(), item.alias, item.source_text});
        } else {
            items.push_back({item.expr, item.alias, item.source_text});
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

    if (has_aggregate) {
        // Compile-time aggregate semantics shared with WITH.
        for (const auto& item : items) {
            if (hasNestedAggregate(item.expr)) {
                error("SyntaxError: NestedAggregation: aggregate functions cannot be nested");
                return std::nullopt;
            }
            if (hasRandInsideAggregate(item.expr)) {
                error("SyntaxError: NonConstantExpression: non-deterministic functions are not "
                      "allowed inside aggregations");
                return std::nullopt;
            }
        }

        std::set<std::string> grouping_vars;
        for (const auto& item : items) {
            if (!hasAggregate(item.expr))
                collectAllVariables(item.expr, grouping_vars);
        }
        for (const auto& item : items) {
            if (auto* fc = std::get_if<std::unique_ptr<cypher::FunctionCall>>(&item.expr)) {
                if (fc && *fc && isAggregateFunctionName((*fc)->name))
                    continue; // simple aggregate — its arguments are aggregated
            }
            if (!hasAggregate(item.expr))
                continue;
            // List comprehensions introduce their own local loop variable;
            // aggregates inside them (e.g. collect(r)) are already scoped to
            // the comprehension and must not trigger ambiguity checks against
            // the outer RETURN scope.
            if (containsListComprehension(item.expr))
                continue;
            if (isAmbiguousAggregationExpr(item.expr)) {
                error("SyntaxError: AmbiguousAggregationExpression: expression mixes aggregate "
                      "and non-aggregate operations");
                return std::nullopt;
            }
            std::set<std::string> non_agg_vars;
            collectNonAggregateVariables(item.expr, non_agg_vars, {});
            for (const auto& var : non_agg_vars) {
                if (!grouping_vars.count(var)) {
                    error("SyntaxError: AmbiguousAggregationExpression: expression mixes aggregate "
                          "and non-aggregate operations");
                    return std::nullopt;
                }
            }
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
        enum class ProjKind {
            GROUP,
            SIMPLE_AGG,
            COMPLEX_AGG
        };
        struct ProjItem {
            binder::BoundExpression expr;
            std::string alias;
            ProjKind kind;
            /// GROUP: ordinal among group keys. SIMPLE_AGG: ordinal among
            /// aggregates. Physical output positions are assigned once the
            /// complete item list is known, because AggregateOp always lays
            /// columns out as [group keys..., aggregates...].
            size_t ordinal = 0;
        };
        std::vector<ProjItem> proj_items;
        std::vector<std::string> group_key_aliases;
        /// Alias → SlotId chosen for group keys / simple aggregates. Shadowing
        /// a bound variable with a different runtime type must allocate a
        /// fresh slot; reusing the old graph slot would make the DPL passes
        /// treat the scalar projection as the graph variable.
        std::unordered_map<std::string, SlotId> projection_slots;

        // Column index tracker for anonymous aggregate columns.
        uint32_t anon_idx = 0;

        for (const auto& item : items) {
            std::string alias = projectionAliasOf(item);
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
                size_t ordinal = agg->aggregates.size() - 1;

                auto existing = ctx_.symbols.find(alias);
                BoundType type = BoundType::clone(agg->aggregates.back().result_type);
                SlotId slot = existing != ctx_.symbols.end() && existing->second.slot_id != INVALID_SLOT_ID
                                  ? existing->second.slot_id
                                  : allocateNamedSlot(alias);
                projection_slots[alias] = slot;
                binder::BoundExpression passthrough = binder::BoundColumnRef{0, std::move(type), alias, slot};
                proj_items.push_back({std::move(passthrough), alias, ProjKind::SIMPLE_AGG, ordinal});
            } else if (hasAggregate(item.expr)) {
                // Complex expression containing aggregates: e.g., RETURN count(a) + 3.
                // Walk the bound expression, extract each aggregate call into
                // AggregateOp as an anonymous column, and replace the call in the
                // expression tree with a BoundColumnRef to that column. The final
                // physical index is retargeted below once all group keys are known.
                auto full_expr = std::move(*bound_expr);
                walkAndReplaceAggCalls(full_expr, agg->aggregates, anon_idx, agg->group_keys.size());
                proj_items.push_back({std::move(full_expr), alias, ProjKind::COMPLEX_AGG, 0});
            } else {
                // Group key: non-aggregate expression.
                agg->group_keys.push_back(std::move(*bound_expr));
                size_t ordinal = agg->group_keys.size() - 1;
                group_key_aliases.push_back(alias);

                auto existing = ctx_.symbols.find(alias);
                BoundType type = getBoundExprType(agg->group_keys.back());
                SlotId slot = existing != ctx_.symbols.end() && existing->second.slot_id != INVALID_SLOT_ID
                                  ? existing->second.slot_id
                                  : allocateNamedSlot(alias);
                projection_slots[alias] = slot;
                binder::BoundExpression passthrough = binder::BoundColumnRef{0, std::move(type), alias, slot};
                proj_items.push_back({std::move(passthrough), alias, ProjKind::GROUP, ordinal});
            }
        }

        // AggregateOp physical layout: every group key first, then every
        // aggregate column, regardless of the written item order.
        const size_t final_group_count = agg->group_keys.size();
        agg->output_names.clear();
        agg->output_names = group_key_aliases;
        std::unordered_map<std::string, size_t> internal_agg_ordinals;
        for (size_t i = 0; i < agg->aggregates.size(); ++i) {
            agg->output_names.push_back(agg->aggregates[i].alias);
            if (agg->aggregates[i].alias.starts_with("__agg_"))
                internal_agg_ordinals[agg->aggregates[i].alias] = i;
        }

        agg->child = std::move(child);

        // Register all AggregateOp columns (group keys first, then
        // aggregates) in the symbol table with their final physical indices.
        for (size_t i = 0; i < agg->output_names.size(); ++i) {
            const std::string& name = agg->output_names[i];
            BoundType type = i < agg->group_keys.size()
                                 ? getBoundExprType(agg->group_keys[i])
                                 : BoundType::clone(agg->aggregates[i - agg->group_keys.size()].result_type);
            auto existing = ctx_.symbols.find(name);
            if (existing != ctx_.symbols.end()) {
                existing->second.column_index = static_cast<uint32_t>(i);
                existing->second.type = std::move(type);
                auto slot_it = projection_slots.find(name);
                if (slot_it != projection_slots.end())
                    existing->second.slot_id = slot_it->second;
            } else {
                ColumnInfo info;
                info.name = name;
                info.type = std::move(type);
                info.column_index = static_cast<uint32_t>(i);
                auto slot_it = projection_slots.find(name);
                info.slot_id = slot_it != projection_slots.end() ? slot_it->second : allocateNamedSlot(name);
                ctx_.symbols[name] = std::move(info);
            }
        }

        std::unordered_map<std::string, SlotId> internal_slots;
        for (const auto& [name, ordinal] : internal_agg_ordinals) {
            (void)ordinal;
            auto it = ctx_.symbols.find(name);
            if (it != ctx_.symbols.end())
                internal_slots[name] = it->second.slot_id;
        }
        for (auto& pi : proj_items) {
            auto* ref = std::get_if<binder::BoundColumnRef>(&pi.expr);
            if (!ref) {
                if (pi.kind == ProjKind::COMPLEX_AGG)
                    retargetAggregateColumnRefs(pi.expr, internal_agg_ordinals, internal_slots, final_group_count);
                continue;
            }
            if (pi.kind == ProjKind::GROUP) {
                ref->column_index = static_cast<uint32_t>(pi.ordinal);
            } else if (pi.kind == ProjKind::SIMPLE_AGG) {
                ref->column_index = static_cast<uint32_t>(final_group_count + pi.ordinal);
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
                std::string alias = projectionAliasOf(item);
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
            auto skip_spec = bindSkipLimit(*ret.skip, "SKIP");
            if (!skip_spec)
                return std::nullopt;
            auto skip = makeBoundSkipOp(std::move(*skip_spec));
            skip->child = std::move(current);
            current = std::move(skip);
        }
        if (ret.limit) {
            auto limit_spec = bindSkipLimit(*ret.limit, "LIMIT");
            if (!limit_spec)
                return std::nullopt;
            auto limit = makeBoundLimitOp(std::move(*limit_spec));
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

        std::string alias = projectionAliasOf(item);
        BoundProjectOp::ProjectItem proj_item;
        proj_item.expr = std::move(*bound_expr);
        proj_item.alias = std::move(alias);
        proj_item.result_type = getBoundExprType(proj_item.expr);
        proj->items.push_back(std::move(proj_item));
    }
    proj->child = std::move(child);

    // Register RETURN aliases so the output schema exposes the projected
    // names. ORDER BY itself is handled below via alias substitution against
    // the input scope (Sort is placed before Project).
    for (size_t i = 0; i < proj->items.size(); ++i) {
        const auto& proj_item = proj->items[i];
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
        // Same validation rules as WITH ORDER BY. DISTINCT removes all but
        // the projected expressions, so every sort key must be (or match) a
        // projection item. Aggregates are not allowed next to a non-
        // aggregating RETURN.
        for (const auto& si : ret.order_by->items) {
            if (hasAggregate(si.expr)) {
                error("SyntaxError: InvalidAggregation: Cannot use aggregate functions in "
                      "ORDER BY of a non-aggregating RETURN clause");
                return std::nullopt;
            }
        }
        if (ret.distinct) {
            std::set<std::string> projected_exprs;
            std::set<std::string> projected_names;
            std::set<std::string> projected_bare_vars;
            for (const auto& item : items) {
                projected_exprs.insert(cypher::expressionToString(item.expr));
                projected_names.insert(projectionAliasOf(item));
                if (std::holds_alternative<std::unique_ptr<cypher::Variable>>(item.expr))
                    projected_bare_vars.insert(cypher::expressionToString(item.expr));
            }
            for (const auto& si : ret.order_by->items) {
                bool ok = projected_exprs.count(cypher::expressionToString(si.expr)) > 0;
                if (!ok) {
                    if (auto* var = std::get_if<std::unique_ptr<cypher::Variable>>(&si.expr)) {
                        if (var && *var && projected_names.count((*var)->name) > 0)
                            ok = true;
                    }
                }
                // `RETURN DISTINCT n ORDER BY n.name` is valid: the sort key
                // is derived from a projected bare variable.
                if (!ok) {
                    std::set<std::string> sort_vars;
                    collectAllVariables(si.expr, sort_vars);
                    bool all_bare = !sort_vars.empty();
                    for (const auto& var : sort_vars) {
                        if (!projected_bare_vars.count(var)) {
                            all_bare = false;
                            break;
                        }
                    }
                    ok = all_bare;
                }
                if (!ok) {
                    error("SyntaxError: UndefinedVariable: ORDER BY expression is not part of the "
                          "DISTINCT projection");
                    return std::nullopt;
                }
            }
        }

        // Sort runs before Project, so references to RETURN aliases are
        // substituted with their original expressions (the same mechanism
        // non-aggregating WITH uses). This also handles aliases nested in
        // larger expressions, e.g. `RETURN n.num AS n ORDER BY n + 2`.
        order_by_alias_subs_.clear();
        for (const auto& item : items)
            order_by_alias_subs_[projectionAliasOf(item)] = &item.expr;

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
        order_by_alias_subs_.clear();
        sort->child = std::move(child_op);
        child_op = std::move(sort);
    }

    // Projection after sort
    proj->child = std::move(child_op);
    BoundLogicalOperator current = std::move(proj);

    // SKIP, LIMIT, DISTINCT
    if (ret.skip) {
        auto skip_spec = bindSkipLimit(*ret.skip, "SKIP");
        if (!skip_spec)
            return std::nullopt;
        auto skip = makeBoundSkipOp(std::move(*skip_spec));
        skip->child = std::move(current);
        current = std::move(skip);
    }
    if (ret.limit) {
        auto limit_spec = bindSkipLimit(*ret.limit, "LIMIT");
        if (!limit_spec)
            return std::nullopt;
        auto limit = makeBoundLimitOp(std::move(*limit_spec));
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
        std::string source_text;
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
            items.push_back({lowered_exprs.back(), item.alias, item.source_text});
        } else {
            items.push_back({item.expr, item.alias, item.source_text});
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
            std::string alias = projectionAliasOf(item);
            if (!seen.insert(alias).second) {
                error("SyntaxError: ColumnNameConflict: duplicate column alias '" + alias + "' in WITH");
                return std::nullopt;
            }
        }
    }

    // WITH requires an explicit alias for every non-variable expression.
    // The non-aggregating branch checks this before binding; aggregating WITH
    // checks after aggregate/ORDER BY validation so ambiguity errors take
    // precedence (`WITH a, count(*)` → NoExpressionAlias; `WITH me.age +
    // you.age, count(*) ORDER BY me.age + you.age + count(*)` →
    // AmbiguousAggregationExpression).
    BoundLogicalOperator current;

    if (has_aggregate) {
        // Check for AmbiguousAggregationExpression (With6 [8][9]):
        // expressions that mix aggregate with non-aggregate operands
        // that are NOT already established as grouping keys.
        std::set<std::string> grouping_vars;
        for (const auto& item : items) {
            if (!hasAggregate(item.expr))
                collectAllVariables(item.expr, grouping_vars);
        }
        for (const auto& item : items) {
            if (auto* fc = std::get_if<std::unique_ptr<cypher::FunctionCall>>(&item.expr)) {
                if (fc && *fc && isAggregateFunctionName((*fc)->name))
                    continue; // simple aggregate — ok
            }
            // List comprehension wraps its internal aggregate — valid.
            if (containsListComprehension(item.expr))
                continue;
            if (hasAggregate(item.expr)) {
                if (isAmbiguousAggregationExpr(item.expr)) {
                    error("SyntaxError: AmbiguousAggregationExpression: expression mixes "
                          "aggregate and non-aggregate operations");
                    return std::nullopt;
                }
                // Gather all variables outside aggregate calls and check they
                // are covered by grouping-key variables. A grouping key is a
                // property expression (`me.age`), so compare variable sets
                // rather than exact expression strings.
                std::set<std::string> non_agg_vars;
                collectNonAggregateVariables(item.expr, non_agg_vars, {});
                for (const auto& v : non_agg_vars) {
                    if (!grouping_vars.count(v)) {
                        error("SyntaxError: AmbiguousAggregationExpression: expression mixes "
                              "aggregate and non-aggregate operations");
                        return std::nullopt;
                    }
                }
            }
        }

        auto agg = std::make_unique<BoundAggregateOp>();

        enum class ProjKind {
            GROUP,
            SIMPLE_AGG,
            COMPLEX_AGG
        };
        struct ProjItem {
            binder::BoundExpression expr;
            std::string alias;
            ProjKind kind;
            size_t ordinal = 0;
        };
        std::vector<ProjItem> proj_items;
        std::vector<std::string> group_key_aliases;
        std::unordered_map<std::string, SlotId> projection_slots;
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
            std::string alias = projectionAliasOf(item);
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
                size_t ordinal = agg->aggregates.size() - 1;
                if (has_complex_agg) {
                    auto existing = ctx_.symbols.find(alias);
                    BoundType type = BoundType::clone(agg->aggregates.back().result_type);
                    SlotId slot = existing != ctx_.symbols.end() && existing->second.slot_id != INVALID_SLOT_ID
                                      ? existing->second.slot_id
                                      : allocateNamedSlot(alias);
                    projection_slots[alias] = slot;
                    binder::BoundExpression passthrough = binder::BoundColumnRef{0, std::move(type), alias, slot};
                    proj_items.push_back({std::move(passthrough), alias, ProjKind::SIMPLE_AGG, ordinal});
                }
            } else if (hasAggregate(item.expr)) {
                auto full_expr = std::move(*bound_expr);
                walkAndReplaceAggCalls(full_expr, agg->aggregates, anon_idx, agg->group_keys.size());
                proj_items.push_back({std::move(full_expr), alias, ProjKind::COMPLEX_AGG, 0});
            } else {
                agg->group_keys.push_back(std::move(*bound_expr));
                size_t ordinal = agg->group_keys.size() - 1;
                group_key_aliases.push_back(alias);
                if (has_complex_agg) {
                    auto existing = ctx_.symbols.find(alias);
                    BoundType type = getBoundExprType(agg->group_keys.back());
                    SlotId slot = existing != ctx_.symbols.end() && existing->second.slot_id != INVALID_SLOT_ID
                                      ? existing->second.slot_id
                                      : allocateNamedSlot(alias);
                    projection_slots[alias] = slot;
                    binder::BoundExpression passthrough = binder::BoundColumnRef{0, std::move(type), alias, slot};
                    proj_items.push_back({std::move(passthrough), alias, ProjKind::GROUP, ordinal});
                }
            }
        }

        // AggregateOp physical layout: all group keys first, then all
        // aggregate columns, regardless of the written item order.
        const size_t final_group_count = agg->group_keys.size();
        agg->output_names.clear();
        agg->output_names = group_key_aliases;
        std::unordered_map<std::string, size_t> internal_agg_ordinals;
        for (size_t i = 0; i < agg->aggregates.size(); ++i) {
            agg->output_names.push_back(agg->aggregates[i].alias);
            if (agg->aggregates[i].alias.starts_with("__agg_"))
                internal_agg_ordinals[agg->aggregates[i].alias] = i;
        }

        // Register AggregateOp columns with their final physical indices.
        for (size_t i = 0; i < agg->output_names.size(); ++i) {
            const std::string& name = agg->output_names[i];
            BoundType type = i < agg->group_keys.size()
                                 ? getBoundExprType(agg->group_keys[i])
                                 : BoundType::clone(agg->aggregates[i - agg->group_keys.size()].result_type);
            auto existing = ctx_.symbols.find(name);
            if (existing != ctx_.symbols.end()) {
                existing->second.column_index = static_cast<uint32_t>(i);
                existing->second.type = std::move(type);
                auto slot_it = projection_slots.find(name);
                if (slot_it != projection_slots.end())
                    existing->second.slot_id = slot_it->second;
            } else {
                ColumnInfo info;
                info.name = name;
                info.type = std::move(type);
                info.column_index = static_cast<uint32_t>(i);
                auto slot_it = projection_slots.find(name);
                info.slot_id = slot_it != projection_slots.end() ? slot_it->second : allocateNamedSlot(name);
                ctx_.symbols[name] = std::move(info);
            }
        }

        std::unordered_map<std::string, SlotId> internal_slots;
        for (const auto& [name, ordinal] : internal_agg_ordinals) {
            (void)ordinal;
            auto it = ctx_.symbols.find(name);
            if (it != ctx_.symbols.end())
                internal_slots[name] = it->second.slot_id;
        }
        for (auto& pi : proj_items) {
            auto* ref = std::get_if<binder::BoundColumnRef>(&pi.expr);
            if (!ref) {
                if (pi.kind == ProjKind::COMPLEX_AGG)
                    retargetAggregateColumnRefs(pi.expr, internal_agg_ordinals, internal_slots, final_group_count);
                continue;
            }
            if (pi.kind == ProjKind::GROUP) {
                ref->column_index = static_cast<uint32_t>(pi.ordinal);
            } else if (pi.kind == ProjKind::SIMPLE_AGG) {
                ref->column_index = static_cast<uint32_t>(final_group_count + pi.ordinal);
            }
        }

        if (has_complex_agg) {
            with_outputs.clear();
            for (const auto& pi : proj_items)
                with_outputs.emplace_back(pi.alias, getBoundExprType(pi.expr));
        } else {
            with_outputs.clear();
            for (size_t i = 0; i < agg->output_names.size(); ++i) {
                BoundType type = i < agg->group_keys.size()
                                     ? getBoundExprType(agg->group_keys[i])
                                     : BoundType::clone(agg->aggregates[i - agg->group_keys.size()].result_type);
                with_outputs.emplace_back(agg->output_names[i], std::move(type));
            }
        }

        // WHERE on WITH: if it references projected (aggregate-output) variables,
        // it must be placed AFTER the aggregation. Otherwise (pure old-scope refs),
        // bind and insert it BEFORE the aggregation for efficiency.
        where_has_projected_ref = false;
        if (wc.where_pred) {
            for (const auto& item : items) {
                std::string alias = projectionAliasOf(item);
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
                std::string alias = projectionAliasOf(item);
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
            // WITH *: pass through all variables from current scope, ordered
            // lexicographically like RETURN *. Anonymous variables are kept:
            // they are not user-visible but still carry rows for downstream
            // clauses (e.g. MATCH () CREATE () WITH * CREATE ()).
            std::vector<const ColumnInfo*> sorted_symbols;
            for (const auto& [name, col_info] : ctx_.symbols) {
                if (name.starts_with("__anon_edge_"))
                    continue;
                sorted_symbols.push_back(&col_info);
            }
            std::sort(sorted_symbols.begin(), sorted_symbols.end(),
                      [](const ColumnInfo* a, const ColumnInfo* b) { return a->name < b->name; });
            for (const auto* col_info : sorted_symbols) {
                auto bound_expr = BoundColumnRef(col_info->column_index, col_info->type, col_info->name,
                                                 col_info->slot_id, col_info->scope_id);
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

                std::string alias = projectionAliasOf(item);
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
                    std::string alias = projectionAliasOf(item);
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
            std::string alias = projectionAliasOf(item);
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

    // Aggregating WITH also requires explicit aliases for non-variable
    // expressions. Run after aggregate/ORDER BY validation so the more
    // specific aggregation errors take precedence.
    if (has_aggregate) {
        for (const auto& item : items) {
            if (item.alias)
                continue;
            if (std::holds_alternative<std::unique_ptr<cypher::Variable>>(item.expr))
                continue;
            error("SyntaxError: NoExpressionAlias: expression in WITH must be aliased "
                  "(use AS <name>)");
            return std::nullopt;
        }
    }

    // SKIP
    if (wc.skip) {
        auto skip_spec = bindSkipLimit(*wc.skip, "SKIP");
        if (!skip_spec)
            return std::nullopt;
        auto skip = makeBoundSkipOp(std::move(*skip_spec));
        skip->child = std::move(current);
        current = std::move(skip);
    }

    // LIMIT
    if (wc.limit) {
        auto limit_spec = bindSkipLimit(*wc.limit, "LIMIT");
        if (!limit_spec)
            return std::nullopt;
        auto limit = makeBoundLimitOp(std::move(*limit_spec));
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
