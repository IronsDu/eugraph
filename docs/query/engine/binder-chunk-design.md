# Binder + Chunk 执行引擎设计

> **状态**：设计阶段，尚未实现
> **参见**：[README.md](../../README.md) 返回文档导航

---

## 概述

在 AST 和物理执行之间插入 **Binder（语义分析 + 类型绑定）** 阶段，并将执行器数据格式从行存（`Row`）升级为列存（`DataChunk` + `SelectionVector`），同时引入 **Catalog（目录系统）** 和 **Function Registry（函数注册表）**。

### 动机

| 现状问题 | 改进 |
|---------|------|
| `ExpressionEvaluator` 每次求值通过字符串查找列名/函数/属性 | Binder 将符号解析为列索引、属性 ID、函数指针，消除运行时字符串 dispatch |
| 所有属性全量获取（`getVertexProperties` 返回全部） | 投影下推：Scan 算子只请求下游需要的属性 ID |
| `Row` = `vector<Value>`，每行每列独立内存分配 | `DataChunk` = 列存数组，同类型值连续存储，SIMD 友好 |
| Filter 产生新 Row，内存搬运频繁 | `SelectionVector` 标记有效行，不物理重整数据 |
| 函数硬编码在 ExpressionEvaluator/LogicalPlanBuilder/AggregatePhysicalOp 三处 | Function Registry 统一注册，Binder 查找绑定 |
| 元数据在 PhysicalPlanner 阶段临时加载 | Catalog 统一管理所有数据库对象 |
| 多标签属性访问无类型信息 | 强类型（`::`）确定具体类型，弱类型推断为 ANY |
| 求值错误静默返回 NULL | 绑定期静态检查报错 + 运行期错误传播 |

---

## 一、新执行流水线

```
Cypher 文本
  → Parser: 字符串 → AST (Statement)                    [不变]
  → Binder: AST → BoundStatement                        [新增]
      ├── Catalog 查找符号（label、property、function）
      ├── 类型推断与检查
      ├── 表达式绑定（Variable→ColumnRef, PropertyAccess→PropertyRef, FunctionCall→FuncRef）
      ├── 投影下推标注（需要哪些属性列）
      └── 输出 BoundLogicalPlan（带完整类型模式）
  → Optimizer: BoundLogicalPlan → BoundLogicalPlan      [预留]
  → PhysicalPlanner: BoundLogicalPlan → PhysicalOp      [适配新接口]
  → Executor: AsyncGenerator<DataChunk>                 [RowBatch→DataChunk]
```

---

## 二、Catalog（目录系统）

**新目录**：`src/compute_service/catalog/`

Catalog 管理所有数据库对象的命名空间和元数据，为 Binder 提供查找服务。

```
Catalog
  ├── GraphCatalogEntry
  │     ├── labels: map<LabelName, LabelDef>
  │     └── edge_labels: map<EdgeLabelName, EdgeLabelDef>
  ├── FunctionCatalogEntry
  │     └── functions: map<FunctionName, vector<FunctionDef>>
  └── IndexCatalogEntry (依托 LabelDef::indexes)
```

```cpp
// src/compute_service/catalog/catalog.hpp
class Catalog {
public:
    // 从 AsyncGraphMetaStore 加载元数据
    folly::coro::Task<void> load(IAsyncGraphMetaStore& meta);

    // Label 查找
    const LabelDef* lookupLabel(const std::string& name) const;
    const LabelDef* lookupLabel(LabelId id) const;
    std::vector<const LabelDef*> lookupLabels(const std::vector<std::string>& names) const;

    // EdgeLabel 查找
    const EdgeLabelDef* lookupEdgeLabel(const std::string& name) const;
    const EdgeLabelDef* lookupEdgeLabel(EdgeLabelId id) const;

    // 属性查找（在给定标签内）
    const PropertyDef* lookupProperty(LabelId lid, const std::string& prop_name) const;
    const PropertyDef* lookupProperty(EdgeLabelId elid, const std::string& prop_name) const;

    // 函数查找
    const FunctionDef* lookupFunction(const std::string& name) const;

    // 所有标签/边标签
    const std::unordered_map<LabelId, LabelDef>& allLabels() const;
    const std::unordered_map<EdgeLabelId, EdgeLabelDef>& allEdgeLabels() const;

    // 索引查找
    const LabelDef::IndexDef* lookupIndex(LabelId lid, const std::string& index_name) const;

private:
    std::unordered_map<std::string, LabelId> label_name_to_id_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs_;
    // Function catalog 由 FunctionRegistry 管理
};
```

