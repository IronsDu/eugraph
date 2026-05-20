# 查询引擎设计

> 参见 [README.md](../../README.md) 返回文档导航

---

## 概述

查询引擎将 Cypher 文本翻译为可执行的物理算子树，通过协程异步执行。

设计目标：
1. **计算层不阻塞**：folly 协程驱动，IO 等待时让出 CPU
2. **IO 与计算分离**：存储引擎调用通过 `IoScheduler` 调度到 IO 线程池
3. **列存批量流水线**：`DataChunk`（默认 1024 行/块）减少协程切换
4. **编译期类型解析**：Binder 阶段完成符号解析和类型推断，消除运行时字符串查找

运行时执行细节见 [execution-model.md](execution-model.md)。

---

## 执行流水线

```
Cypher 查询文本
    → CypherQueryParser: 字符串 → AST (Statement)
    → Binder: AST → BoundStatement (语义绑定，符号解析，类型推断)
        ├── Catalog: label/property 解析
        ├── FunctionRegistry: 函数签名匹配
        └── BindContext: 变量→列索引映射，属性需求收集
          （此时表达式中的变量引用为 BoundVariableRef，使用变量名）
    → ColumnResolver: BoundVariableRef → BoundColumnRef (名称→列索引)
        └── 遍历 BoundLogicalPlan 和 BoundExpression，将变量名解析为列索引
    → LogicalOptimizer: Columbia 风格 Cascades RBO（规则化逻辑优化）
        ├── Memo: Group + GroupExpr 多表达式存储
        ├── TaskQueue: OptimizeGroup / OptimizeExpr / ApplyRule 任务驱动
        └── FilterPushdownRule: Filter 下推到 Expand / Sort / Skip / Limit / Distinct 下方
    → PhysicalPlanner::planBound: BoundLogicalPlan → PhysicalOperator 树
        ├── tryBoundIndexScan: Filter(LabelScan) + 可索引谓词 → IndexScanPhysicalOp
        ├── tryBoundEdgeIndexScan: Filter(Expand) + 可索引谓词 → EdgeIndexScanPhysicalOp
        └── ID 解析，存储引用绑定
    → QueryExecutor: 协程管道执行 (Pull-based 火山模型，AsyncGenerator<DataChunk>)
```

---

## 一、Catalog（目录系统）

`src/query/catalog/`

Catalog 管理所有数据库对象的命名空间和元数据，为 Binder 提供查找服务。在 `QueryExecutor::prepareStream` 中从 `IAsyncGraphMetaStore` 加载，每次查询加载最新元数据。

```cpp
class Catalog {
public:
    void load(map<LabelId, LabelDef>, map<EdgeLabelId, EdgeLabelDef>);
    const LabelDef* lookupLabel(const string& name) const;
    const LabelDef* lookupLabel(LabelId id) const;
    const EdgeLabelDef* lookupEdgeLabel(const string& name) const;
    const PropertyDef* lookupProperty(LabelId lid, const string& prop_name) const;
    // ...
};
```

---

## 二、类型系统

`src/query/planner/bound_type.hpp`

```cpp
enum class BoundTypeKind {
    BOOL, INT64, DOUBLE, STRING, VERTEX, EDGE, PATH, LIST, ANY, NULL_TYPE
};

struct BoundType {
    BoundTypeKind kind;
    std::unique_ptr<BoundType> element_type; // 仅 LIST 使用

    static BoundType Bool(), Int64(), Double(), String();
    static BoundType Vertex(), Edge(), Path(), Any(), Null();
    static BoundType List(BoundType element);

    int implicitCastCost(const BoundType& target) const; // -1=不可转换, 0=精确匹配
};
```

**ANY 类型语义**：弱类型属性访问 `n.name` 跨多标签收集候选类型，若全部一致则推断为具体类型，否则为 `ANY`。强类型访问 `n::Person.name` 直接查找确定类型。

---

## 三、Function Registry（函数注册表）

`src/query/function/`

### FunctionDef

每个注册函数包含元数据和执行回调：

```cpp
struct FunctionDef {
    string name;
    vector<BoundType> arg_types;
    BoundType return_type;
    bool is_aggregate;
    bool has_variadic_args;

    // 标量函数执行回调（逐值，兼容保留）
    using ScalarFn = function<Value(const vector<Value>&)>;
    ScalarFn scalar_fn;

    // 批量标量函数回调（逐列处理，一次处理 DataChunk 整列数据）
    using BatchScalarFn = function<void(const vector<const Column*>& args, Column& result, size_t count)>;
    BatchScalarFn batch_scalar_fn;

    // 聚合函数回调（状态工厂 + 累积 + 终结）
    using AggInitFn     = function<unique_ptr<AggStateBase>()>;
    using AggUpdateFn   = function<void(AggStateBase&, const Value&)>;
    using AggFinalizeFn = function<Value(const AggStateBase&)>;
    AggInitFn agg_init;
    AggUpdateFn agg_update;
    AggFinalizeFn agg_finalize;

    int matchCost(const vector<BoundType>& call_arg_types) const; // 重载解析
};
```

