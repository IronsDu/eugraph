# AST 节点参考

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

源码：`src/query/parser/ast.hpp`

---

## 一、顶层结构

```
Statement = variant<unique_ptr<RegularQuery>, unique_ptr<StandaloneCall>,
                     unique_ptr<ExplainStatement>, unique_ptr<ProfileStatement>>

RegularQuery
  ├── SingleQuery first
  └── vector<pair<UnionClause, SingleQuery>> unions

SingleQuery
  └── vector<Clause> clauses

Clause = variant<unique_ptr<MatchClause>, unique_ptr<UnwindClause>, unique_ptr<CallClause>,
                 unique_ptr<CreateClause>, unique_ptr<MergeClause>, unique_ptr<DeleteClause>,
                 unique_ptr<SetClause>, unique_ptr<RemoveClause>, unique_ptr<ReturnClause>,
                 unique_ptr<WithClause>, unique_ptr<ForeachClause>>
```

---

## 二、子句（Clause）

| 子句 | 关键字段 |
|------|----------|
| `MatchClause` | `optional`, `patterns: vector<PatternPart>`, `where_pred: optional<Expression>` |
| `CreateClause` | `patterns: vector<PatternPart>` |
| `ReturnClause` | `distinct`, `return_all`, `items: vector<ReturnItem>`, `order_by`, `skip`, `limit` |
| `WithClause` | 同 ReturnClause + `where_pred` |
| `DeleteClause` | `detach`, `expressions: vector<Expression>` |
| `SetClause` | `items: vector<SetItem>` |
| `RemoveClause` | `items: vector<RemoveItem>` |
| `MergeClause` | `pattern`, `on_create`, `on_match` |
| `UnwindClause` | `list_expr`, `variable` |
| `CallClause` | `procedure_name`, `args`, `yield_items`, `where_pred` |
| `ForeachClause` | `variable`, `list_expr`, `body: vector<Clause>` |

### ReturnItem

```cpp
struct ReturnItem { Expression expr; optional<string> alias; };
```

### OrderBy

```cpp
struct OrderBy { vector<SortItem> items; };
struct SortItem { Expression expr; Direction direction; };  // ASC / DESC
```

---

## 三、模式（Pattern）

```
PatternPart { optional<string> variable; PatternElement element; }

PatternElement { NodePattern node; vector<pair<RelationshipPattern, NodePattern>> chain; }

NodePattern { optional<string> variable; vector<string> labels; optional<PropertiesMap> properties; }

RelationshipPattern { optional<string> variable; vector<string> rel_types;
                      optional<PropertiesMap> properties; RelationshipDirection direction;
                      optional<pair<Expression, Expression>> range; }
```

`RelationshipDirection`: `LEFT_TO_RIGHT` / `RIGHT_TO_LEFT` / `UNDIRECTED`

---

## 四、表达式（Expression）

```cpp
Expression = variant<
    unique_ptr<Literal>,        // 字面量
    unique_ptr<Variable>,       // 变量引用
    unique_ptr<Parameter>,      // $param
    unique_ptr<BinaryOp>,       // 二元运算
    unique_ptr<UnaryOp>,        // 一元运算
    unique_ptr<FunctionCall>,   // 函数调用
    unique_ptr<PropertyAccess>, // .属性访问
    unique_ptr<LabelCastExpr>,  // n::Label 转型表达式
    unique_ptr<ParenExpr>,      // (expr) 括号表达式
    unique_ptr<ListExpr>,       // [1, 2, 3]
    unique_ptr<MapExpr>,        // {k: v}
    unique_ptr<CaseExpr>,       // CASE WHEN THEN ELSE END
    unique_ptr<ListComprehension>,    // [x IN list WHERE pred | proj]
    unique_ptr<PatternComprehension>, // [(a)-->(b) WHERE pred | proj]
    unique_ptr<SubscriptExpr>,  // list[index]
    unique_ptr<SliceExpr>,      // list[from..to]
    unique_ptr<ExistsExpr>,     // EXISTS { pattern }
    unique_ptr<AllExpr>,        // ALL(x IN list WHERE pred)
    unique_ptr<AnyExpr>,        // ANY(...)
    unique_ptr<NoneExpr>,       // NONE(...)
    unique_ptr<SingleExpr>      // SINGLE(...)
>;
```

### Literal

```cpp
Literal { variant<NullValue, bool, int64_t, double, string> value; }
```

### BinaryOp

```cpp
BinaryOp { BinaryOperator op; Expression left; Expression right; }
```

BinaryOperator 枚举：`ADD, SUB, MUL, DIV, MOD, POW, EQ, NEQ, LT, GT, LTE, GTE, STARTS_WITH, ENDS_WITH, CONTAINS, IN, AND, OR, XOR, LIST_CONCAT`

### UnaryOp

```cpp
UnaryOp { UnaryOperator op; Expression operand; }
```

UnaryOperator 枚举：`NOT, NEGATE, PLUS, IS_NULL, IS_NOT_NULL`

### FunctionCall

```cpp
FunctionCall { string name; bool distinct; vector<Expression> args; }
```

### PropertyAccess

```cpp
PropertyAccess { Expression object; string property; }
```

### LabelCastExpr

```cpp
LabelCastExpr { Expression object; string label; }
```

用于 `n::Label` 转型语法，将属性查找范围限定到指定标签。可嵌套 PropertyAccess（`n::Label.prop`）或单独使用（`RETURN n::Label` 返回该标签全部属性）。

### ParenExpr

```cpp
ParenExpr { Expression inner; }
```

保留括号信息，供后续处理阶段感知括号边界（如 `hoistAtomicPredicates` 不跨 `ParenExpr` 提升原子谓词）。使用 `stripParens()` 可在不需要括号信息的阶段递归去除所有 `ParenExpr` 包装。

### 其他表达式类型

| 类型 | 关键字段 |
|------|----------|
| `CaseExpr` | `subject`, `vector<pair<Expression,Expression>> when_thens`, `else_expr` |
| `ListComprehension` | `variable`, `list_expr`, `where_pred`, `projection` |
| `PatternComprehension` | `variable`, `patterns`, `where_pred`, `projection` |
| `AllExpr/AnyExpr/NoneExpr/SingleExpr` | `variable`, `list_expr`, `where_pred` |

---

## 五、执行状态

当前 `Statement`/`Clause` 中的常规查询、独立 `CALL`、`EXPLAIN`、`PROFILE` 都已进入
执行路径。MATCH、OPTIONAL MATCH、CREATE、MERGE、SET、REMOVE、DELETE、UNWIND、
WITH、RETURN、CALL、UNION/UNION ALL、FOREACH 均已实现。

表达式方面，Literal、Variable、Parameter、PropertyAccess、LabelCastExpr、
FunctionCall、BinaryOp（含 `%`/`^`/字符串谓词/LIST_CONCAT）、UnaryOp、
CaseExpr、List/Map 字面量、ListComprehension、PatternComprehension、
ExistsExpr、Subscript/Slice、量词（ALL/ANY/NONE/SINGLE）均已进入执行路径。

仍待处理的能力见 [cypher-syntax.md](cypher-syntax.md) 的“Neo4j 支持但 EuGraph
尚未支持”一节；其中主要包括明确不做的 `CALL { ... }` 子查询、`LOAD CSV`、
约束 DDL，以及复杂的 RETURN/CASE 完整 EXISTS 形式。