Catalog 在 QueryExecutor 启动时加载一次，绑定多个查询可复用。

---

## 三、类型系统

**新文件**：`src/compute_service/binder/bound_type.hpp`

```cpp
// BoundType: 绑定后的静态类型
enum class BoundTypeKind {
    BOOL,
    INT64,
    DOUBLE,
    STRING,
    VERTEX,
    EDGE,
    PATH,
    LIST,     // List<T>
    ANY,      // 多标签弱类型访问，运行时类型不确定
    NULL_TYPE, // 字面量 NULL 的类型
};

struct BoundType {
    BoundTypeKind kind;
    // 仅 LIST 使用
    std::unique_ptr<BoundType> element_type;

    static BoundType Bool();
    static BoundType Int64();
    static BoundType Double();
    static BoundType String();
    static BoundType Vertex();
    static BoundType Edge();
    static BoundType Path();
    static BoundType Any();
    static BoundType Null();
    static BoundType List(BoundType element);

    bool operator==(const BoundType& other) const;
    bool canCastTo(const BoundType& target) const;  // 隐式转换检查
};
```

### ANY 类型语义

- 弱类型属性访问 `n.name`：收集所有候选标签的 `name` 属性类型，若全部一致则推断为具体类型，否则为 `ANY`
- 强类型属性访问 `n::Person.name`：查找 `Person.name` 定义，类型确定
- `ANY` 列在 Column 中需要额外存储 per-value 类型标记（退化为 variant 存储）

---

## 四、Function Registry（函数注册表）

**新目录**：`src/compute_service/function/`

### 函数注册

```cpp
// src/compute_service/function/function_def.hpp
struct FunctionDef {
    std::string name;
    std::vector<BoundType> arg_types;   // 参数类型（可变参数时为空）
    BoundType return_type;
    bool is_aggregate = false;
    bool has_variadic_args = false;     // 如 coalesce(...)
    // 函数指针，由具体实现提供
    void* impl;  // 类型擦除的执行指针，具体签名取决于函数
};

// src/compute_service/function/function_registry.hpp
class FunctionRegistry {
public:
    // 注册内置函数
    void registerBuiltins();

    // 查找函数（支持重载：同名+参数类型精确匹配）
    const FunctionDef* lookup(const std::string& name,
                              const std::vector<BoundType>& arg_types) const;

    // 按名称查找所有重载
    std::vector<const FunctionDef*> lookupAll(const std::string& name) const;

    // 注册自定义函数
    void registerFunction(FunctionDef def);

private:
    // name -> vector<overload>
    std::unordered_map<std::string, std::vector<FunctionDef>> functions_;
};
```

### 内置函数注册表

| 函数 | 参数类型 | 返回类型 | 聚合 |
|------|---------|---------|------|
| `id(Vertex)` | Vertex | Int64 | 否 |
| `id(Edge)` | Edge | Int64 | 否 |
| `nodes(Path)` | Path | List\<Vertex\> | 否 |
| `relationships(Path)` | Path | List\<Edge\> | 否 |
| `length(Path)` | Path | Int64 | 否 |
| `count(*)` | — | Int64 | 是 |
| `count(Any)` | Any | Int64 | 是 |
| `sum(Int64)` | Int64 | Int64 | 是 |
| `sum(Double)` | Double | Double | 是 |
| `avg(Int64)` | Int64 | Double | 是 |
| `avg(Double)` | Double | Double | 是 |
| `min(Any)` | Any | Any | 是 |
| `max(Any)` | Any | Any | 是 |

### 函数实现组织

每种函数独立文件，实现为自由函数或函数对象：

```
src/compute_service/function/
  ├── function_def.hpp
  ├── function_registry.hpp / .cpp
  ├── scalar/
  │     ├── id_function.hpp
  │     ├── path_functions.hpp     # nodes, relationships, length
  │     └── ...
  └── aggregate/
        ├── count_function.hpp
        ├── sum_function.hpp
        ├── avg_function.hpp
        ├── min_function.hpp
        └── max_function.hpp
```

---

## 五、Bound Expression（绑定后表达式）

**不同于 AST Expression**：BoundExpression 将符号引用解析为具体索引/指针，每个节点带有确定的返回类型。

**新目录**：`src/compute_service/binder/bound_expression/`

每个表达式类型独立文件。

### 层次结构