### FunctionRegistry

支持同名函数重载，`lookup()` 按参数类型进行代价最小匹配：

```cpp
class FunctionRegistry {
    void registerBuiltins();
    const FunctionDef* lookup(const string& name, const vector<BoundType>& arg_types) const;
    const vector<FunctionDef>* lookupAll(const string& name) const;
    void registerFunction(FunctionDef def);
};
```

### 内置函数注册表

| 函数 | 参数类型 | 返回类型 | 聚合 | 状态类 |
|------|---------|---------|------|--------|
| `id(Vertex)` | Vertex | Int64 | 否 | — |
| `id(Edge)` | Edge | Int64 | 否 | — |
| `nodes(Path)` | Path | List\<Vertex\> | 否 | — |
| `relationships(Path)` | Path | List\<Edge\> | 否 | — |
| `length(Path)` | Path | Int64 | 否 | — |
| `count(*)` | — | Int64 | 是 | `CountState` |
| `sum(Int64)` | Int64 | Int64 | 是 | `Int64SumState` |
| `sum(Double)` | Double | Double | 是 | `DoubleSumState` |
| `avg(Int64)` | Int64 | Double | 是 | `AvgState` |
| `avg(Double)` | Double | Double | 是 | `AvgState` |
| `min(Any)` | Any | Any | 是 | `MinState` |
| `max(Any)` | Any | Any | 是 | `MaxState` |

聚合状态类（`src/query/function/aggregate/`）继承 `AggStateBase`，提供 `add()` 和 `finalize()` 方法。

---

## 四、Bound Expression（绑定后表达式）

`src/query/planner/bound_expression/`

BoundExpression 将符号引用解析为具体索引/指针，每个节点带有确定的返回类型。与 AST Expression 的区别：AST 是语法树（名称为字符串），BoundExpression 是语义树（名称已解析为索引/指针）。

```
BoundExpression = variant<
    BoundLiteral,           // 字面量 + 类型
    BoundColumnRef,         // 列索引 + 类型（零拷贝引用 DataChunk 列）
    BoundVariableRef,       // 变量名 + 类型（Binder 产出，ColumnResolver 解析为 BoundColumnRef）
    BoundParameter,         // $param
    BoundPropertyRef,       // 属性访问（prop_id + 类型，支持多候选）
    BoundLabelCast,         // n::Label 转型
    BoundBinaryOp,          // 二元运算 + 结果类型
    BoundUnaryOp,           // 一元运算 + 结果类型
    BoundFunctionCall,      // FunctionDef 指针 + 类型化参数
    BoundList,              // 列表字面量
    BoundMap,               // Map 字面量
    BoundCase,              // CASE WHEN
    BoundSubscript,         // list[index]
    BoundSlice,             // list[from..to]
    BoundAllExpr,           // ALL(x IN list WHERE pred) — 全称量化
    BoundAnyExpr,           // ANY(x IN list WHERE pred) — 存在量化
    BoundNoneExpr,          // NONE(x IN list WHERE pred) — 全称否定量化
    BoundSingleExpr,        // SINGLE(x IN list WHERE pred) — 唯一存在量化
>;
```

`BoundFunctionCall` 持有 `const FunctionDef*`，求值时调用 `func_def->batch_scalar_fn(args, result, count)` 批量处理整列数据，无需运行时字符串分发。

---

## 五、Binder

`src/query/planner/`

### Binder（查询管线入口）

`Binder` 类将 AST 直接绑定为 `BoundLogicalPlan`，是查询管线的核心阶段：

1. 接收 `cypher::Statement`（AST）
2. 递归绑定 MATCH/RETURN/WHERE/CREATE/SET/REMOVE 子句
3. 将 `cypher::Expression` → `binder::BoundExpression`
4. 通过 Catalog 解析 label/property → LabelId/PropId
5. 通过 FunctionRegistry 解析函数调用 → FunctionDef 指针
6. 推断表达式返回类型
7. 收集投影下推所需属性信息
8. 为 `BoundBinaryOp`/`BoundUnaryOp` 解析类型特化的批量函数指针（`BinaryBatchFn`/`UnaryBatchFn`）
9. 产出 `BoundStatement`（含 `BoundLogicalPlan` + `BindContext`）

