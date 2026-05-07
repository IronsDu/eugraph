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
    → Binder: AST → BoundLogicalPlan (语义绑定，符号解析，类型推断)
        ├── Catalog: label/property 解析
        ├── FunctionRegistry: 函数签名匹配
        └── BindContext: 变量→列索引映射，属性需求收集
    → PhysicalPlanner:
        ├── planBound: BoundLogicalPlan → PhysicalOperator 树
        └── ID 解析，存储引用绑定
    → QueryExecutor: 协程管道执行 (Pull-based 火山模型，AsyncGenerator<DataChunk>)
```

---

## 一、Catalog（目录系统）

`src/compute_service/catalog/`

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

`src/compute_service/binder/bound_type.hpp`

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

`src/compute_service/function/`

### FunctionDef

每个注册函数包含元数据和执行回调：

```cpp
struct FunctionDef {
    string name;
    vector<BoundType> arg_types;
    BoundType return_type;
    bool is_aggregate;
    bool has_variadic_args;

    // 标量函数执行回调
    using ScalarFn = function<Value(const vector<Value>&)>;
    ScalarFn scalar_fn;

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

聚合状态类（`src/compute_service/function/aggregate/`）继承 `AggStateBase`，提供 `add()` 和 `finalize()` 方法。

---

## 四、Bound Expression（绑定后表达式）

`src/compute_service/binder/bound_expression/`

BoundExpression 将符号引用解析为具体索引/指针，每个节点带有确定的返回类型。与 AST Expression 的区别：AST 是语法树（名称为字符串），BoundExpression 是语义树（名称已解析为索引/指针）。

```
BoundExpression = variant<
    BoundLiteral,           // 字面量 + 类型
    BoundColumnRef,         // 列索引 + 类型（零拷贝引用 DataChunk 列）
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
>;
```

`BoundFunctionCall` 持有 `const FunctionDef*`，求值时直接调用 `func_def->scalar_fn`，无需运行时字符串分发。

---

## 五、Binder

`src/compute_service/binder/`

### Binder（查询管线入口）

`Binder` 类将 AST 直接绑定为 `BoundLogicalPlan`，是查询管线的核心阶段：

1. 接收 `cypher::Statement`（AST）
2. 递归绑定 MATCH/RETURN/WHERE/CREATE/SET/REMOVE 子句
3. 将 `cypher::Expression` → `binder::BoundExpression`
4. 通过 Catalog 解析 label/property → LabelId/PropId
5. 通过 FunctionRegistry 解析函数调用 → FunctionDef 指针
6. 推断表达式返回类型
7. 收集投影下推所需属性信息
8. 产出 `BoundStatement`（含 `BoundLogicalPlan` + `BindContext`）

`BoundLogicalPlan` 的 root 是 `BoundLogicalOperator`（variant），包含 15 种 Bound\*Op 类型，所有符号引用已解析为具体 ID 和列索引。

### PlanBinder（旧管线兼容）

PlanBinder 在旧管线的 `PhysicalPlanner` 内部工作，对 `LogicalPlanBuilder` 产出的 `LogicalPlan` 表达式进行绑定。当前查询管线已切换到 `Binder` 路径（`Binder` → `planBound`），PlanBinder 仅保留用于兼容。

---

## 六、DataChunk（列存数据块）

`src/compute_service/executor/data_chunk.hpp`

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

## 七、逻辑计划

### 算子类型（15 种）

`vector<LogicalOperator> children` 形成火山模型算子树。

| 算子 | 说明 | 子节点数 |
|------|------|---------|
| `AllNodeScanOp` | 全顶点扫描 | 0 |
| `LabelScanOp` | 按标签扫描顶点 | 0 |
| `ExpandOp` | 从顶点展开邻居（支持多类型过滤） | 1 |
| `FilterOp` | 过滤行 | 1 |
| `ProjectOp` | 投影/重映射列 | 1 |
| `AggregateOp` | 聚合 + 分组 | 1 |
| `SortOp` | 排序 | 1 |
| `SkipOp` | 跳过前 N 行 | 1 |
| `DistinctOp` | 去重 | 1 |
| `LimitOp` | 限制行数 | 1 |
| `CreateNodeOp` | 创建顶点 | 0-1 |
| `CreateEdgeOp` | 创建边 | 1 |
| `SetOp` | 设置属性/标签 | 1 |
| `RemoveOp` | 移除属性/标签 | 1 |
| `PathBuildOp` | 组装路径变量 | 1 |

**符号表**：`Binder` 通过 `BindContext` 维护变量→列索引映射，MATCH 中每个变量分配列索引，RETURN/WHERE 通过索引访问。

### Cypher 子句映射

| Cypher 子句 | 逻辑算子 |
|-------------|----------|
| `MATCH (n)` | `AllNodeScanOp` |
| `MATCH (n:Label)` | `LabelScanOp` |
| `MATCH (n:Label{prop: val})` | `LabelScanOp` + `FilterOp` |
| `MATCH (a)-[r:TYPE]->(b)` | `ExpandOp`（子节点为 a 的 scan） |
| `MATCH p = ...` | `PathBuildOp` |
| `WHERE pred` | `FilterOp` |
| `RETURN items` | `ProjectOp`（无聚合）/ `AggregateOp`（有聚合） |
| `RETURN ... ORDER BY` | `SortOp` |
| `RETURN ... SKIP N` | `SkipOp` |
| `RETURN ... LIMIT N` | `LimitOp` |
| `RETURN DISTINCT ...` | `DistinctOp` |
| `CREATE (n:Label)` | `CreateNodeOp` |
| `CREATE (a)-[r:TYPE]->(b)` | `CreateEdgeOp` |
| `SET n:Label` / `SET n.prop = val` | `SetOp` |
| `REMOVE n:Label` / `REMOVE n.prop` | `RemoveOp` |

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
| `FilterPhysicalOp` | `BoundExpression` 谓词, Schema | DICTIONARY 共享 child buffer |
| `ProjectPhysicalOp` | `BoundExpression` 投影项列表 | FLAT columns |
| `AggregatePhysicalOp` | `FunctionDef*` 聚合, `BoundExpression` 分组键 | FLAT output |
| `SortPhysicalOp` | `BoundExpression` 排序键 | FLAT output |
| `SkipPhysicalOp` | `int64_t skip` | DICTIONARY 调整 selection |
| `DistinctOp` | — | DICTIONARY hash set 去重 |
| `LimitPhysicalOp` | `int64_t limit` | DICTIONARY 截断 selection |
| `CreateNodePhysicalOp` | `IAsyncGraphDataStore&`, `VertexId` | drain child → 写入 → 单行 FLAT |
| `CreateEdgePhysicalOp` | `IAsyncGraphDataStore&`, `EdgeId` | drain child → 写入 → 单行 FLAT |
| `SetPhysicalOp` | `IAsyncGraphDataStore&`, items | child 原样 yield |
| `RemovePhysicalOp` | `IAsyncGraphDataStore&`, items | child 原样 yield |
| `PathBuildPhysicalOp` | 路径变量名, 元素列名列表 | 复制 child 列 + 新建 PATH FLAT 列 |

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

### PhysicalPlanner

`PlanContext` 携带 label/edge_label 映射和 ID 分配器。每个算子的 `planOperator` 返回 `output_schema` + `output_types`。

**索引扫描优化**：
- `Filter(LabelScan)` + 可索引谓词 → `IndexScanPhysicalOp`
- `Filter(Expand)` + 可索引谓词 → `EdgeIndexScanPhysicalOp`

---

## 九、表达式求值

`src/compute_service/executor/vectorized_evaluator.hpp`

`VectorizedEvaluator` 对 `BoundExpression` 求值，直接操作 DataChunk 列数据，无运行时字符串查找。

### 求值分发

| BoundExpression 类型 | 求值方式 |
|---------------------|---------|
| `BoundLiteral` | 写入 CONSTANT 列 |
| `BoundColumnRef` | 零拷贝引用 input.columns[column_index] |
| `BoundBinaryOp` | 递归求值左右操作数，逐行 switch 运算 |
| `BoundUnaryOp` | 递归求值操作数，逐行 switch 运算 |
| `BoundPropertyRef` | 从 VertexValue/EdgeValue 列提取指定 prop_id |
| `BoundFunctionCall` | 调用 `func_def->scalar_fn(args)` |
| `BoundLabelCast` | 递归求值内部表达式 |

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
5. beginTran + setTransaction
6. planBound: BoundLogicalPlan → PhysicalOperator（ID 解析，存储引用绑定）
7. 若 EXPLAIN: 收集物理算子 toString，rollback，返回
8. executeChunk: AsyncGenerator<DataChunk> 由调用方 drain
```

### 流式执行

`prepareStream()` 执行步骤 1-8 后，将 generator + 事务打包为 `shared_ptr<StreamContext>` 返回，由 handler 层流式消费并提交事务。

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

## 十二、当前实现状态

### 已完成
- Cypher Parser + AST（含 `n::Label` 转型语法 + `LabelCastExpr`）
- 逻辑计划构建（15 种算子）
- Catalog 目录系统 + FunctionRegistry 函数注册表（含执行回调）
- BoundExpression 13 种类型 + Binder 语义绑定（已接入管线）
- BoundType 类型系统（隐式转换代价、类型合并）
- DataChunk 列存数据块（FLAT/CONSTANT/DICTIONARY + SelectionVector）
- VectorizedEvaluator 表达式求值器
- 所有物理算子升级为 DataChunk + BoundExpression 执行
- 读算子：Scan / Expand / Filter / Project / Limit / Sort / Skip / Distinct / Aggregate
- 聚合：count/sum/avg/min/max + GROUP BY + DISTINCT（通过 FunctionDef 回调）
- 写算子：CreateNode / CreateEdge / SetOp / RemoveOp（含索引维护）
- 多标签查询：per-label 属性存储、弱类型属性访问、强类型转型
- 索引扫描优化（IndexScan、EdgeIndexScan）
- 流式执行（StreamContext + Thrift ServerStream）
- 索引 DDL（CREATE/DROP/SHOW INDEX，含边索引）

### 已知限制（待优化）

**逐行函数调用开销**：`VectorizedEvaluator::evalFunctionCall` 和 `evalBinaryOp`/`evalUnaryOp` 对每行数据分别调用一次函数。在高并发场景下，逐行函数调用的虚函数/`std::function` 开销可能成为瓶颈。DuckDB 的做法是为每种操作生成或使用类型特化的批量处理函数，一次处理整列数据（如 `int64_add(column_a, column_b, result, count)`）。这是后续性能优化的首要方向。

**BoundBinaryOp/BoundUnaryOp 无函数指针**：当前通过 `switch(op)` 枚举分发。设计目标是绑定时确定具体的操作函数指针（如 DuckDB 的 `BinaryOpFunc`），求值时直接调用，消除 switch 开销。

**完整 Binder 已接入管线**：查询管线已从 `LogicalPlanBuilder` → `PlanBinder` 路径切换到 `Binder` → `planBound` 路径。`Binder` 类直接从 AST 产出 `BoundLogicalPlan`，`PhysicalPlanner::planBound()` 将其转换为 `PhysicalOperator` 树。旧的 `LogicalPlanBuilder` + `PlanBinder` 路径仍保留但不再作为默认管线。

**索引扫描优化暂未接入新管线**：`planBound` 路径当前不含 `tryIndexScan`/`tryEdgeIndexScan` 优化（Filter+LabelScan 不会自动转换为 IndexScan）。查询功能不受影响，但无法利用索引加速。待后续优化器阶段接入。

**投影下推未实际生效**：`Binder` 收集了属性需求（`BindContext::property_requirements`），但 Scan 算子尚未利用这些信息减少属性加载量，仍然获取全部属性。

### 待实现
- DELETE, MERGE 执行
- 多 MATCH + JOIN
- 查询优化器（谓词下推、投影裁剪）
- WITH 子句
- UNWIND + 列表操作
- DDL 异步执行（DdlWorker 后台线程）
- 崩溃恢复（索引 DDL 状态恢复）