```
BoundExpression = variant<
    BoundLiteral,           // 字面量 + 类型
    BoundColumnRef,         // 列引用（列索引 + 类型）
    BoundParameter,         // $param
    BoundPropertyRef,       // 属性访问（prop_id + 类型，或 ANY）
    BoundLabelCast,         // n::Label 转型
    BoundBinaryOp,          // 二元运算 + 操作函数指针
    BoundUnaryOp,           // 一元运算 + 操作函数指针
    BoundFunctionCall,      // 函数调用 + FunctionDef 指针
    BoundList,              // 列表字面量
    BoundMap,               // Map 字面量
    BoundCase,              // CASE WHEN
    BoundSubscript,         // list[index]
    BoundSlice,             // list[from..to]
    BoundExists,            // EXISTS { pattern }    [暂不实现]
    BoundListComprehension, // [x IN list | proj]   [暂不实现]
    BoundPatternComprehension, // [(a)-->(b) | proj] [暂不实现]
    BoundQuantifier         // ALL/ANY/NONE/SINGLE   [暂不实现]
>;
```

### 各表达式类型定义

#### BoundLiteral (`bound_literal.hpp`)

```cpp
struct BoundLiteral {
    Value value;        // 运行时的字面量值
    BoundType type;     // 字面量的具体类型
};
```

#### BoundColumnRef (`bound_column_ref.hpp`)

```cpp
struct BoundColumnRef {
    uint32_t column_index;   // 在 DataChunk 中的列索引
    BoundType type;          // 该列的类型（可能为 ANY）
    std::string name;        // 列名（调试用）
};
```

#### BoundParameter (`bound_parameter.hpp`)

```cpp
struct BoundParameter {
    std::string name;        // $param_name
    BoundType expected_type; // 参数期望的类型
};
```

#### BoundPropertyRef (`bound_property_ref.hpp`)

```cpp
struct BoundPropertyRef {
    // 对象表达式绑定的列
    std::unique_ptr<BoundExpression> object;
    // 强类型：确定的 prop_id + type
    // 弱类型：遍历多个标签的候选属性
    struct ResolvedProperty {
        LabelId label_id;
        uint16_t prop_id;
        BoundType type;
    };
    std::vector<ResolvedProperty> candidates;  // 多标签时可能有多个候选
    BoundType result_type;  // 全部一致则具体类型，否则 ANY
};
```

#### BoundLabelCast (`bound_label_cast.hpp`)

```cpp
struct BoundLabelCast {
    std::unique_ptr<BoundExpression> object;
    LabelId label_id;
    BoundType result_type;  // Vertex（只含该标签属性）
};
```

#### BoundBinaryOp (`bound_binary_op.hpp`)

```cpp
// 二元操作函数签名
using BinaryOpFunc = void (*)(const void* left_data, const void* right_data,
                               void* result_data, size_t count,
                               const uint8_t* left_valid, const uint8_t* right_valid,
                               uint8_t* result_valid);

struct BoundBinaryOp {
    BinaryOperator op;
    std::unique_ptr<BoundExpression> left;
    std::unique_ptr<BoundExpression> right;
    BoundType result_type;
    BinaryOpFunc func;  // 具体操作函数（绑定后确定）
};
```

#### BoundUnaryOp (`bound_unary_op.hpp`)

```cpp
using UnaryOpFunc = void (*)(const void* input_data, void* result_data,
                              size_t count, const uint8_t* input_valid,
                              uint8_t* result_valid);

struct BoundUnaryOp {
    UnaryOperator op;
    std::unique_ptr<BoundExpression> operand;
    BoundType result_type;
    UnaryOpFunc func;
};
```

#### BoundFunctionCall (`bound_function_call.hpp`)

```cpp
struct BoundFunctionCall {
    const FunctionDef* func_def;   // 指向注册表中的函数定义
    std::vector<std::unique_ptr<BoundExpression>> args;
    BoundType return_type;
    bool distinct = false;
};
```

#### BoundList (`bound_list.hpp`)

```cpp
struct BoundList {
    std::vector<std::unique_ptr<BoundExpression>> elements;
    BoundType element_type;  // 元素类型（如不一致则为 ANY）
};
```

#### BoundMap, BoundCase 等