`BoundLogicalPlan` 的 root 是 `BoundLogicalOperator`（variant），包含 17 种 BoundXxxOp 类型，定义在 `planner/logical_plan/operator/` 下各自独立的头文件中。所有符号引用已解析为具体 ID，但变量引用此时仍为 `BoundVariableRef`（名称 + 类型），由 ColumnResolver 在 Binder 之后解析为 `BoundColumnRef`（列索引）。

### ColumnResolver（变量名 → 列索引）

`src/query/planner/column_resolver.hpp`

Binder 产出的 `BoundLogicalPlan` 中，表达式变量引用为 `BoundVariableRef`（通过变量名字符串标识）。ColumnResolver 遍历整个 BoundLogicalPlan 和所有 BoundExpression，将 `BoundVariableRef` 替换为 `BoundColumnRef`（列索引）。这是两阶段解析的第二阶段：

```
Binder: AST 变量名 → BoundVariableRef（名称，在 BoundLogicalPlan 中）
ColumnResolver: BoundVariableRef → BoundColumnRef（列索引，直接引用 DataChunk 列）
```

之所以分两阶段而非 Binder 阶段直接确定列索引，是因为 Binder 在递归绑定时尚未知道最终输出 schema。

---

## 六、DataChunk（列存数据块）

`src/query/executor/data_chunk.hpp`

### Column（单列）

支持三种物理形态：

| 形态 | 用途 | 内存 |
|------|------|------|
| `FLAT` | 标准数组 | 按类型连续存储（vector\<int64_t\> 等） |
| `CONSTANT` | 字面量广播 | 只存一个 Value，所有行共享 |
| `DICTIONARY` | 零拷贝过滤/切片 | 引用另一个 ColumnBuffer + SelectionVector |

```cpp
struct Column {
    VectorForm form;
    BoundTypeKind type;
    ColumnBuffer buffer;            // FLAT/CONSTANT 数据
    SelectionVector selection;       // DICTIONARY 选择向量
    shared_ptr<ColumnBuffer> parent; // DICTIONARY 引用的源 buffer
};
```

### DataChunk

```cpp
struct DataChunk {
    vector<Column> columns;
    size_t count = 0;        // 逻辑行数
    static constexpr size_t DEFAULT_CAPACITY = 1024;
};
```

### SelectionVector

标记有效行，不物理重整数据。`is_identity = true` 时表示恒等选择（所有行有效），无需分配数组。

```cpp
struct SelectionVector {
    vector<uint32_t> indices;
    size_t count = 0;
    bool is_identity = true;
};
```

---

## 七、Bound Logical Plan（绑定后逻辑计划）

`src/query/planner/logical_plan/operator/`

每个算子类型定义在独立的头文件中（满足物理设计原则：最小化依赖，每个文件单一职责）。

`BoundLogicalOperator` 是一个 variant，包含 16 种算子。"子节点"通过 variant 值嵌入（非 unique_ptr），形成递归的算子树。

### 算子类型（18 种）

| 算子 | 定义文件 | 说明 | 子节点 |
|------|---------|------|--------|
| `BoundScanOp` | `bound_scan_op.hpp` | 全顶点扫描 | — |
| `BoundLabelScanOp` | `bound_label_scan_op.hpp` | 按标签扫描顶点 | — |
| `BoundExpandOp` | `bound_expand_op.hpp` | 从顶点展开邻居（支持多类型过滤） | 嵌入 `BoundLogicalOperator child` |
| `BoundVarLenExpandOp` | `bound_varlen_expand_op.hpp` | 变长邻接展开（`[*min..max]`） | 嵌入 `BoundLogicalOperator child` |
| `BoundFilterOp` | `bound_filter_op.hpp` | 过滤行 | 嵌入 `BoundLogicalOperator child` |
| `BoundProjectOp` | `bound_project_op.hpp` | 投影/重映射列 | 嵌入 `BoundLogicalOperator child` |
| `BoundAggregateOp` | `bound_aggregate_op.hpp` | 聚合 + 分组 | 嵌入 `BoundLogicalOperator child` |
| `BoundSortOp` | `bound_sort_op.hpp` | 排序 | 嵌入 `BoundLogicalOperator child` |
| `BoundSkipOp` | `bound_skip_op.hpp` | 跳过前 N 行 | 嵌入 `BoundLogicalOperator child` |
| `BoundDistinctOp` | `bound_distinct_op.hpp` | 去重 | 嵌入 `BoundLogicalOperator child` |
| `BoundLimitOp` | `bound_limit_op.hpp` | 限制行数 | 嵌入 `BoundLogicalOperator child` |
| `BoundCreateNodeOp` | `bound_create_node_op.hpp` | 创建顶点 | — |
| `BoundCreateEdgeOp` | `bound_create_edge_op.hpp` | 创建边。`label_id` 和 `label_name` 均为 `optional`（边类型可能不存在，binder 不修改数据库，仅标记）。`pending_props` 记录尚未解析的属性名→表达式映射，留给物理算子执行时按名解析。 | — |
| `BoundSetOp` | `bound_set_op.hpp` | 设置属性/标签 | 嵌入 `BoundLogicalOperator child` |
| `BoundRemoveOp` | `bound_remove_op.hpp` | 移除属性/标签 | 嵌入 `BoundLogicalOperator child` |
| `BoundPathBuildOp` | `bound_path_build_op.hpp` | 组装路径变量 | 嵌入 `BoundLogicalOperator child` |
| `BoundUnwindOp` | `bound_unwind_op.hpp` | 列表展开为行（UNWIND） | 嵌入 `BoundLogicalOperator child` |
| `BoundBinaryJoinOp` | `bound_binary_join_op.hpp` | 二元 Join（Cross/Inner/Left） | 嵌入 `BoundLogicalOperator left` + `right` |

