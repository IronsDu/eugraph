# Cypher Parser + AST 设计方案

## 一、背景与目标

eugraph 需要支持 Cypher 查询语言（而非原始规划中的 GSQL），作为 `Compute Service` 的查询入口。本设计覆盖从 Cypher 文本字符串到 AST（抽象语法树）的完整解析流程。

**范围**：
- Cypher 文本的词法分析（Lexer）
- Cypher 文本的语法分析（Parser）
- AST 节点类型定义
- ANTLR4 与 CMake 的集成方案

**不在范围内**：
- 查询规划（Planner）和执行（Executor）-- 后续设计
- 查询优化
- 完整的 openCypher 兼容性测试套件

---

## 二、技术选型

### 2.1 Parser Generator: ANTLR4

| 项目 | 决策 | 原因 |
|------|------|------|
| Parser Generator | ANTLR4 | 成熟稳定，支持 C++ target，有完整的 Cypher .g4 语法文件 |
| C++ 运行时来源 | vcpkg `antlr4` 包 | 与项目现有依赖管理一致，避免源码编译 |
| 代码生成方式 | CMake `add_custom_command` | 构建时自动从 .g4 生成 C++ 源码，无需手动步骤 |
| Grammar 来源 | `antlr/grammars-v4/cypher` | ANTLR4 .g4 格式，BSD 许可，覆盖完整 openCypher 语法 |

### 2.2 为什么选择 ANTLR4 而非手写 Parser

1. Cypher 语法复杂度高（300+ 产生式），手写维护成本大
2. ANTLR4 自动生成 Visitor/Listener，方便构建 AST
3. 社区已有高质量的 Cypher .g4 语法，经过充分测试
4. C++ target 性能满足数据库需求

---

## 三、Grammar 文件

### 3.1 来源

使用 `antlr/grammars-v4/cypher` 仓库的 `CypherLexer.g4` 和 `CypherParser.g4`。

- **许可**：BSD 3-Clause
- **维护者**：Boris Zhguchev (2022)
- **特性**：
  - `caseInsensitive = true`，自动处理大小写无关的 Cypher 关键字
  - 覆盖 openCypher TCK 所需的全部语法
  - 支持 MATCH, CREATE, MERGE, DELETE, SET, REMOVE, RETURN, WITH, UNION, UNWIND, WHERE, ORDER BY, LIMIT, SKIP, CALL, YIELD 等完整子句

### 3.2 Grammar 文件存放位置

```
eugraph/
├── grammar/                    # 新增目录
│   ├── CypherLexer.g4          # 词法规则
│   └── CypherParser.g4         # 语法规则
└── src/
    └── compute_service/
        └── parser/
            ├── cypher_parser.hpp    # 对外封装接口
            └── ast_visitor.hpp      # AST 构建器
```

Grammar 文件直接从 grammars-v4/cypher 复制，不做修改。如果未来需要扩展语法（如添加自定义函数），通过继承或包装的方式扩展，不直接修改 .g4 文件。

### 3.3 ANTLR 生成的代码

ANTLR4 生成的 C++ 文件将输出到 `${CMAKE_BINARY_DIR}/generated/cypher/` 目录，包括：

```
build/generated/cypher/
├── CypherLexer.cpp
├── CypherLexer.h
├── CypherParser.cpp
├── CypherParser.h
├── CypherParserBaseVisitor.cpp
├── CypherParserBaseVisitor.h
├── CypherParserVisitor.cpp
└── CypherParserVisitor.h
```

这些文件不纳入版本控制（加入 .gitignore）。

---

## 四、CMake 集成方案

### 4.1 依赖添加

在 `vcpkg.json` 中添加 `antlr4` 依赖：

```json
{
  "dependencies": [
    "antlr4",
    "rocksdb",
    "gtest",
    "spdlog",
    "lz4",
    "zlib",
    "zstd"
  ]
}
```

### 4.2 CMake 配置

在根 `CMakeLists.txt` 中添加：

