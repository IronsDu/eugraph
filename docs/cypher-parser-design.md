# Cypher Parser 设计

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

---

## 一、技术选型

| 决策 | 选择 | 原因 |
|------|------|------|
| Parser Generator | ANTLR4 | 成熟稳定，社区已有高质量 Cypher .g4 语法 |
| C++ 运行时 | vcpkg `antlr4` | 与项目依赖管理一致 |
| 代码生成 | 预生成 C++ 代码提交仓库 | 避免构建时依赖 Java/ANTLR JAR |
| Grammar 来源 | `antlr/grammars-v4/cypher` | BSD 许可，覆盖完整 openCypher 语法 |

---

## 二、Grammar 与代码生成

Grammar 文件位于 `grammar/CypherLexer.g4` 和 `grammar/CypherParser.g4`，从 grammars-v4 复制，不修改。特性：
- `caseInsensitive = true`，自动处理大小写无关关键字
- 覆盖 MATCH, CREATE, MERGE, DELETE, SET, REMOVE, RETURN, WITH, UNION, UNWIND 等全部子句

ANTLR 生成的 C++ 代码（Visitor + Lexer/Parser）**预生成后直接提交到源码树**，CMake 编译时直接使用，不依赖 Java/JAR。

---

## 三、AST 设计

### 设计原则

1. **与 ANTLR 解耦**：AST 是独立数据结构，不持有 ANTLR 类型引用
2. **值语义**：`variant<unique_ptr<T>>` 模式，可自由拷贝/移动
3. **精简**：只保留语义有意义的信息，丢弃括号、分号等语法细节

### 节点层次

```
Statement → variant<RegularQuery, StandaloneCall>
  RegularQuery → SingleQuery + unions
    SingleQuery → vector<Clause>
      Clause → variant<MatchClause, CreateClause, ReturnClause, WithClause,
                       DeleteClause, SetClause, RemoveClause, MergeClause,
                       UnwindClause, CallClause>
        Expression → variant<Literal, Variable, BinaryOp, UnaryOp, FunctionCall,
                             PropertyAccess, ListExpr, MapExpr, CaseExpr, ...>
          Pattern → PatternPart → PatternElement → NodePattern + RelationshipPattern
```

完整 AST 节点定义见 [ast-reference.md](ast-reference.md)。

### 核心类型

- **Expression**：19 种 variant 类型，用 `unique_ptr` 实现递归
- **Literal**：`variant<NullValue, bool, int64_t, double, string>`
- **BinaryOp**：20 种操作符（算术、比较、字符串、逻辑、列表）
- **UnaryOp**：5 种操作符（NOT, NEGATE, PLUS, IS_NULL, IS_NOT_NULL）

---

## 四、AST 构建（AstBuilder）

`AstBuilder` **直接遍历 ANTLR Parse Tree**（不继承 `BaseVisitor`）。原因：ANTLR `BaseVisitor` 返回 `std::any`，与 move-only 的 `unique_ptr` AST 类型不兼容，直接遍历避免了 `std::any_cast` 和类型安全问题。

实现文件：`src/compute_service/parser/cypher_parser.cpp`

### 错误处理

使用 `BailErrorStrategy` 快速失败（第一个语法错误即停止），自定义 `ParseErrorListener` 收集错误信息转换为 `ParseError{message, line, column}`。

---

## 五、对外接口

```cpp
class CypherQueryParser {
    std::variant<Statement, ParseError> parse(const std::string& cypher_text);
};
```

线程安全：每次调用创建独立的 ANTLR lexer/parser 实例。

---

## 六、文件结构

```
grammar/
  CypherLexer.g4
  CypherParser.g4
src/compute_service/parser/
  ast.hpp              — AST 节点定义
  cypher_parser.hpp    — 对外接口
  cypher_parser.cpp    — AstBuilder + CypherQueryParser 实现
  index_ddl_parser.hpp — 索引 DDL 解析器
  index_ddl_parser.cpp
```

---

## 七、设计决策

| # | 决策 | 原因 |
|---|------|------|
| 1 | ANTLR4 而非手写 Parser | Cypher 语法复杂（300+ 产生式），.g4 维护成本低 |
| 2 | 直接遍历 Parse Tree 而非 Visitor | `std::any` 与 move-only AST 不兼容 |
| 3 | AST 与 ANTLR 解耦 | Planner 不依赖 ANTLR 运行时；AST 更精简 |
| 4 | Grammar 文件不修改 | 保持与上游一致，便于后续更新 |
| 5 | `variant<unique_ptr<T>>` 模式 | 值语义、类型安全、无手动内存管理 |
| 6 | 预生成 C++ 代码提交仓库 | 避免构建时 Java 依赖，简化 CI |
