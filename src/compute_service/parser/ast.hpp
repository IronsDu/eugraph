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

// ==================== Expression ====================
// 递归类型用 unique_ptr 包装

using Expression = std::variant<std::unique_ptr<Literal>, std::unique_ptr<Variable>, std::unique_ptr<Parameter>,
                                std::unique_ptr<BinaryOp>, std::unique_ptr<UnaryOp>, std::unique_ptr<FunctionCall>,
                                std::unique_ptr<PropertyAccess>, std::unique_ptr<ListExpr>, std::unique_ptr<MapExpr>,
                                std::unique_ptr<CaseExpr>, std::unique_ptr<ListComprehension>,
                                std::unique_ptr<PatternComprehension>, std::unique_ptr<SubscriptExpr>,
                                std::unique_ptr<SliceExpr>, std::unique_ptr<ExistsExpr>, std::unique_ptr<AllExpr>,
                                std::unique_ptr<AnyExpr>, std::unique_ptr<NoneExpr>, std::unique_ptr<SingleExpr>>;

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

// ==================== 顶层 Statement ====================

using Statement = std::variant<std::unique_ptr<RegularQuery>, std::unique_ptr<StandaloneCall>>;

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

inline Expression makeFunctionCall(std::string name, std::vector<Expression> args, bool distinct = false) {
    return std::make_unique<FunctionCall>(FunctionCall{std::move(name), distinct, std::move(args)});
}

} // namespace cypher
} // namespace eugraph