```cmake
# ANTLR4 runtime
find_package(antlr4-runtime REQUIRED)

# ANTLR4 code generation
find_package(Java COMPONENTS Runtime REQUIRED)

set(ANTLR4_JAR "${PROJECT_SOURCE_DIR}/third_party/antlr-4.13.2-complete.jar")
set(GRAMMAR_DIR "${PROJECT_SOURCE_DIR}/grammar")
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/cypher")

# Generate Cypher parser from .g4 files
add_custom_command(
    OUTPUT
        "${GENERATED_DIR}/CypherLexer.cpp"
        "${GENERATED_DIR}/CypherLexer.h"
        "${GENERATED_DIR}/CypherParser.cpp"
        "${GENERATED_DIR}/CypherParser.h"
        "${GENERATED_DIR}/CypherParserBaseVisitor.cpp"
        "${GENERATED_DIR}/CypherParserBaseVisitor.h"
        "${GENERATED_DIR}/CypherParserVisitor.cpp"
        "${GENERATED_DIR}/CypherParserVisitor.h"
    COMMAND ${Java_JAVA_EXECUTABLE} -jar "${ANTLR4_JAR}"
        -Dlanguage=Cpp
        -visitor
        -no-listener
        -o "${GENERATED_DIR}"
        -package eugraph::cypher
        "${GRAMMAR_DIR}/CypherLexer.g4"
        "${GRAMMAR_DIR}/CypherParser.g4"
    DEPENDS
        "${GRAMMAR_DIR}/CypherLexer.g4"
        "${GRAMMAR_DIR}/CypherParser.g4"
    COMMENT "Generating Cypher parser from ANTLR grammar"
)
```

关键决策：
- `-visitor` 生成 Visitor 接口（用于 AST 构建）
- `-no-listener` 不生成 Listener（我们只使用 Visitor 模式）
- `-package eugraph::cypher` 放入项目命名空间

### 4.3 编译目标

创建静态库目标 `eugraph_parser_lib`：

```cmake
add_library(eugraph_parser_lib STATIC
    ${PARSER_SOURCES}  # ANTLR 生成的 .cpp 文件
    src/compute_service/parser/cypher_parser.cpp
    src/compute_service/parser/ast_visitor.cpp
    src/compute_service/parser/ast.cpp
)

target_include_directories(eugraph_parser_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${GENERATED_DIR}"
)

target_link_libraries(eugraph_parser_lib PUBLIC
    antlr4-runtime::antlr4_runtime
    spdlog::spdlog
)
```

---

## 五、AST 节点设计

### 5.1 设计原则

1. **与 ANTLR Parse Tree 解耦**：AST 是独立的数据结构，不持有任何 ANTLR 运行时类型的引用
2. **值语义**：AST 节点使用 `std::variant` + `std::unique_ptr`，可自由拷贝/移动
3. **类型安全**：每种 AST 节点是一个明确的 C++ struct，不做运行时类型转换
4. **精简**：只保留语义上有意义的信息，丢弃括号、分号等语法细节

### 5.2 AST 节点层次