```cpp
struct BoundMap {
    std::vector<std::pair<std::string, std::unique_ptr<BoundExpression>>> entries;
};

struct BoundCase {
    std::optional<std::unique_ptr<BoundExpression>> subject;
    std::vector<std::pair<std::unique_ptr<BoundExpression>, std::unique_ptr<BoundExpression>>> when_thens;
    std::optional<std::unique_ptr<BoundExpression>> else_expr;
    BoundType result_type;
};
```

### BoundExpression 辅助

```cpp
// src/compute_service/binder/bound_expression/bound_expression_fwd.hpp
// 前置声明所有类型

// src/compute_service/binder/bound_expression/bound_expression.hpp
// BoundExpression 的 variant 定义 + 访问器

// src/compute_service/binder/bound_expression/bound_expression_visitor.hpp
// 统一的访问辅助（代替频繁的 std::visit）
template <typename R, typename Visitor>
R visitBoundExpression(const BoundExpression& expr, Visitor&& visitor);
```

---

## 六、Binder（绑定器）

**新文件**：`src/compute_service/binder/binder.hpp` / `.cpp`

### 职责

1. 遍历 AST，在 Catalog 中查找符号
2. 将 AST Expression 转换为 BoundExpression
3. 推断每个表达式的返回类型
4. 执行类型检查，不兼容时报错
5. 维护符号表（变量名 → 列索引 + 类型）
6. 收集投影下推所需属性信息

### 绑定上下文

```cpp
struct BindContext {
    const Catalog& catalog;
    const FunctionRegistry& func_registry;
    // 当前可见的符号：name → {column_index, BoundType, source_label}
    std::unordered_map<std::string, ColumnInfo> symbols;
    // 投影下推收集：Scan 算子需要获取哪些属性
    struct PropertyRequirement {
        std::optional<LabelId> label_id;  // nullopt = 不确定（弱类型访问，需要所有标签）
        std::vector<uint16_t> prop_ids;
    };
    std::unordered_map<std::string, PropertyRequirement> property_requirements;
};
```

### 绑定过程

```
Binder::bind(Statement)
  → bindRegularQuery(RegularQuery)
    → for each SingleQuery:
      → for each Clause:
        → bindMatch(MatchClause)       // 绑定模式，注册变量
        → bindWhere(Expression)        // 绑定 WHERE 谓词，收集属性需求
        → bindReturn(ReturnClause)     // 绑定 RETURN 投影，收集属性需求
        → bindWith(WithClause)         // 绑定 WITH 子句
        → bindCreate(CreateClause)     // 绑定 CREATE
        → ...
  → 产出 BoundStatement（含 BoundLogicalPlan + 输出类型模式）
```

### 类型推断规则

```
e1 + e2:
  INT64 + INT64 → INT64
  DOUBLE + DOUBLE → DOUBLE
  INT64 + DOUBLE → DOUBLE（隐式转换）
  其他 → 绑定期报错 "type mismatch"

e1 = e2:
  任意同类型可比较 → BOOL
  支持 INT64 = DOUBLE（隐式转换后比较）
  不支持 VERTEX = INT64

n.name（弱类型）:
  候选类型集合 = {Person.name: STRING, Employee.name: STRING} → STRING
  候选类型集合 = {Person.name: STRING, Product.name: INT64} → ANY

n::Person.name（强类型）:
  查找 Person.name → STRING
  若 Person 没有 name → 绑定期报错 "property not found"

id(x):
  若 x 类型为 Vertex → 返回 INT64
  若 x 类型为 Edge → 返回 INT64
  其他 → 报错 "function id() requires Vertex or Edge argument"
```

### 错误处理

绑定期可检测的错误（不再等到执行时静默返回 NULL）：

- 变量未定义
- 函数不存在
- 函数参数类型不匹配
- 属性不存在（强类型访问 `::`）
- 二元操作类型不兼容
- Label/EdgeLabel 不存在
- 聚合函数嵌套

---

## 七、Bound Logical Plan

**新目录**：`src/compute_service/binder/bound_logical_plan/`

```cpp
struct ColumnInfo {
    std::string name;
    BoundType type;
    // 若此列来自属性访问，记录来源信息（供投影下推使用）
    std::optional<LabelId> source_label;
    std::optional<uint16_t> source_prop_id;
};

struct BoundLogicalOperator {
    LogicalOperatorType type;
    std::vector<ColumnInfo> output_schema;  // 该算子的输出模式
    // ... 算子特有数据
    std::vector<std::unique_ptr<BoundLogicalOperator>> children;
};

struct BoundLogicalPlan {
    std::unique_ptr<BoundLogicalOperator> root;
    std::vector<ColumnInfo> output_schema;  // 最终输出模式
};
```

