# AST 节点参考

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

源码：`src/compute_service/parser/ast.hpp`

---

## 一、顶层结构

```
Statement = variant<unique_ptr<RegularQuery>, unique_ptr<StandaloneCall>>

RegularQuery
  ├── SingleQuery first
  └── vector<pair<UnionClause, SingleQuery>> unions

SingleQuery
  └── vector<Clause> clauses

Clause = variant<unique_ptr<MatchClause>, UnwindClause, CallClause, CreateClause,
                 MergeClause, DeleteClause, SetClause, RemoveClause, ReturnClause,
                 WithClause>
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

### 其他表达式类型

| 类型 | 关键字段 |
|------|----------|
| `CaseExpr` | `subject`, `vector<pair<Expression,Expression>> when_thens`, `else_expr` |
| `ListComprehension` | `variable`, `list_expr`, `where_pred`, `projection` |
| `PatternComprehension` | `variable`, `patterns`, `where_pred`, `projection` |
| `AllExpr/AnyExpr/NoneExpr/SingleExpr` | `variable`, `list_expr`, `where_pred` |

---

## 五、执行状态

详细执行状态见 [cypher-syntax.md](cypher-syntax.md)，此处简述：

**已执行**的子句：MATCH, CREATE, RETURN + ORDER BY/SKIP/LIMIT/DISTINCT + 聚合

**仅解析**的子句：DELETE, SET, REMOVE, MERGE, UNWIND, WITH, CALL, UNION

**已执行的表达式**：Literal, Variable, PropertyAccess, FunctionCall(`id`/聚合), BinaryOp(比较/算术/逻辑), UnaryOp(NOT/NEGATE/IS_NULL)

**仅解析的表达式**：% ^, STARTS_WITH/ENDS_WITH/CONTAINS/IN/XOR, CASE, ListComprehension, PatternComprehension, 量词, Exists, Subscript/Slice, $param