```
ASTNode (基类)
├── Statement
│   ├── Query
│   │   ├── RegularQuery      (singleQuery + UNION clauses)
│   │   └── StandaloneCall    (CALL ... YIELD ...)
│   └── AdministrationCommand (future: CREATE GRAPH, etc.)
│
├── SingleQuery
│   ├── ReadingClause[]
│   ├── UpdatingClause[]
│   └── ReturnClause
│
├── Clause (子句)
│   ├── MatchClause           (OPTIONAL MATCH ...)
│   ├── CreateClause
│   ├── MergeClause
│   ├── DeleteClause          (DETACH DELETE ...)
│   ├── SetClause
│   ├── RemoveClause
│   ├── UnwindClause
│   ├── WithClause
│   ├── ReturnClause
│   └── CallClause
│
├── Pattern (模式)
│   ├── PatternPart           (变量 = 模式)
│   ├── NodePattern           (变量:标签 {属性})
│   └── RelationshipPattern   (-[变量:类型 {属性}]->)
│
├── Expression (表达式)
│   ├── Literal               (42, "hello", true, null)
│   ├── Variable              (x, n)
│   ├── BinaryOp              (+, -, *, /, =, <>, <, >, <=, >=, AND, OR, XOR)
│   ├── UnaryOp               (NOT, -, +)
│   ├── FunctionCall          (count(x), sum(x), ...)
│   ├── PropertyAccess        (n.name)
│   ├── ListExpr              ([1, 2, 3])
│   ├── MapExpr               ({key: value})
│   ├── CaseExpr              (CASE WHEN ... THEN ... ELSE ... END)
│   ├── ListComprehension     ([x IN list WHERE ... | ...])
│   ├── PatternComprehension  ([(n)-->(m) | m.name])
│   ├── Parameter             ($param)
│   ├── SubscriptExpr         (list[0])
│   ├── SliceExpr             (list[1..3])
│   └── ExistsExpr            (EXISTS { MATCH ... })
│
├── OrderBy
│   ├── SortItem              (expression ASC/DESC)
│
└── SetItem
    ├── SetProperty           (n.name = expr)
    ├── SetProperties         (n = {props})
    └── SetLabels             (n:Label)
```

### 5.3 核心 AST 类型定义