> **开发注意：新增 Variant 类型的检查清单**
>
> `BoundLogicalOperator` 是 `std::variant`，新增算子类型时，除 variant 定义外，以下位置必须同步更新：
>
> 1. `opt_rule.hpp` 的 `OptNodeType` 枚举 — 添加对应的节点类型
> 2. `opt_rule.cpp` 的 `nodeTypeFromVariantIndex()` 映射数组 — 添加 variant index → 枚举的映射
> 3. `column_resolver.cpp` 的 `resolveOperator()` — 添加 `std::visit` 分支（递归处理子节点）
> 4. 所有 `std::visit` 访问 `BoundLogicalOperator` 的位置（`binder.cpp` 的 `applyProjectionPushdown`、`memo.cpp`、`physical_planner.cpp` 的 `planBoundOperator`）
> 5. 本项目禁用 `-Wno-error`——漏更新导致 `unused-function` 或越界会直接编译失败或 ASAN 崩溃

**符号表**：`Binder` 通过 `BindContext` 维护变量→列索引映射，MATCH 中每个变量分配列索引（在 `BoundScanOp::column_index` / `BoundLabelScanOp::column_index` 等字段中）。表达式中的变量引用在 Binder 阶段为 `BoundVariableRef`（变量名），经 `ColumnResolver` 解析为 `BoundColumnRef`（列索引）。

### Cypher 子句映射

| Cypher 子句 | 逻辑算子 |
|-------------|----------|
| `MATCH (n)` | `AllNodeScanOp` |
| `MATCH (n:Label)` | `LabelScanOp` |
| `MATCH (n:Label{prop: val})` | `LabelScanOp` + `FilterOp` |
| `MATCH (a)-[r:TYPE]->(b)` | `ExpandOp`（子节点为 a 的 scan） |
| `MATCH (a)-[*2..3]->(b)` | `VarLenExpandOp`（子节点为 a 的 scan） |
| `MATCH p = ...` | `PathBuildOp` |
| `WHERE pred` | `FilterOp` |
| `RETURN items` | `ProjectOp`（无聚合）/ `AggregateOp`（有聚合） |
| `RETURN expr`（无 MATCH） | `SingletonOp` + `ProjectOp` |
| `RETURN ... ORDER BY` | `SortOp` |
| `RETURN ... SKIP N` | `SkipOp` |
| `RETURN ... LIMIT N` | `LimitOp` |
| `RETURN DISTINCT ...` | `DistinctOp` |
| `CREATE (n:Label)` | `CreateNodeOp` |
| `CREATE (a)-[r:TYPE]->(b)` | `CreateEdgeOp` |
| `SET n:Label` / `SET n.prop = val` | `SetOp` |
| `REMOVE n:Label` / `REMOVE n.prop` | `RemoveOp` |
| `UNWIND expr AS var` | `UnwindOp` |

---

## 八、物理计划

### 物理算子接口

每个物理算子提供双接口：

```cpp
class PhysicalOperator {
    virtual AsyncGenerator<RowBatch> execute();      // 旧接口，桥接到 executeChunk
    virtual AsyncGenerator<DataChunk> executeChunk(); // 列存接口
};
```

### 物理算子列表