每个逻辑算子携带完整的输出模式（列名 + 类型），物理计划阶段可直接使用。

---

## 八、DataChunk（列存数据块）

**新文件**：`src/compute_service/executor/data_chunk.hpp`

### Column（单列）

```cpp
enum class ColumnType {
    BOOL,
    INT64,
    DOUBLE,
    STRING,
    VERTEX,
    EDGE,
    PATH,
    LIST,
    ANY,  // 退化为 variant 存储
};

struct Column {
    ColumnType type;
    size_t capacity = 0;

    // 类型数据（根据 type 使用对应成员）
    // 具体类型列使用固定类型 vector + validity bitmap
    union {
        std::vector<bool>* bool_data;
        std::vector<int64_t>* int64_data;
        std::vector<double>* double_data;
        std::vector<std::string>* string_data;
        std::vector<VertexValue>* vertex_data;
        std::vector<EdgeValue>* edge_data;
        std::vector<PathValue>* path_data;
        std::vector<ListValue>* list_data;
    };
    // 对于 ANY 类型，存储 variant 向量
    std::vector<Value>* any_data = nullptr;

    // Validity bitmap: bit i = 0 表示第 i 行为 NULL
    std::vector<uint8_t> validity;
};
```

### DataChunk

```cpp
struct DataChunk {
    std::vector<Column> columns;
    size_t count = 0;   // 逻辑行数（≤ capacity）
    size_t capacity = 0;

    static constexpr size_t DEFAULT_CAPACITY = 1024;

    void reset();        // 清空所有列，count=0
    size_t allocate(size_t n);  // 分配 n 行空间
};
```

### SelectionVector

```cpp
struct SelectionVector {
    std::vector<uint32_t> indices;  // 逻辑行 → 物理行映射
    size_t count = 0;

    // 初始化：恒等映射（所有行有效）
    static SelectionVector identity(size_t n);

    // 根据 predicate 列（BOOL）过滤
    void filter(const uint8_t* validity, const std::vector<bool>& predicate);
};
```

### Chunk 操作

```cpp
// 从 Chunk 中提取单行的某个列值（用于最终输出转换）
Value getValue(const DataChunk& chunk, size_t col_idx, size_t row_idx,
               const SelectionVector* sel = nullptr);

// 将 Value 写入 Column 指定位置
void setValue(Column& col, size_t idx, const Value& val);
```

---

## 九、物理算子接口适配

### 旧接口

```cpp
virtual folly::coro::AsyncGenerator<RowBatch> execute() = 0;
```

### 新接口

```cpp
virtual folly::coro::AsyncGenerator<DataChunk> execute() = 0;

// 可选：算子声明输出模式（由 Binder 绑定的类型信息填充）
virtual const std::vector<ColumnInfo>& outputSchema() const;
```

### 各算子变更要点

| 算子 | 变更 |
|------|------|
| Scan (AllNode/Label/Index) | 根据 projection 只获取需要的属性 ID，以列存填充 DataChunk |
| Expand | 只获取需要的属性，边属性按需加载。批量获取邻居属性减少 N+1 |
| Filter | 基于 SelectionVector 过滤，不改造 Chunk。谓词求值用 BoundExpression |
| Project | 用 BoundExpression 求值，增量添加列到 Chunk |
| Sort | 先计算排序键（物化），按 sel 重排。使用对 Chunk 列的比较 |
| Aggregate | 基于 Chunk 列进行分组和聚合。聚合函数指针来自 FunctionRegistry |
| Distinct | 基于 SelectionVector 标记重复行，不物理删除 |

---

## 十、存储层接口变更

### 新增按需获取接口

```cpp
// IAsyncGraphDataStore 新增 / 变更：

// 按需获取顶点属性（projection 指定需要的 prop_ids）
folly::coro::Task<std::optional<Properties>>
getVertexProperties(VertexId vid, LabelId label_id,
                    const std::vector<uint16_t>& projection);

// 批量按需获取（减少 Expand 算子的 N+1 调用）
folly::coro::Task<std::vector<std::optional<Properties>>>
batchGetVertexProperties(const std::vector<VertexId>& vids, LabelId label_id,
                          const std::vector<uint16_t>& projection);

// 按需获取边属性
folly::coro::Task<std::optional<Properties>>
getEdgeProperties(EdgeLabelId label_id, EdgeId eid,
                  const std::vector<uint16_t>& projection);

// 扫描顶点时指定投影
folly::coro::AsyncGenerator<std::vector<VertexId>>
scanVerticesByLabel(LabelId label_id,
                    const std::vector<uint16_t>& projection);
```