```cpp
// src/compute_service/parser/ast.hpp

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace eugraph {
namespace cypher {

// ==================== 前向声明 ====================

struct Literal;
struct Variable;
struct BinaryOp;
struct UnaryOp;
struct FunctionCall;
struct PropertyAccess;
struct ListExpr;
struct MapExpr;
struct CaseExpr;
struct ListComprehension;
struct PatternComprehension;
struct Parameter;
struct SubscriptExpr;
struct SliceExpr;
struct ExistsExpr;
struct AllExpr;
struct AnyExpr;
struct NoneExpr;
struct SingleExpr;

// ==================== Expression ====================

using Expression = std::variant<
    std::unique_ptr<Literal>,
    std::unique_ptr<Variable>,
    std::unique_ptr<BinaryOp>,
    std::unique_ptr<UnaryOp>,
    std::unique_ptr<FunctionCall>,
    std::unique_ptr<PropertyAccess>,
    std::unique_ptr<ListExpr>,
    std::unique_ptr<MapExpr>,
    std::unique_ptr<CaseExpr>,
    std::unique_ptr<ListComprehension>,
    std::unique_ptr<PatternComprehension>,
    std::unique_ptr<Parameter>,
    std::unique_ptr<SubscriptExpr>,
    std::unique_ptr<SliceExpr>,
    std::unique_ptr<ExistsExpr>,
    std::unique_ptr<AllExpr>,
    std::unique_ptr<AnyExpr>,
    std::unique_ptr<NoneExpr>,
    std::unique_ptr<SingleExpr>
>;

// ==================== Literal ====================

struct NullValue {};

struct Literal {
    std::variant<NullValue, bool, int64_t, double, std::string> value;
};

// ==================== Variable ====================

struct Variable {
    std::string name;
};

// ==================== BinaryOp ====================

enum class BinaryOperator {
    // 算术
    ADD, SUB, MUL, DIV, MOD, POW,
    // 比较
    EQ, NEQ, LT, GT, LTE, GTE,
    // 字符串
    STARTS_WITH, ENDS_WITH, CONTAINS,
    // 列表
    IN,
    // 正则
    REGEX_MATCH,
    // 逻辑
    AND, OR, XOR,
    // 列表拼接
    LIST_CONCAT,
};

struct BinaryOp {
    BinaryOperator op;
    Expression left;
    Expression right;
};

// ==================== UnaryOp ====================

enum class UnaryOperator {
    NOT, NEGATE, PLUS
};

struct UnaryOp {
    UnaryOperator op;
    Expression operand;
};

// ==================== FunctionCall ====================

struct FunctionCall {
    std::string name;
    bool distinct = false;            // count(DISTINCT x)
    std::vector<Expression> args;
};

// ==================== PropertyAccess ====================

struct PropertyAccess {
    Expression object;                // 通常是 Variable
    std::string property;
};

// ==================== ListExpr ====================

struct ListExpr {
    std::vector<Expression> elements;
};

// ==================== MapExpr ====================

struct MapExpr {
    // 保留键值对的顺序（Cypher 中 map 字面量有序）
    std::vector<std::pair<std::string, Expression>> entries;
};

// ==================== CaseExpr ====================

struct CaseExpr {
    std::optional<Expression> subject;     // CASE expr WHEN ... 或 CASE WHEN ...
    std::vector<std::pair<Expression, Expression>> when_thens;  // WHEN cond THEN result
    std::optional<Expression> else_expr;   // ELSE result
};

// ==================== ListComprehension ====================

struct ListComprehension {
    std::string variable;                  // x IN ...
    Expression list_expr;                  // ... IN list
    std::optional<Expression> where_pred;  // WHERE predicate
    std::optional<Expression> projection;  // | projection
};

// ==================== PatternComprehension ====================

struct PatternComprehension {
    std::optional<std::string> variable;   // [x IN]
    struct PatternFilter {
        std::vector<PatternPart> patterns;
        std::optional<Expression> where_pred;
    };
    PatternFilter pattern_filter;
    std::optional<Expression> projection;
};

// ==================== Parameter ====================

struct Parameter {
    std::string name;                      // $param_name 或 $123
};

// ==================== SubscriptExpr ====================

struct SubscriptExpr {
    Expression list;
    Expression index;
};

// ==================== SliceExpr ====================

struct SliceExpr {
    Expression list;
    std::optional<Expression> from;        // [from..to]
    std::optional<Expression> to;
};

// ==================== ExistsExpr ====================

struct ExistsExpr {
    // EXISTS { MATCH ... WHERE ... }
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
    // {key: expr, ...}
    std::vector<std::pair<std::string, Expression>> entries;
};

struct NodePattern {
    std::optional<std::string> variable;          // (x:Label {key: val})
    std::vector<std::string> labels;              // :Label1:Label2
    std::optional<PropertiesMap> properties;
};

enum class RelationshipDirection {
    LEFT_TO_RIGHT,   // -[]->
    RIGHT_TO_LEFT,   // <-[]-
    UNDIRECTED       // -[]-
};

struct RelationshipPattern {
    std::optional<std::string> variable;
    std::vector<std::string> rel_types;           // :TYPE1|TYPE2
    std::optional<PropertiesMap> properties;
    RelationshipDirection direction;
    std::optional<std::pair<Expression, Expression>> range;  // *min..max
};

struct PatternElement {
    NodePattern node;
    std::vector<std::pair<RelationshipPattern, NodePattern>> chain;  // 连续的 -[]->()
};

struct PatternPart {
    std::optional<std::string> variable;          // named path: p = (a)-->(b)
    PatternElement element;
};

// ==================== Clause ====================

struct MatchClause {
    bool optional = false;                        // OPTIONAL MATCH
    std::vector<PatternPart> patterns;
    std::optional<Expression> where_pred;         // 可能被提升到 Match 中
};

struct CreateClause {
    std::vector<PatternPart> patterns;
};

struct MergeClause {
    PatternPart pattern;
    std::vector<SetItem> on_create;               // ON CREATE SET ...
    std::vector<SetItem> on_match;                // ON MATCH SET ...
};

struct DeleteClause {
    bool detach = false;                          // DETACH DELETE
    std::vector<Expression> expressions;
};

enum class SetItemKind {
    SET_PROPERTY,       // n.prop = expr
    SET_PROPERTIES,     // n = {props}
    SET_DYNAMIC_PROPERTY, // n[expr] = expr
    SET_LABELS,         // n:Label
};

struct SetItem {
    SetItemKind kind;
    Expression target;                            // 变量或属性访问
    std::optional<Expression> value;              // 新值（SET_LABELS 无）
    std::string label;                            // SET_LABELS 使用
};

struct RemoveItem {
    enum class Kind { PROPERTY, LABEL };
    Kind kind;
    Expression target;
    std::string name;                             // 属性名或标签名
};

struct RemoveClause {
    std::vector<RemoveItem> items;
};

struct UnwindClause {
    Expression list_expr;
    std::string variable;
};

struct ReturnItem {
    Expression expr;
    std::optional<std::string> alias;             // expr AS alias
};

struct ReturnClause {
    bool distinct = false;
    std::vector<ReturnItem> items;
    std::optional<OrderBy> order_by;
    std::optional<Expression> skip;
    std::optional<Expression> limit;
};

struct WithClause {
    bool distinct = false;
    std::vector<ReturnItem> items;
    std::optional<OrderBy> order_by;
    std::optional<Expression> skip;
    std::optional<Expression> limit;
    std::optional<Expression> where_pred;
};

struct OrderBy {
    enum class Direction { ASC, DESC };
    struct SortItem {
        Expression expr;
        Direction direction = Direction::ASC;
    };
    std::vector<SortItem> items;
};

struct CallClause {
    std::string procedure_name;
    std::vector<Expression> args;
    std::vector<std::string> yield_items;         // YIELD x AS y
    std::optional<Expression> where_pred;
};

// ==================== Query ====================

// 一个 SingleQuery 由多个子句顺序组成
// 子句分为 Reading 和 Updating 两类
using Clause = std::variant<
    std::unique_ptr<MatchClause>,
    std::unique_ptr<UnwindClause>,
    std::unique_ptr<CallClause>,
    std::unique_ptr<CreateClause>,
    std::unique_ptr<MergeClause>,
    std::unique_ptr<DeleteClause>,
    std::unique_ptr<SetClause>,
    std::unique_ptr<RemoveClause>,
    std::unique_ptr<ReturnClause>,
    std::unique_ptr<WithClause>
>;

struct SingleQuery {
    std::vector<Clause> clauses;
};

struct UnionClause {
    bool all = false;                             // UNION ALL vs UNION
};

struct RegularQuery {
    SingleQuery first;
    std::vector<std::pair<UnionClause, SingleQuery>> unions;
};

struct StandaloneCall {
    std::string procedure_name;
    std::vector<Expression> args;
    std::vector<std::string> yield_items;
    std::optional<Expression> where_pred;
};

// ==================== 顶层 Statement ====================

using Statement = std::variant<
    std::unique_ptr<RegularQuery>,
    std::unique_ptr<StandaloneCall>
>;

} // namespace cypher
} // namespace eugraph
```