| 物理算子 | 持有的额外信息 | DataChunk 特性 |
|----------|---------------|---------------|
| `AllNodeScanPhysicalOp` | `IAsyncGraphDataStore&`, label 定义 | FLAT columns |
| `LabelScanPhysicalOp` | `IAsyncGraphDataStore&`, `LabelId` | FLAT columns |
| `IndexScanPhysicalOp` | `IAsyncGraphDataStore&`, `LabelId`, `prop_id`, `ScanMode` | FLAT columns |
| `EdgeIndexScanPhysicalOp` | `IAsyncGraphDataStore&`, `EdgeLabelId`, `prop_ids` | DICTIONARY columns for src/dst/edge |
| `ExpandPhysicalOp` | `IAsyncGraphDataStore&`, `optional<vector<EdgeLabelId>>`, Schema | 输入 DICTIONARY + 新列 FLAT |
| `VarLenExpandPhysicalOp` | `IAsyncGraphDataStore&`, `optional<vector<EdgeLabelId>>`, `min_hops`, `max_hops`, Schema | DFS + 边唯一性，输入 DICTIONARY + 目标顶点 FLAT |
| `FilterPhysicalOp` | `BoundExpression` 谓词, Schema | DICTIONARY 共享 child buffer |
| `ProjectPhysicalOp` | `BoundExpression` 投影项列表 | FLAT columns |
| `AggregatePhysicalOp` | `FunctionDef*` 聚合, `BoundExpression` 分组键 | FLAT output |
| `SortPhysicalOp` | `BoundExpression` 排序键 | FLAT output |
| `SkipPhysicalOp` | `int64_t skip` | DICTIONARY 调整 selection |
| `DistinctOp` | — | DICTIONARY hash set 去重 |
| `LimitPhysicalOp` | `int64_t limit` | DICTIONARY 截断 selection |
| `CreateNodePhysicalOp` | `IAsyncGraphDataStore&`, `VertexId` | drain child → 写入 → 单行 FLAT |
| `CreateEdgePhysicalOp` | `IAsyncGraphDataStore&`, `EdgeId`, `pending_props` | drain child → 解析 `pending_props`（按属性名匹配 pid）→ 写入边 → 单行 FLAT |
| `CreateEdgeLabelPhysicalOp` | `IAsyncGraphMetaStore&`, `IAsyncGraphDataStore&`, 标签名, 属性名列表, 共享的 `name_to_id`/`defs` 映射 | 运行时创建边类型：写元数据 → 创建 WT 数据表 → 重载定义同步本地映射 → 透传子算子输出 |
| `AlterEdgeLabelPhysicalOp` | `IAsyncGraphMetaStore&`, 标签名, 新属性名列表, 共享的 `defs` 映射 | 运行时为已有边类型添加缺失的属性列 → 重载定义 → 透传子算子输出 |
| `SetPhysicalOp` | `IAsyncGraphDataStore&`, items | child 原样 yield |
| `RemovePhysicalOp` | `IAsyncGraphDataStore&`, items | child 原样 yield |
| `PathBuildPhysicalOp` | 路径变量名, 元素列名列表 | 复制 child 列 + 新建 PATH FLAT 列 |
| `CrossProductPhysicalOp` | 左/右 child PhysicalOperator, 左/右 Schema | 嵌套循环笛卡尔积：左输入 DICTIONARY + 右输入 DICTIONARY |

### 各算子执行逻辑

**Scan**：通过 `AsyncGenerator<vector<VertexId>>` 异步扫描，逐批加载 `VertexValue`（属性+标签）。

**IndexScan**：由物理优化阶段从 `Filter(LabelScan)` 推导产生，支持等值和范围扫描。

**EdgeIndexScan**：由 `Filter(Expand)` 推导产生（单类型时）。边索引 value 包含邻接信息，可直接产出 src/dst/edge 列。

**Expand**：对输入的每一行，调用 `scanEdges(src_id, dir, label_filter)`，每条边产生一行输出。输入列以 DICTIONARY 形式共享给子算子。

**Filter**：`VectorizedEvaluator` 求值 BoundExpression 谓词，DICTIONARY 选择向量标记有效行。

**Project**：`VectorizedEvaluator` 求值 BoundExpression 投影项，写入新 FLAT 列。

**Aggregate**：阻断算子——按 group key 哈希分组，通过 `FunctionDef` 的 `agg_init/agg_update/agg_finalize` 回调执行聚合。支持 `count`/`sum`/`avg`/`min`/`max` + `DISTINCT`。全局聚合无输入时输出一行（count=0）。

**Sort**：阻断算子——全量物化子算子输出到内存，`VectorizedEvaluator` 预计算排序键，`std::sort` 排序后分批 yield FLAT DataChunk。

