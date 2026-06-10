#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace eugraph {
namespace cypher {

// ==================== Expression 前向声明 ====================

struct Literal;
struct Variable;
struct Parameter;
struct BinaryOp;
struct UnaryOp;
struct FunctionCall;
struct PropertyAccess;
struct ListExpr;
struct MapExpr;
struct CaseExpr;
struct ListComprehension;
struct PatternComprehension;
struct SubscriptExpr;
struct SliceExpr;
struct ExistsExpr;
struct AllExpr;
struct AnyExpr;
struct NoneExpr;
struct SingleExpr;
struct LabelCastExpr;
struct ParenExpr;

// ==================== Expression ====================
// 递归类型用 unique_ptr 包装

using Expression =
    std::variant<std::unique_ptr<Literal>, std::unique_ptr<Variable>, std::unique_ptr<Parameter>,
                 std::unique_ptr<BinaryOp>, std::unique_ptr<UnaryOp>, std::unique_ptr<FunctionCall>,
                 std::unique_ptr<PropertyAccess>, std::unique_ptr<LabelCastExpr>, std::unique_ptr<ListExpr>,
                 std::unique_ptr<MapExpr>, std::unique_ptr<CaseExpr>, std::unique_ptr<ListComprehension>,
                 std::unique_ptr<PatternComprehension>, std::unique_ptr<SubscriptExpr>, std::unique_ptr<SliceExpr>,
                 std::unique_ptr<ExistsExpr>, std::unique_ptr<AllExpr>, std::unique_ptr<AnyExpr>,
                 std::unique_ptr<NoneExpr>, std::unique_ptr<SingleExpr>, std::unique_ptr<ParenExpr>>;

// ==================== Literal ====================

struct NullValue {};

struct Literal {
    std::variant<NullValue, bool, int64_t, double, std::string> value;
};

// ==================== Variable ====================

struct Variable {
    std::string name;
};

// ==================== Parameter ====================

struct Parameter {
    std::string name; // $param_name
};

// ==================== BinaryOp ====================

enum class BinaryOperator {
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    POW,
    EQ,
    NEQ,
    LT,
    GT,
    LTE,
    GTE,
    STARTS_WITH,
    ENDS_WITH,
    CONTAINS,
    IN,
    AND,
    OR,
    XOR,
    LIST_CONCAT,
};

struct BinaryOp {
    BinaryOperator op;
    Expression left;
    Expression right;
};

// ==================== UnaryOp ====================

enum class UnaryOperator {
    NOT,
    NEGATE,
    PLUS,
    IS_NULL,
    IS_NOT_NULL
};

struct UnaryOp {
    UnaryOperator op;
    Expression operand;
};

// ==================== FunctionCall ====================

struct FunctionCall {
    std::string name;
    bool distinct = false;
    std::vector<Expression> args;
};

// ==================== PropertyAccess ====================

struct PropertyAccess {
    Expression object;
    std::string property;
};

/// Label-scoped access: n::Label or n::Label.prop
struct LabelCastExpr {
    Expression object;
    std::string label;
};

// ==================== ParenExpr ====================

// Marks an expression that was wrapped in parentheses in the source text.
// Used by hoistAtomicPredicates to prevent hoisting atomic predicates
// across explicit parenthesization boundaries. Stripped before the binder.
struct ParenExpr {
    Expression inner;
};

// ==================== ListExpr ====================

struct ListExpr {
    std::vector<Expression> elements;
};

// ==================== MapExpr ====================

struct MapExpr {
    std::vector<std::pair<std::string, Expression>> entries;
};

// ==================== CaseExpr ====================

struct CaseExpr {
    std::optional<Expression> subject;
    std::vector<std::pair<Expression, Expression>> when_thens;
    std::optional<Expression> else_expr;
};

// ==================== ListComprehension ====================

struct ListComprehension {
    std::string variable;
    Expression list_expr;
    std::optional<Expression> where_pred;
    std::optional<Expression> projection;
};

// ==================== PatternComprehension ====================
// 前向声明 PatternPart

struct PatternPart;

struct PatternComprehension {
    std::optional<std::string> variable;
    std::vector<PatternPart> patterns;
    std::optional<Expression> where_pred;
    std::optional<Expression> projection;
};

// ==================== SubscriptExpr ====================

struct SubscriptExpr {
    Expression list;
    Expression index;
};

// ==================== SliceExpr ====================

struct SliceExpr {
    Expression list;
    std::optional<Expression> from;
    std::optional<Expression> to;
};

// ==================== ExistsExpr ====================

struct ExistsExpr {
    std::vector<PatternPart> patterns;
    std::optional<Expression> where_pred;
};

// ==================== Quantifier Expressions ====================

struct AllExpr {
    std::string variable;
    Expression list_expr;
    std::optional<Expression> where_pred;
};

struct AnyExpr {
    std::string variable;
    Expression list_expr;
    std::optional<Expression> where_pred;
};

struct NoneExpr {
    std::string variable;
    Expression list_expr;
    std::optional<Expression> where_pred;
};

struct SingleExpr {
    std::string variable;
    Expression list_expr;
    std::optional<Expression> where_pred;
};

// ==================== Pattern ====================

struct PropertiesMap {
    std::vector<std::pair<std::string, Expression>> entries;
};

struct NodePattern {
    std::optional<std::string> variable;
    std::vector<std::string> labels;
    std::optional<PropertiesMap> properties;
};

enum class RelationshipDirection {
    LEFT_TO_RIGHT, // -[]->
    RIGHT_TO_LEFT, // <-[]-
    UNDIRECTED     // -[]-
};

struct RelationshipPattern {
    std::optional<std::string> variable;
    std::vector<std::string> rel_types;
    std::optional<PropertiesMap> properties;
    RelationshipDirection direction = RelationshipDirection::LEFT_TO_RIGHT;
    std::optional<std::pair<Expression, Expression>> range; // *min..max
};

struct PatternElement {
    NodePattern node;
    std::vector<std::pair<RelationshipPattern, NodePattern>> chain;
};

struct PatternPart {
    std::optional<std::string> variable; // named path: p = (a)-->(b)
    PatternElement element;
};

// ==================== Clause ====================

struct OrderBy {
    enum class Direction {
        ASC,
        DESC
    };
    struct SortItem {
        Expression expr;
        Direction direction = Direction::ASC;
    };
    std::vector<SortItem> items;
};

struct ReturnItem {
    Expression expr;
    std::optional<std::string> alias;
};

struct MatchClause {
    bool optional = false;
    std::vector<PatternPart> patterns;
    std::optional<Expression> where_pred;
};

struct CreateClause {
    std::vector<PatternPart> patterns;
};

struct MergeClause {
    PatternPart pattern;
    std::vector<struct SetItem> on_create;
    std::vector<struct SetItem> on_match;
};

struct DeleteClause {
    bool detach = false;
    std::vector<Expression> expressions;
};

enum class SetItemKind {
    SET_PROPERTY,
    SET_PROPERTIES,
    SET_DYNAMIC_PROPERTY,
    SET_LABELS,
};

struct SetItem {
    SetItemKind kind;
    Expression target;
    std::optional<Expression> value;
    std::string label;
    bool is_add_assign = false;
};

struct RemoveItem {
    enum class Kind {
        PROPERTY,
        LABEL
    };
    Kind kind;
    Expression target;
    std::string name;
};

struct RemoveClause {
    std::vector<RemoveItem> items;
};

struct UnwindClause {
    Expression list_expr;
    std::string variable;
};

struct ReturnClause {
    bool distinct = false;
    bool return_all = false; // RETURN *
    std::vector<ReturnItem> items;
    std::optional<OrderBy> order_by;
    std::optional<Expression> skip;
    std::optional<Expression> limit;
};

struct WithClause {
    bool distinct = false;
    bool return_all = false; // WITH *
    std::vector<ReturnItem> items;
    std::optional<OrderBy> order_by;
    std::optional<Expression> skip;
    std::optional<Expression> limit;
    std::optional<Expression> where_pred;
};

struct SetClause {
    std::vector<SetItem> items;
};

struct CallClause {
    std::string procedure_name;
    std::vector<Expression> args;
    std::vector<ReturnItem> yield_items;
    std::optional<Expression> where_pred;
};

// Clause variant
using Clause = std::variant<std::unique_ptr<MatchClause>, std::unique_ptr<UnwindClause>, std::unique_ptr<CallClause>,
                            std::unique_ptr<CreateClause>, std::unique_ptr<MergeClause>, std::unique_ptr<DeleteClause>,
                            std::unique_ptr<SetClause>, std::unique_ptr<RemoveClause>, std::unique_ptr<ReturnClause>,
                            std::unique_ptr<WithClause>>;

// ==================== Query ====================

struct SingleQuery {
    std::vector<Clause> clauses;
};

struct UnionClause {
    bool all = false;
};

struct RegularQuery {
    SingleQuery first;
    std::vector<std::pair<UnionClause, SingleQuery>> unions;
};

struct StandaloneCall {
    std::string procedure_name;
    std::vector<Expression> args;
    std::vector<ReturnItem> yield_items;
    std::optional<Expression> where_pred;
};

// ==================== EXPLAIN ====================

struct ExplainStatement {
    std::unique_ptr<RegularQuery> query;
};

// ==================== 顶层 Statement ====================

using Statement =
    std::variant<std::unique_ptr<RegularQuery>, std::unique_ptr<StandaloneCall>, std::unique_ptr<ExplainStatement>>;

// ==================== 辅助：创建 Expression 的便捷函数 ====================

inline Expression makeLiteral(std::variant<NullValue, bool, int64_t, double, std::string> val) {
    return std::make_unique<Literal>(Literal{std::move(val)});
}

inline Expression makeVariable(std::string name) {
    return std::make_unique<Variable>(Variable{std::move(name)});
}

inline Expression makeBinaryOp(BinaryOperator op, Expression left, Expression right) {
    return std::make_unique<BinaryOp>(BinaryOp{op, std::move(left), std::move(right)});
}

inline Expression makeUnaryOp(UnaryOperator op, Expression operand) {
    return std::make_unique<UnaryOp>(UnaryOp{op, std::move(operand)});
}

inline Expression makePropertyAccess(Expression object, std::string property) {
    return std::make_unique<PropertyAccess>(PropertyAccess{std::move(object), std::move(property)});
}

inline Expression makeLabelCast(Expression object, std::string label) {
    return std::make_unique<LabelCastExpr>(LabelCastExpr{std::move(object), std::move(label)});
}

inline Expression makeFunctionCall(std::string name, std::vector<Expression> args, bool distinct = false) {
    return std::make_unique<FunctionCall>(FunctionCall{std::move(name), distinct, std::move(args)});
}

inline Expression makeParenExpr(Expression inner) {
    return std::make_unique<ParenExpr>(ParenExpr{std::move(inner)});
}

// Strip ParenExpr wrappers from an Expression tree. Called after hoisting
// and before the binder, so the binder never sees ParenExpr nodes.
inline Expression stripParens(Expression expr);

// ==================== 辅助：Expression → 字符串（用于生成默认列名）====================

inline std::string expressionToString(const Expression& expr);

inline std::string expressionToString(const Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> std::string {
            using T = std::decay_t<decltype(ptr)>;
            using OpType = typename T::element_type;
            if constexpr (std::is_same_v<OpType, Variable>) {
                return ptr->name;
            } else if constexpr (std::is_same_v<OpType, PropertyAccess>) {
                auto obj_str = expressionToString(ptr->object);
                if (std::holds_alternative<std::unique_ptr<SubscriptExpr>>(ptr->object) ||
                    std::holds_alternative<std::unique_ptr<SliceExpr>>(ptr->object)) {
                    obj_str = "(" + obj_str + ")";
                }
                return obj_str + "." + ptr->property;
            } else if constexpr (std::is_same_v<OpType, LabelCastExpr>) {
                return expressionToString(ptr->object) + "::" + ptr->label;
            } else if constexpr (std::is_same_v<OpType, ParenExpr>) {
                return "(" + expressionToString(ptr->inner) + ")";
            } else if constexpr (std::is_same_v<OpType, Literal>) {
                if (std::holds_alternative<std::string>(ptr->value))
                    return std::get<std::string>(ptr->value);
                if (std::holds_alternative<int64_t>(ptr->value))
                    return std::to_string(std::get<int64_t>(ptr->value));
                if (std::holds_alternative<double>(ptr->value)) {
                    std::string s = std::to_string(std::get<double>(ptr->value));
                    // Strip trailing zeros to match TCK expectation (e.g. 12.96 not 12.960000)
                    auto dot = s.find('.');
                    if (dot != std::string::npos) {
                        size_t end = s.size() - 1;
                        while (end > dot + 1 && s[end] == '0')
                            --end;
                        s = s.substr(0, end + 1);
                    }
                    return s;
                }
                if (std::holds_alternative<bool>(ptr->value))
                    return std::get<bool>(ptr->value) ? "true" : "false";
                return "null";
            } else if constexpr (std::is_same_v<OpType, BinaryOp>) {
                static const char* binOpNames[] = {"+",        "-",  "*",   "/",  "%",   "^",           "=",
                                                   "<>",       "<",  ">",   "<=", ">=",  "STARTS WITH", "ENDS WITH",
                                                   "CONTAINS", "IN", "AND", "OR", "XOR", "||"};
                const char* opStr = static_cast<size_t>(ptr->op) < sizeof(binOpNames) / sizeof(binOpNames[0])
                                        ? binOpNames[static_cast<size_t>(ptr->op)]
                                        : "?";
                static const auto binPrec = [](BinaryOperator o) -> int {
                    switch (o) {
                    case BinaryOperator::POW:
                        return 3;
                    case BinaryOperator::MUL:
                    case BinaryOperator::DIV:
                    case BinaryOperator::MOD:
                        return 2;
                    case BinaryOperator::ADD:
                    case BinaryOperator::SUB:
                        return 1;
                    default:
                        return 0;
                    }
                };
                int parentPrec = binPrec(ptr->op);
                auto parenChild = [&](const Expression& child, bool isRight) -> std::string {
                    auto s = expressionToString(child);
                    if (std::holds_alternative<std::unique_ptr<BinaryOp>>(child)) {
                        auto& childOp = *std::get<std::unique_ptr<BinaryOp>>(child);
                        int childPrec = binPrec(childOp.op);
                        // Left  child: parens when strictly lower precedence.
                        // Right child: parens when lower or equal (non-associative ops like / and -).
                        if (childPrec < parentPrec || (isRight && childPrec == parentPrec))
                            return "(" + s + ")";
                    }
                    return s;
                };
                return parenChild(ptr->left, false) + " " + opStr + " " + parenChild(ptr->right, true);
            } else if constexpr (std::is_same_v<OpType, UnaryOp>) {
                static const char* unaryOpNames[] = {"NOT ", "-", "+", "IS NULL", "IS NOT NULL"};
                const char* opStr = static_cast<size_t>(ptr->op) < sizeof(unaryOpNames) / sizeof(unaryOpNames[0])
                                        ? unaryOpNames[static_cast<size_t>(ptr->op)]
                                        : "?";
                if (ptr->op == UnaryOperator::IS_NULL || ptr->op == UnaryOperator::IS_NOT_NULL)
                    return expressionToString(ptr->operand) + " " + opStr;
                return opStr + expressionToString(ptr->operand);
            } else if constexpr (std::is_same_v<OpType, FunctionCall>) {
                std::string r = ptr->name + "(";
                if (ptr->args.empty() && ptr->name == "count") {
                    r += "*";
                } else {
                    for (size_t i = 0; i < ptr->args.size(); i++) {
                        if (i > 0)
                            r += ", ";
                        r += expressionToString(ptr->args[i]);
                    }
                }
                return r + ")";
            } else if constexpr (std::is_same_v<OpType, SubscriptExpr>) {
                return expressionToString(ptr->list) + "[" + expressionToString(ptr->index) + "]";
            } else if constexpr (std::is_same_v<OpType, SliceExpr>) {
                std::string r = expressionToString(ptr->list) + "[";
                if (ptr->from)
                    r += expressionToString(*ptr->from);
                r += "..";
                if (ptr->to)
                    r += expressionToString(*ptr->to);
                r += "]";
                return r;
            } else {
                return "?";
            }
        },
        expr);
}