---

## 六、对外接口设计

### 6.1 CypherParser 封装类

将 ANTLR 的使用细节封装起来，对外只暴露纯 AST：

```cpp
// src/compute_service/parser/cypher_parser.hpp

#pragma once

#include "compute_service/parser/ast.hpp"
#include <memory>
#include <string>
#include <variant>

namespace eugraph {
namespace cypher {

struct ParseError {
    std::string message;
    int line = 0;
    int column = 0;
};

class CypherParser {
public:
    CypherParser();
    ~CypherParser();

    // 解析 Cypher 查询，成功返回 AST Statement，失败返回 ParseError
    // 线程安全：每次调用创建独立的 ANTLR parser 实例
    std::variant<Statement, ParseError> parse(const std::string& cypher_text);

private:
    // ANTLR 运行时的 Allocator，跨调用复用以提高性能
    // 注意：ANTLR C++ runtime 不是完全线程安全的，
    // parse() 内部每次创建独立的 lexer/parser/token stream
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cypher
} // namespace eugraph
```

### 6.2 使用示例

```cpp
#include "compute_service/parser/cypher_parser.hpp"

using namespace eugraph::cypher;

CypherParser parser;
auto result = parser.parse("MATCH (n:Person)-[:KNOWS]->(m) WHERE n.age > 30 RETURN n.name, m");

if (std::holds_alternative<Statement>(result)) {
    auto& stmt = std::get<Statement>(result);
    // stmt 是一个 RegularQuery
    // 后续交给 QueryPlanner 处理
} else {
    auto& err = std::get<ParseError>(result);
    spdlog::error("Parse error at {}:{}: {}", err.line, err.column, err.message);
}
```