**Distinct**：`unordered_set<Row, RowHash, RowEqual>` 去重，DICTIONARY 选择向量标记首次出现的行。

**Skip/Limit**：DICTIONARY selection 调整，不物理拷贝数据。

**CreateNode/CreateEdge**：通过 `co_await store_.insertVertex/insertEdge` 异步写入。写入前检查唯一约束，写入后维护索引条目。

**CreateEdgeLabel / AlterEdgeLabel（运行时 DDL）**：当 binder 检测到 CREATE 语句引用了不存在的边类型或属性时，不会在绑定阶段修改数据库（binder 只检测并标记），而是在物理计划中插入 `CreateEdgeLabelPhysicalOp` 或 `AlterEdgeLabelPhysicalOp`。这两个算子先完成元数据变更和 WT 数据表创建，再执行子算子的实际边写入操作。`CreateEdgePhysicalOp` 执行时通过 `pending_props` 按属性名解析 pid（因为 pid 在 DDL 算子执行后才分配）。

### PhysicalPlanner

`src/query/physical_plan/physical_planner.hpp`

唯一入口为 `planBound(BoundLogicalPlan&, IAsyncGraphDataStore&, IAsyncGraphMetaStore&, PlanContext&)`。`PlanContext` 携带 label/edge_label 映射和 ID 分配器。每个算子的 `planBoundOperator()` 返回 `output_schema` + `output_types`。

**运行时 DDL 自动插入**：处理 `BoundCreateEdgeOp` 时，planBound 根据 `label_name` 和 `pending_props` 字段决定是否插入额外的物理算子：
- `label_name` 有值（边类型不存在）→ 插入 `CreateEdgeLabelPhysicalOp`（携带属性名列表），一次性创建边类型及其属性
- `label_id` 有值但 `pending_props` 非空（边类型存在但缺少属性）→ 插入 `AlterEdgeLabelPhysicalOp`，为已有边类型添加属性列
- 插入的 DDL 算子先于 `CreateEdgePhysicalOp` 执行，确保边写入时 schema 已就绪

**索引扫描优化**（已接入 planBound 管线）：
- `Filter(LabelScan)` + 可索引谓词 → `tryBoundIndexScan()` → `IndexScanPhysicalOp`
- `Filter(Expand)` + 可索引谓词 → `tryBoundEdgeIndexScan()` → `EdgeIndexScanPhysicalOp`

可索引谓词提取通过 `collectBoundConditions()` / `tryExtractBoundCondition()`，支持等值和范围条件。

**Unwind**：对输入的每个 DataChunk，通过 `VectorizedEvaluator` 求值列表表达式得到每行的列表值。遍历每个列表元素，复制输入列并设置 UNWIND 变量列，产出展开后的行。空列表和 NULL 跳过该行。累积到 `DEFAULT_CAPACITY`(1024) 行后 yield 一个 DataChunk。

---

## 九、表达式求值

`src/query/executor/vectorized_evaluator.hpp`

`VectorizedEvaluator` 对 `BoundExpression` 求值，直接操作 DataChunk 列数据，无运行时字符串查找。

### 求值分发

| BoundExpression 类型 | 求值方式 |
|---------------------|---------|
| `BoundLiteral` | 写入 CONSTANT 列 |
| `BoundColumnRef` | 零拷贝引用 `input.columns[column_index]` |
| `BoundBinaryOp` | 递归求值左右操作数，通过 `BinaryBatchFn` 类型特化函数指针批量处理整列 |
| `BoundUnaryOp` | 递归求值操作数，通过 `UnaryBatchFn` 类型特化函数指针批量处理整列 |
| `BoundPropertyRef` | 从 VertexValue/EdgeValue 列提取指定 prop_id |
| `BoundFunctionCall` | 调用 `func_def->batch_scalar_fn(args, result, count)` 批量处理 |
| `BoundLabelCast` | 递归求值内部表达式 |
| `BoundQuantifierExpr` | `evalQuantifierExpr()`：为 list 中每个元素构建临时单行 DataChunk 求值 where_pred |

### 聚合函数执行

`AggregatePhysicalOp` 通过 `FunctionDef` 聚合回调执行：

1. `agg_init()` → 为每个分组创建独立状态对象（继承 `AggStateBase`）
2. 每行 `agg_update(state, value)` → 累积
3. `agg_finalize(state)` → 输出最终结果

---

## 十、QueryExecutor

### 编排流程