inline Expression stripParens(Expression expr) {
    if (auto* paren = std::get_if<std::unique_ptr<ParenExpr>>(&expr)) {
        if (*paren)
            return stripParens(std::move((*paren)->inner));
        return expr;
    }
    // Recurse into compound expression types that hold sub-expressions.
    if (auto* bin = std::get_if<std::unique_ptr<BinaryOp>>(&expr)) {
        if (*bin) {
            (*bin)->left = stripParens(std::move((*bin)->left));
            (*bin)->right = stripParens(std::move((*bin)->right));
        }
        return expr;
    }
    if (auto* un = std::get_if<std::unique_ptr<UnaryOp>>(&expr)) {
        if (*un) {
            (*un)->operand = stripParens(std::move((*un)->operand));
        }
        return expr;
    }
    if (auto* fc = std::get_if<std::unique_ptr<FunctionCall>>(&expr)) {
        if (*fc) {
            for (auto& a : (*fc)->args)
                a = stripParens(std::move(a));
        }
        return expr;
    }
    if (auto* pa = std::get_if<std::unique_ptr<PropertyAccess>>(&expr)) {
        if (*pa) {
            (*pa)->object = stripParens(std::move((*pa)->object));
        }
        return expr;
    }
    if (auto* lc = std::get_if<std::unique_ptr<LabelCastExpr>>(&expr)) {
        if (*lc) {
            (*lc)->object = stripParens(std::move((*lc)->object));
        }
        return expr;
    }
    if (auto* le = std::get_if<std::unique_ptr<ListExpr>>(&expr)) {
        if (*le) {
            for (auto& e : (*le)->elements)
                e = stripParens(std::move(e));
        }
        return expr;
    }
    if (auto* me = std::get_if<std::unique_ptr<MapExpr>>(&expr)) {
        if (*me) {
            for (auto& [k, v] : (*me)->entries)
                v = stripParens(std::move(v));
        }
        return expr;
    }
    if (auto* ce = std::get_if<std::unique_ptr<CaseExpr>>(&expr)) {
        if (*ce) {
            if ((*ce)->subject)
                (*ce)->subject = stripParens(std::move(*(*ce)->subject));
            for (auto& [w, t] : (*ce)->when_thens) {
                w = stripParens(std::move(w));
                t = stripParens(std::move(t));
            }
            if ((*ce)->else_expr)
                (*ce)->else_expr = stripParens(std::move(*(*ce)->else_expr));
        }
        return expr;
    }
    if (auto* lc2 = std::get_if<std::unique_ptr<ListComprehension>>(&expr)) {
        if (*lc2) {
            (*lc2)->list_expr = stripParens(std::move((*lc2)->list_expr));
            if ((*lc2)->where_pred)
                (*lc2)->where_pred = stripParens(std::move(*(*lc2)->where_pred));
            if ((*lc2)->projection)
                (*lc2)->projection = stripParens(std::move(*(*lc2)->projection));
        }
        return expr;
    }
    if (auto* sub = std::get_if<std::unique_ptr<SubscriptExpr>>(&expr)) {
        if (*sub) {
            (*sub)->list = stripParens(std::move((*sub)->list));
            (*sub)->index = stripParens(std::move((*sub)->index));
        }
        return expr;
    }
    if (auto* sl = std::get_if<std::unique_ptr<SliceExpr>>(&expr)) {
        if (*sl) {
            (*sl)->list = stripParens(std::move((*sl)->list));
            if ((*sl)->from)
                (*sl)->from = stripParens(std::move(*(*sl)->from));
            if ((*sl)->to)
                (*sl)->to = stripParens(std::move(*(*sl)->to));
        }
        return expr;
    }
    return expr;
}

} // namespace cypher
} // namespace eugraph