---

## 七、AST 构建策略

### 7.1 Visitor 模式

继承 ANTLR 生成的 `CypherParserBaseVisitor`，重写每个 `visit*` 方法，将 ANTLR Parse Tree 转换为自定义 AST。

```cpp
// src/compute_service/parser/ast_visitor.hpp

#pragma once

// ANTLR 生成的头文件
#include "CypherParser.h"
#include "CypherParserBaseVisitor.h"

#include "compute_service/parser/ast.hpp"

namespace eugraph {
namespace cypher {

class AstBuilder : public CypherParserBaseVisitor {
public:
    // 顶层入口
    std::variant<Statement, ParseError> visitScript(CypherParser::ScriptContext* ctx) override;

    // Query 层
    std::any visitRegularQuery(CypherParser::RegularQueryContext* ctx) override;
    std::any visitSingleQuery(CypherParser::SingleQueryContext* ctx) override;
    std::any visitStandaloneCall(CypherParser::StandaloneCallContext* ctx) override;

    // Clause 层
    std::any visitMatchSt(CypherParser::MatchStContext* ctx) override;
    std::any visitCreateSt(CypherParser::CreateStContext* ctx) override;
    std::any visitMergeSt(CypherParser::MergeStContext* ctx) override;
    std::any visitDeleteSt(CypherParser::DeleteStContext* ctx) override;
    std::any visitSetSt(CypherParser::SetStContext* ctx) override;
    std::any visitRemoveSt(CypherParser::RemoveStContext* ctx) override;
    std::any visitUnwindSt(CypherParser::UnwindStContext* ctx) override;
    std::any visitReturnSt(CypherParser::ReturnStContext* ctx) override;
    std::any visitWithSt(CypherParser::WithStContext* ctx) override;

    // Expression 层（按优先级链）
    std::any visitExpression(CypherParser::ExpressionContext* ctx) override;
    std::any visitXorExpression(CypherParser::XorExpressionContext* ctx) override;
    std::any visitAndExpression(CypherParser::AndExpressionContext* ctx) override;
    std::any visitNotExpression(CypherParser::NotExpressionContext* ctx) override;
    std::any visitComparisonExpression(CypherParser::ComparisonExpressionContext* ctx) override;
    std::any visitAddSubExpression(CypherParser::AddSubExpressionContext* ctx) override;
    std::any visitMultDivExpression(CypherParser::MultDivExpressionContext* ctx) override;
    std::any visitPowerExpression(CypherParser::PowerExpressionContext* ctx) override;
    std::any visitUnaryAddSubExpression(CypherParser::UnaryAddSubExpressionContext* ctx) override;
    std::any visitAtomicExpression(CypherParser::AtomicExpressionContext* ctx) override;

    // Pattern 层
    std::any visitPatternPart(CypherParser::PatternPartContext* ctx) override;
    std::any visitPatternElem(CypherParser::PatternElemContext* ctx) override;
    std::any visitNodePattern(CypherParser::NodePatternContext* ctx) override;
    std::any visitRelationshipPattern(CypherParser::RelationshipPatternContext* ctx) override;

private:
    // 辅助方法
    Expression buildExpression(CypherParser::ExpressionContext* ctx);
    PatternPart buildPatternPart(CypherParser::PatternPartContext* ctx);
    NodePattern buildNodePattern(CypherParser::NodePatternContext* ctx);
    std::vector<SetItem> buildSetItems(CypherParser::SetStContext* ctx);
};

} // namespace cypher
} // namespace eugraph
```

### 7.2 错误处理

使用 ANTLR 的 `BailErrorStrategy` 实现快速失败（遇到第一个语法错误就停止），并捕获错误信息转换为 `ParseError`：