```
1. IndexDdlParser::tryParse() → 若为索引 DDL，handleIndexDdl，short-circuit
2. parse: 字符串 → AST（EXPLAIN 已内置于语法规则）
3. 加载元数据: listLabels() + listEdgeLabels() → 构建 Catalog + FunctionRegistry
4. bind: AST → BoundLogicalPlan（Binder 语义绑定，符号解析，类型推断）
5. optimize: LogicalOptimizer 优化 BoundLogicalPlan（Filter 下推等规则）
6. beginTran + setTransaction
7. planBound: BoundLogicalPlan → PhysicalOperator（ID 解析，存储引用绑定）
8. 若 EXPLAIN: 收集物理算子 toString，rollback，返回
9. executeChunk: AsyncGenerator<DataChunk> 由调用方 drain
```

### 流式执行

`prepareStream()` 执行步骤 1-9 后，将 generator + 事务打包为 `shared_ptr<StreamContext>` 返回，由 handler 层流式消费并提交事务。

详细见 [execution-model.md](execution-model.md) 和 [transaction-model.md](transaction-model.md)。

---

## 十一、运行时类型

### Value

```cpp
using Value = variant<monostate, bool, int64_t, double, string,
                      VertexValue, EdgeValue, PathValue, ListValue>;
```

### DataChunk / Column（见第六节）

### Row / Schema（兼容层）

```cpp
using Row = vector<Value>;       // 位置式，列索引对应 Schema
using Schema = vector<string>;   // 列名列表
```

`RowBatch` 和 `Row` 保留作为兼容层，用于 DDL/EXPLAIN 结果输出和最终结果转换。

---

## 十二、已知限制与后续规划

### 已知限制

- **投影下推仅支持点查**：存储层通过逐属性 `getVertexProperty` 点查实现 projection，未利用 prefix scan 批量获取
- **Expand N+1 模式**：Expand 算子逐顶点调用 `getVertexLabels` + `getVertexProperties`，未批量预取
- **WITH 后 MATCH 独立变量（已支持）**：`MATCH ... WITH ... MATCH (newVar) RETURN ...` 通过 `BoundBinaryJoinOp`（JoinType::Cross）+ `CrossProductPhysicalOp` 实现笛卡尔积语义。右孩子可以是任意算子链（Scan → Filter → Expand）。
- **WITH 后 MATCH 混合模式（部分支持）**：`MATCH (x:X), (a)-->(b)` 中同时包含独立扫描和关联扩展的模式暂不支持。
- **WITH 作为首个子句（已支持）**：`WITH 1 AS x RETURN x` 通过 `BoundSingletonOp` 提供单行空数据源。

### 待实现

- DELETE, MERGE 执行
- Inner/Left Join（`BoundBinaryJoinOp` 已预留 `JoinType::Inner/Left`）
- `range()` 函数注册（配合 UNWIND 使用）
- `collect()` 函数注册（聚合函数）
- 列表拼接 `+` 的 LIST_CONCAT 绑定
- `RETURN *` 变量展开
- DDL 异步执行（DdlWorker 后台线程）
- 崩溃恢复（索引 DDL 状态恢复）

### 待优化：边属性邻接存储

**设想**：扩展 `CREATE EDGE LABEL` 语法，允许指定哪些属性列随邻接关系存储在 KV 的 value 中。

**动机**：当前边属性存储在独立的边属性表中。变长查询的逐跳属性过滤（如 `[:REL*1..5 {status: 'active'}]`）每跳都需要额外 IO 回查属性。如果高频过滤属性直接存储在邻接 KV 的 value 中，可以消除随机 IO。

**现状**：变长查询暂时使用额外 IO 回查方式完成属性过滤。此优化需存储层配合，优先级低于变长查询核心功能。

---

## 变长邻接查询（VarLen Expand）

`BoundVarLenExpandOp`（逻辑算子）+ `VarLenExpandPhysicalOp`（物理算子）实现 `(a)-[r:TYPE*min..max]->(b)` 变长邻接模式。

### 逻辑算子：BoundVarLenExpandOp

定义于 `src/query/planner/logical_plan/operator/bound_varlen_expand_op.hpp`。