---

## 十一、表达式求值器升级

旧的 `ExpressionEvaluator` 替换为基于 BoundExpression 的向量化求值。

```cpp
// src/compute_service/executor/vectorized_evaluator.hpp

class VectorizedEvaluator {
public:
    // 对 DataChunk 中的所有行求值，结果写入 result 列
    void evaluate(const BoundExpression& expr, const DataChunk& input,
                  Column& result, const SelectionVector* sel = nullptr);

    // 求值为 predicate（用于 Filter），结果写入 bool 向量
    void evaluatePredicate(const BoundExpression& expr, const DataChunk& input,
                           std::vector<bool>& result,
                           const SelectionVector* sel = nullptr);
};
```

求值过程：
1. `BoundColumnRef` → 直接从 `input.columns[column_index]` 复制/引用
2. `BoundPropertyRef` → 从 VertexValue/EdgeValue 列中提取指定属性
3. `BoundBinaryOp` → 调用 `func(data, data, result, count, ...)`
4. `BoundFunctionCall` → 调用 `func_def->impl`

---

## 十二、实现计划

### 阶段 1：基础设施

- [ ] `BoundType` 类型系统
- [ ] `Catalog` 类
- [ ] `FunctionRegistry` + 内置函数注册
- [ ] `FunctionDef` 和函数签名定义

### 阶段 2：Bound Expression

- [ ] 各 BoundExpression 类型定义（每种独立文件）
- [ ] `BoundExpression` variant + 访问器

### 阶段 3：Binder

- [ ] `BindContext`
- [ ] `Binder::bind(Statement)` 主入口
- [ ] 各子句绑定（MATCH, RETURN, WITH, WHERE, CREATE, SET, REMOVE）
- [ ] 类型推断与检查
- [ ] 投影下推属性收集
- [ ] 错误报告

### 阶段 4：Bound Logical Plan

- [ ] `BoundLogicalOperator` 类型（带 output_schema）
- [ ] `BoundLogicalPlan` 适配现有 `LogicalPlanBuilder`

### 阶段 5：DataChunk

- [ ] `Column` + `DataChunk` + `SelectionVector`
- [ ] Chunk 基本操作（get/set/reset/allocate）

### 阶段 6：向量化求值器

- [ ] `VectorizedEvaluator`
- [ ] 逐表达式类型的求值实现

### 阶段 7：物理算子升级

- [ ] Scan 算子适配 DataChunk + 投影下推
- [ ] Expand 算子适配（含边属性加载 + 批量接口）
- [ ] Filter 算子适配 SelectionVector
- [ ] Project/Aggregate/Sort 算子适配

### 阶段 8：存储层适配

- [ ] 按需获取属性接口
- [ ] 批量获取接口

### 阶段 9：集成与清理

- [ ] 替换 QueryExecutor 中的旧流程
- [ ] 移除旧的 `ExpressionEvaluator`
- [ ] 端到端测试
- [ ] 更新 [query-engine-design.md](query-engine-design.md)

---

## 十三、关键设计决策

1. **BoundExpression 独立于 AST**：AST 是语法树（名称是字符串），BoundExpression 是语义树（名称已解析），两者生命周期不同。AST 在解析后即可释放，BoundExpression 存续到执行结束。

2. **Catalog 按查询加载**：每次执行从 MetaStore 加载最新元数据（按需或全量），不跨查询缓存，保证 schema 变更后查询可见。

3. **ANY 类型只在绑定时无法确定时才使用**：强类型访问 `::` 始终产生具体类型。弱类型访问在候选类型一致时也尽量推断为具体类型。

4. **SelectionVector 不跨 Batch**：每个 DataChunk 有独立的 SelectionVector，Batch 之间不共享。这简化了内存管理，代价是跨 Batch 过滤需要额外处理（如 Limit/Skip 算子）。

5. **Chunk 在算子间零拷贝传递**：下游算子直接读取上游 Chunk 的 Column 数据，需要新增列时才在 Chunk 上 append。避免多余的拷贝。

6. **每个 BoundExpression 类型独立文件**：遵循物理设计原则，每个类型在独立的 `.hpp` 文件中，通过前置声明和 `unique_ptr` 避免循环依赖。