```cpp
// 在 CypherParser::parse() 实现中
auto lexer = std::make_unique<CypherLexer>(&input);
auto tokens = std::make_unique<antlr4::CommonTokenStream>(lexer.get());
auto parser = std::make_unique<CypherParser>(tokens.get());

// 自定义错误监听器
ParseErrorListener error_listener;
lexer->removeErrorListeners();
parser->removeErrorListeners();
lexer->addErrorListener(&error_listener);
parser->addErrorListener(&error_listener);

auto tree = parser->script();
if (error_listener.hasError()) {
    return error_listener.getError();
}

AstBuilder builder;
auto result = builder.visitScript(tree);
return std::any_cast<Statement>(std::move(result));
```

---

## 八、目录结构（更新后）

```
eugraph/
├── grammar/                              # 新增：Grammar 文件
│   ├── CypherLexer.g4
│   └── CypherParser.g4
├── src/
│   ├── common/                           # 已有
│   │   └── types/
│   ├── storage/                          # 已有
│   │   ├── graph_store.hpp
│   │   ├── graph_store_impl.hpp
│   │   └── kv/
│   ├── compute_service/                  # 新增
│   │   └── parser/
│   │       ├── ast.hpp                   # AST 节点定义
│   │       ├── ast.cpp                   # AST 辅助实现（toString 等）
│   │       ├── cypher_parser.hpp         # 对外解析接口
│   │       ├── cypher_parser.cpp         # 解析实现（封装 ANTLR）
│   │       ├── ast_visitor.hpp           # AST 构建器声明
│   │       └── ast_visitor.cpp           # AST 构建器实现
│   └── main.cpp
├── tests/
│   ├── test_key_codec.cpp                # 已有
│   ├── test_value_codec.cpp              # 已有
│   ├── test_graph_store_wt.cpp           # 已有
│   ├── test_cypher_parser.cpp            # 新增：Parser + AST 测试
│   └── test_wiredtiger.cpp               # 已有
├── third_party/
│   ├── wiredtiger/                       # 已有
│   └── antlr-4.13.2-complete.jar         # 新增：ANTLR 工具 JAR
└── docs/
    ├── cypher-parser-design.md           # 本文档
    └── ...
```

---

## 九、ANTLR JAR 管理

ANTLR 工具（`antlr-4.13.2-complete.jar`）是构建时工具，不是运行时依赖。

存放方式：放入 `third_party/antlr-4.13.2-complete.jar`，纳入 git 版本控制（约 3MB）。

开发者在首次构建时无需单独安装 ANTLR，CMake 自动使用该 JAR 文件。

---

## 十、设计决策记录

| # | 决策 | 备选方案 | 选择原因 |
|---|------|---------|---------|
| 1 | 使用 ANTLR4 而非手写 Parser | 手写递归下降 Parser | Cypher 语法复杂，维护 .g4 文件远比手写容易 |
| 2 | 使用 Visitor 而非 Listener 构建 AST | Listener + 栈 | Visitor 可自然返回值，代码更清晰 |
| 3 | AST 与 ANTLR Parse Tree 解耦 | 直接使用 ANTLR Context 对象作为 AST | 解耦后 Planner 不依赖 ANTLR 运行时；AST 更精简 |
| 4 | Grammar 文件不修改 | 修改 .g4 适配项目 | 保持与上游一致，便于后续更新 |
| 5 | 使用 `std::variant` + `std::unique_ptr` | 类继承 + 虚函数 | 值语义，内存安全，避免手动内存管理 |
| 6 | JAR 文件纳入版本控制 | 构建时从 Maven 下载 | 确保构建可重复性，避免网络依赖 |
| 7 | 生成的代码放入 build 目录 | 纳入 src 目录 | 不污染源码树，构建产物可随时重新生成 |

---

## 十一、实现计划

实现分为以下步骤（对应 plan.md 中的子任务）：

1. **准备 Grammar 文件**：从 grammars-v4/cypher 复制 .g4 文件到 `grammar/` 目录
2. **下载 ANTLR JAR**：下载 `antlr-4.13.2-complete.jar` 到 `third_party/`
3. **更新 CMake 配置**：
   - `vcpkg.json` 添加 `antlr4`
   - `CMakeLists.txt` 添加代码生成规则和编译目标