| 字段 | 类型 | 说明 |
|------|------|------|
| `src_variable` / `src_column_index` | string / uint32 | 起点变量名及列索引 |
| `dst_variable` / `dst_column_index` | string / uint32 | 终点变量名及列索引 |
| `edge_label_ids` | `vector<EdgeLabelId>` | 边类型过滤；空 = 全部类型 |
| `direction` | `RelationshipDirection` | OUT / IN / BOTH |
| `min_hops` / `max_hops` | int64 | `max=-1` 表示无上界 |
| `dst_label_prop_ids` | `unordered_map<LabelId, vector<uint16>>` | 目标顶点投影下推 |
| `path_variable` / `path_column_index` | string / uint32 | 命名路径变量（可选） |
| `path_handled_by_varlen` | bool | 为 true 时跳过 PathBuildOp 创建 |
| `edge_variable` / `edge_column_index` | string / uint32 | 命名边变量 → `LIST<EDGE>`（可选） |
| `edge_prop_filters` | `unordered_map<EdgeLabelId, vector<pair<uint16, PropertyValue>>>` | 逐跳边属性过滤 |
| `child` | `BoundLogicalOperator` | 子算子（通常为 Scan） |

### 物理算子：VarLenExpandPhysicalOp

定义于 `src/query/physical_plan/operator/varlen_expand_physical_op.hpp`。

**遍历算法**：DFS + 显式栈。`StackFrame` 记录当前顶点、深度、预取邻边列表、边索引、入边信息。每步：

1. 检查边是否重复（`visited_edges` per-source-row，含 src_id/edge_label_id/dst_id/seq）——保证**路径内边唯一性**
2. 检查边属性过滤条件——不满足则 `continue`
3. 若 `next_depth >= min_hops`，emit 一条结果（含路径/边列表）
4. 若未达深度上限（或 unbounded），扫描邻边、顶点循环检测（线性扫描 stack）、入栈

**输出列顺序**：`[child cols...] [dst_vertex] [path?] [edge_list?]`

**Identity path**（`min_hops=0`）：DFS 前额外 emit 一行：
- `dst` = `src`（相同顶点），`path` = PathValue 仅含起点，`edge_list` = 空 ListValue
- `max_hops=0` 时跳过边扫描和 DFS

**Undirected**：通过 `MergedEdgeScanCursor` 合并 OUT + IN 两个方向的扫描结果。

**边属性过滤**：DFS 中逐跳 `co_await getEdgeProperties()` 检查 `{prop: value}` 条件，不满足则剪枝。

**Filter 下推控制**：`FilterPushdownRule` 禁止 Filter 通过 VarLenExpand 下推——filter 可能引用算子产出的 path / dst / edge 列。

**顶点循环检测**：对 unbounded（`max_hops < 0`）或未达 max 深度的遍历，线性扫描 stack 检查候选邻点是否已在当前路径中。

### 路径谓词

`BoundAllExpr` / `BoundAnyExpr` / `BoundNoneExpr` / `BoundSingleExpr` 四种 BoundExpression（`bound_quantifier_expr.hpp`），支持 `ALL/ANY/NONE/SINGLE(x IN list WHERE pred)` 语法。

关键实现决策：

- **循环变量作用域在 Binder 处理**：绑定 `where_pred` 前将循环变量临时注册到 `BindContext`（分配真实 column_index），绑定后移除。`where_pred` 中的循环变量自然解析为 `BoundColumnRef`，无需修改 ColumnResolver 的核心逻辑。
- **运行时求值**：`VectorizedEvaluator::evalQuantifierExpr()` 为 list 中每个元素构建临时单行 DataChunk（input columns + loop var column），走正常 `evaluateInternal` 路径求值 `where_pred`。不修改 `BoundVariableRef`/`BoundColumnRef` 的现有求值分支。
- **量词语义**：遵循 Cypher 标准——ALL 空列表→true，ANY 空列表→false，NONE 空列表→true，SINGLE→恰好一个元素满足。
- `BoundList` 求值同步修复（此前落入 evaluator 的 catch-all else，返回空列）。

### 已知限制（VarLenExpand 特有）

- **LIMIT 不参与 DFS 剪枝**：DFS 先穷举当前 source vertex 的所有路径才输出，LIMIT 在后续 pipeline 截断。超级节点（100 条边）上 `[*2]` 会产生 ~10k 中间结果，此时 LIMIT 无法提前终止遍历。
- **混合固定+varlen 链 + 命名路径**：`p = (a)-[:X]->(b)-[:Y*2..3]->(c)` 不支持（Binder 阶段拒绝）。
- **边属性过滤仅支持字面常量**：`{prop: value}` 中 value 必须为字面量（int/string/bool/double），不支持表达式或变量引用。
- **中间顶点缺少属性**：DFS 构建 PathValue 时，除首尾顶点外只设置 `id`，无 labels 和 properties。因此 `ALL(x IN nodes(p) WHERE x.name = 'Alice')` 对中间顶点不生效。
- **多 MATCH + VarLenExpand 未实现**：当前不支持多条 MATCH 子句中包含变长邻接的组合（属于多 MATCH + JOIN 的通用待实现项）。