4. **实现 AST 节点**：`ast.hpp`
5. **实现 AstBuilder**：`ast_visitor.hpp/.cpp`
6. **实现 CypherParser 封装**：`cypher_parser.hpp/.cpp`
7. **编写测试**：`test_cypher_parser.cpp`

---

## 十二、测试策略

### 12.1 解析正确性测试

对每种 Cypher 子句编写测试用例：

```cpp
TEST(CypherParserTest, SimpleMatch) {
    CypherParser parser;
    auto result = parser.parse("MATCH (n:Person) RETURN n");
    ASSERT_TRUE(std::holds_alternative<Statement>(result));
}

TEST(CypherParserTest, MatchWithWhere) {
    CypherParser parser;
    auto result = parser.parse("MATCH (n:Person) WHERE n.age > 30 RETURN n.name");
    ASSERT_TRUE(std::holds_alternative<Statement>(result));
}

TEST(CypherParserTest, CreateNodes) {
    CypherParser parser;
    auto result = parser.parse("CREATE (n:Person {name: 'Alice', age: 30})");
    ASSERT_TRUE(std::holds_alternative<Statement>(result));
}

TEST(CypherParserTest, MatchAndCreate) {
    CypherParser parser;
    auto result = parser.parse(
        "MATCH (n:Person) CREATE (m:Person {name: n.name})"
    );
    ASSERT_TRUE(std::holds_alternative<Statement>(result));
}

TEST(CypherParserTest, UnionQuery) {
    CypherParser parser;
    auto result = parser.parse(
        "MATCH (n:Person) RETURN n.name UNION MATCH (m:Animal) RETURN m.name"
    );
    ASSERT_TRUE(std::holds_alternative<Statement>(result));
}

TEST(CypherParserTest, CaseInsensitiveKeywords) {
    CypherParser parser;
    auto result = parser.parse("match (n:Person) return n");
    ASSERT_TRUE(std::holds_alternative<Statement>(result));
}
```

### 12.2 错误报告测试

```cpp
TEST(CypherParserTest, SyntaxError) {
    CypherParser parser;
    auto result = parser.parse("MATCHH (n:Person) RETURN n");
    ASSERT_TRUE(std::holds_alternative<ParseError>(result));
    auto& err = std::get<ParseError>(result);
    EXPECT_GT(err.line, 0);
    EXPECT_FALSE(err.message.empty());
}
```

### 12.3 AST 结构验证

在测试中访问 AST 节点，验证结构正确：

```cpp
TEST(CypherParserTest, AstStructure) {
    CypherParser parser;
    auto result = parser.parse("MATCH (n:Person)-[:KNOWS]->(m) RETURN n");

    auto& stmt = std::get<Statement>(result);
    auto& query = std::get<std::unique_ptr<RegularQuery>>(stmt);
    auto& first = query->first;

    // 第一个 clause 应该是 MatchClause
    ASSERT_EQ(first.clauses.size(), 2);  // Match + Return

    auto& match = std::get<std::unique_ptr<MatchClause>>(first.clauses[0]);
    EXPECT_FALSE(match->optional);
    EXPECT_EQ(match->patterns.size(), 1);

    // 验证 Pattern 结构
    auto& part = match->patterns[0];
    EXPECT_EQ(part.element.chain.size(), 1);  // 一个关系链

    auto& node = part.element.node;
    EXPECT_TRUE(node.variable.has_value());
    EXPECT_EQ(node.variable.value(), "n");
    EXPECT_EQ(node.labels.size(), 1);
    EXPECT_EQ(node.labels[0], "Person");
}
```

### 12.4 复杂查询测试

```cpp
TEST(CypherParserTest, ComplexQuery) {
    CypherParser parser;
    auto result = parser.parse(R"(
        MATCH (n:Person)-[:KNOWS]->(m:Person)
        WHERE n.age > 30 AND m.city = 'Beijing'
        WITH n, count(m) AS friend_count
        ORDER BY friend_count DESC
        LIMIT 10
        RETURN n.name, friend_count
    )");
    ASSERT_TRUE(std::holds_alternative<Statement>(result));
}
```
