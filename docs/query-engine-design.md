# 查询引擎设计

## 概述

查询引擎负责将 Cypher 查询文本翻译为可执行的计划，并通过协程管道异步执行。核心设计目标：

1. **计算层不阻塞**：使用 folly 协程（C++20 coroutine）驱动查询执行，重查询在等待 IO 时主动让出 CPU，短查询不会被长时间阻塞。
2. **IO 与计算分离**：所有存储引擎调用通过 `IoScheduler` 调度到 folly IO 线程池，计算层代码不直接阻塞式调用存储引擎。
3. **批量流水线**：使用 `AsyncGenerator<RowBatch>` 替代逐行 `AsyncGenerator<Row>`，以 1024 行为一批，减少协程切换开销。

## 执行流水线

```
Cypher 查询文本
    │
    ▼
┌──────────────────┐
│  Cypher Parser   │  ANTLR4 → Parse Tree → AST (Statement)
│  (M1, 已完成)     │  文档: docs/cypher-parser-design.md
└────────┬─────────┘
         │ Statement
         ▼
┌──────────────────┐
│ LogicalPlanBuilder│  AST → LogicalPlan (算子树，字符串名称)
│  (M2)            │
└────────┬─────────┘
         │ LogicalPlan
         ▼
┌──────────────────┐
│ PhysicalPlanner   │  LogicalPlan → PhysicalOperator 树 (ID 解析、存储引用绑定)
│  (M3)            │
└────────┬─────────┘
         │ unique_ptr<PhysicalOperator>
         ▼
┌──────────────────┐
│ QueryExecutor     │  协程管道执行，IO 调度，事务管理
│  (M3)            │
└──────────────────┘
```

---

## 一、协程执行模型

### 1.1 线程模型

```
┌───────────────────────────────────────────────┐
│                QueryExecutor                   │
│                                               │
│  ┌─────────────────┐  ┌──────────────────┐    │
│  │ Compute 线程池   │  │  IO 线程池        │    │
│  │ (CPUThreadPool)  │  │ (IOThreadPool)    │    │
│  │                  │  │                   │    │
│  │ • 逻辑运算       │  │ • WiredTiger 调用 │    │
│  │ • 表达式求值     │  │ • Cursor 操作     │    │
│  │ • 过滤/投影/排序 │  │ • 事务提交/回滚   │    │
│  │ • 协程调度       │  │                   │    │
│  └────────┬────────┘  └────────┬──────────┘    │
│           │   co_await          │               │
│           │◄────────────────────┘               │
│           │   结果返回                           │
└───────────────────────────────────────────────┘
```

- **Compute 线程池**（`folly::CPUThreadPoolExecutor`）：执行查询的逻辑运算。当查询需要访问存储引擎时，通过 `co_await` 将 IO 请求调度到 IO 线程池，协程挂起，CPU 线程可以执行其他查询。
- **IO 线程池**（`folly::IOThreadPoolExecutor`）：执行所有存储引擎调用。完成后恢复挂起的协程。

### 1.2 为什么使用协程

**问题**：传统同步模型中，查询执行全程占用一个线程。重查询（如全表扫描 + 多跳遍历）会长时间占用线程，导致短查询排队等待。

**解决方案**：

| 场景 | 同步模型 | 协程模型 |
|------|----------|----------|
| 查询 A 等待存储 IO | 线程阻塞，浪费 CPU | 协程挂起，CPU 线程执行查询 B |
| 全表扫描 + 过滤 | 线程被独占 | 每次 IO 请求后让出，其他查询可穿插执行 |
| 多个并发查询 | 需要 N 个线程 | 少量线程即可服务大量并发 |

### 1.3 IoScheduler

```cpp
class IoScheduler {
    std::shared_ptr<folly::IOThreadPoolExecutor> io_pool_;
    folly::Executor::KeepAlive<> io_ka_;

    template <typename F>
    folly::coro::Task<R> dispatch(F func);

    template <typename F>
    folly::coro::Task<void> dispatchVoid(F func);
};
```

- `dispatch(func)`：将 `func` 调度到 IO 线程池，协程挂起直到结果就绪。
- 内部使用 `folly::coro::co_viaIfAsync` 切换到 IO executor 执行，完成后恢复调用者。
- 所有对 `ISyncGraphDataStore` 的调用都通过此机制，确保计算层不直接阻塞。

### 1.4 AsyncGraphDataStore

```cpp
class AsyncGraphDataStore : public IAsyncGraphDataStore {
    ISyncGraphDataStore* store_;
    IoScheduler* io_;
    GraphTxnHandle txn_;
};
```

`ISyncGraphDataStore` 的异步包装，实现 `IAsyncGraphDataStore` 接口。每个方法将同步存储调用通过 `IoScheduler::dispatch` 转为异步协程。同时提供事务方法（`beginTran/commitTran/rollbackTran`）和 DDL 方法（`createLabel/createEdgeLabel`）：

```cpp
folly::coro::Task<bool> insertVertex(VertexId vid,
    std::span<const std::pair<LabelId, Properties>> label_props,
    const PropertyValue* pk_value) {
    auto result = co_await io_->dispatch(
        [this, vid, label_props, pk_value]() -> bool {
            return store_->insertVertex(txn_, vid, label_props, pk_value);
        });
    co_return result;
}
```

支持的异步操作：

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `scanVerticesByLabel` | `AsyncGenerator<vector<VertexId>>` | 按标签扫描顶点，批量返回 |
| `scanEdges` | `AsyncGenerator<vector<EdgeIndexEntry>>` | 扫描某顶点的边 |
| `scanEdgesByType` | `AsyncGenerator<vector<EdgeTypeIndexEntry>>` | 按关系类型扫描边 |
| `insertVertex` | `Task<bool>` | 插入顶点 |
| `insertEdge` | `Task<bool>` | 插入边 |
| `beginTran` | `Task<GraphTxnHandle>` | 开始事务 |
| `commitTran` | `Task<bool>` | 提交事务 |
| `rollbackTran` | `Task<bool>` | 回滚事务 |
| `createLabel` | `Task<bool>` | 创建标签物理表 |
| `createEdgeLabel` | `Task<bool>` | 创建关系类型物理表 |
| `getVertexProperties` | `Task<optional<Properties>>` | 获取顶点属性 |
| `getVertexLabels` | `Task<LabelIdSet>` | 获取顶点标签集合 |

---

## 二、逻辑计划（M2）

### 2.1 算子类型

使用 `std::variant<unique_ptr<XxxOp>>` 模式（与 AST 一致），12 种算子：

| 算子 | 说明 | 子节点 |
|------|------|--------|
| `AllNodeScanOp` | 全顶点扫描 | 0 |
| `LabelScanOp` | 按标签扫描顶点 | 0 |
| `ExpandOp` | 从顶点展开邻居 | 1（输入） |
| `FilterOp` | 过滤行 | 1（输入） |
| `ProjectOp` | 投影/重映射列 | 1（输入） |
| `AggregateOp` | 聚合（count/sum/avg/min/max）+ 分组 | 1（输入） |
| `SortOp` | 排序 | 1（输入） |
| `SkipOp` | 跳过前 N 行 | 1（输入） |
| `DistinctOp` | 去重 | 1（输入） |
| `LimitOp` | 限制行数 | 1（输入） |
| `CreateNodeOp` | 创建顶点 | 0-1（链式创建时） |
| `CreateEdgeOp` | 创建边 | 1（输入） |

每个算子持有 `vector<LogicalOperator> children`，形成火山模型（Volcano）的算子树。

### 2.2 关键设计决策

**逻辑计划保持字符串名称**：算子中的 label 名称、关系类型名称保持为字符串，name → ID 的解析推迟到物理计划阶段。原因：
- 逻辑计划关注语义正确性，不依赖具体的 ID 映射。
- 便于后续优化器在逻辑层面做规则变换（如谓词下推、投影裁剪），无需关心存储细节。

**符号表**：`LogicalPlanBuilder` 内部维护 `SymbolTable`（变量名 → 列索引），跟踪当前行模式（schema）。MATCH 中的每个变量绑定分配一个列索引，RETURN 和 WHERE 使用该索引访问列。

### 2.3 翻译规则

```
MATCH (n:Person) WHERE n.age > 20 RETURN n.name, n.age LIMIT 10
```

翻译为：

```
Limit(10)
  └─ Project([n.name, n.age])
       └─ Filter(n.age > 20)
            └─ LabelScan(n, Person)
```

| Cypher 子句 | 逻辑算子 | 说明 |
|-------------|----------|------|
| `MATCH (n)` | `AllNodeScanOp` | 无标签过滤 |
| `MATCH (n:Label)` | `LabelScanOp` | 按标签扫描 |
| `MATCH (a)-[r:TYPE]->(b)` | `ExpandOp`（子节点为 a 的 scan） | 展开邻居 |
| `WHERE pred` | `FilterOp` | 过滤 |
| `RETURN items` | `ProjectOp` | 投影（无聚合函数时） |
| `RETURN agg(...)` | `AggregateOp` | 聚合（含 count/sum/avg/min/max 时） |
| `RETURN ... ORDER BY` | `SortOp` | 排序 |
| `RETURN ... SKIP N` | `SkipOp` | 跳过前 N 行 |
| `RETURN ... LIMIT N` | `LimitOp` | 限制行数 |
| `RETURN DISTINCT ...` | `DistinctOp` | 去重 |
| `CREATE (n:Label)` | `CreateNodeOp` | 创建顶点 |
| `CREATE (a)-[r:TYPE]->(b)` | `CreateEdgeOp` | 创建边 |

### 2.4 文件结构

```
src/compute_service/logical_plan/
  logical_operator.hpp       -- 8 种算子定义
  logical_plan.hpp           -- LogicalPlan 容器 + toString
  logical_plan_visitor.hpp   -- Visitor 接口（优化器扩展点）
  logical_plan_builder.hpp   -- Builder 类声明
  logical_plan_builder.cpp   -- AST → LogicalPlan 翻译实现
tests/test_logical_plan.cpp  -- 逻辑计划测试
```

---

## 三、物理计划（M3）

### 3.1 物理算子

物理算子与逻辑算子一一对应，但增加了执行所需的上下文（另有 `IndexScanPhysicalOp` 由物理优化阶段直接产生，无对应逻辑算子）：

| 物理算子 | 额外信息 | 说明 |
|----------|----------|------|
| `AllNodeScanPhysicalOp` | `IAsyncGraphDataStore&`, `label_map`, `label_defs` | 扫描所有已知标签，加载完整 VertexValue |
| `LabelScanPhysicalOp` | `IAsyncGraphDataStore&`, `LabelId`, `label_defs` | 按标签 ID 扫描，加载完整 VertexValue |
| `IndexScanPhysicalOp` | `IAsyncGraphDataStore&`, `LabelId`, `prop_id`, `ScanMode` | 按索引扫描（等值/范围），物理优化阶段产生 |
| `ExpandPhysicalOp` | `IAsyncGraphDataStore&`, `optional<vector<EdgeLabelId>>`, `Schema` | 展开，按关系类型过滤（支持多类型），通过 schema 定位 src_var 列 |
| `FilterPhysicalOp` | `ExpressionEvaluator`, `label_defs` | 行内表达式求值过滤 |
| `ProjectPhysicalOp` | `ExpressionEvaluator`, `label_defs` | 行内表达式求值投影 |
| `AggregatePhysicalOp` | `ExpressionEvaluator`, `label_defs`, `group_keys`, `aggregates` | 聚合（count/sum/avg/min/max），支持分组和 DISTINCT |
| `SortPhysicalOp` | `ExpressionEvaluator`, `label_defs`, `sort_items` | 物化全部行后排序，支持多键和 ASC/DESC |
| `SkipPhysicalOp` | `int64_t skip` | 跳过前 N 行后传递 |
| `DistinctPhysicalOp` | — | 使用 `unordered_set<Row, RowHash, RowEqual>` 去重 |
| `LimitPhysicalOp` | `int64_t limit` | 限制行数 |
| `CreateNodePhysicalOp` | `IAsyncGraphDataStore&`, `VertexId`, `label_props` | 创建顶点（预分配 ID） |
| `CreateEdgePhysicalOp` | `IAsyncGraphDataStore&`, `EdgeId`, `src/dst` | 创建边（预分配 ID） |

关键差异：
- 逻辑计划的字符串 label/rel_type 在此阶段解析为 `LabelId` / `EdgeLabelId`。
- 物理算子持有 `IAsyncGraphDataStore&` 引用，用于执行实际的存储操作。
- `CreateNodeOp` / `CreateEdgeOp` 在此阶段预分配 VertexId / EdgeId（通过 `PlanContext::next_vertex_id` 递增），用于后续变量引用解析。

### 3.2 执行模型

所有物理算子的 `execute()` 方法返回 `folly::coro::AsyncGenerator<RowBatch>`，形成 **Pull-based 火山模型**：

```cpp
class PhysicalOperator {
    virtual folly::coro::AsyncGenerator<RowBatch> execute() = 0;
};
```

**Pull-based**：消费者通过 `co_await gen.next()` 拉取数据。父算子拉取子算子的输出，处理后再推给自己的消费者。

**批量处理**：`RowBatch` 最多包含 1024 行（`RowBatch::CAPACITY = 1024`），减少协程切换次数。相比逐行 `AsyncGenerator<Row>`，批量模式将协程切换开销降低约 1000 倍。

### 3.3 各算子执行逻辑

#### Scan 算子

```cpp
// LabelScanPhysicalOp::execute()
auto gen = store_.scanVerticesByLabel(label_id_);  // AsyncGenerator<vector<VertexId>>
while (auto batch = co_await gen.next()) {
    RowBatch output;
    for (VertexId vid : *batch) {
        auto props = co_await store_.getVertexProperties(vid, label_id_);
        VertexValue vv;
        vv.id = vid;
        vv.labels = LabelIdSet{label_id_};
        vv.properties = std::move(props);
        Row row;
        row.push_back(Value(std::move(vv)));
        output.push_back(std::move(row));
    }
    if (!output.empty()) co_yield std::move(output);
}
```

Scan 算子通过 `IAsyncGraphDataStore` 发起异步扫描，每次拉取一批 VertexId，加载完整的 `VertexValue`（含属性和标签），转为 `RowBatch` 向上输出。

#### IndexScan 算子

`IndexScanPhysicalOp` 由物理优化阶段从 `Filter(LabelScan)` 模式推导产生，不经过逻辑计划。当 Filter 的谓词匹配 `PropertyAccess op Literal` 模式且目标属性存在 PUBLIC 索引时，将 Filter + LabelScan 替换为 IndexScan。支持等值查询（`ScanMode::EQUALITY`）和范围查询（`ScanMode::RANGE`）。

#### Expand 算子

```cpp
// ExpandPhysicalOp::execute()
// label_filters_: nullopt → 扫描全部边；empty vector → 无匹配类型，不扫描；非空 → 逐类型 scanEdges
auto child_gen = child_->execute();
while (auto child_batch = co_await child_gen.next()) {
    for (const auto& row : child_batch->rows) {
        VertexId src_id = row[src_col_idx_];  // 通过 schema 定位 src_var 列索引
        for (const auto& label_filter : scan_filters) {
            auto edge_gen = store_.scanEdges(src_id, dir, label_filter);
            while (auto edge_batch = co_await edge_gen.next()) {
                // 每条边产生一个新 row（原 row + 目标顶点 + 可选边值）
            }
        }
    }
    co_yield output;
}
```

**关系类型过滤**：
- 无类型（`-[]->`）：`label_filters_` 为 `nullopt`，单次 `scanEdges(vid, dir, nullopt)` 扫描全部边
- 单类型（`[:KNOWS]`）：`label_filters_` 为 `{label_id}`，单次 `scanEdges(vid, dir, label_id)` 按前缀过滤
- 多类型（`[:KNOWS|FRIEND]`）：对每个类型分别调用 `scanEdges`，合并结果
- 类型不存在：`label_filters_` 为空 vector，不发起扫描，返回 0 行

**src_var 列定位**：构造时通过 `src_var_` 在 `input_schema_` 中查找列索引 `src_col_idx_`，执行时直接按索引取值，支持多跳展开（`(a)->(b)->(c)` 中第二跳从 `b` 列而非 `a` 列展开）。

Expand 是嵌套循环：对输入的每一行，扫描该顶点的边，每条边生成一行输出。

#### Filter / Project

纯计算算子，不涉及 IO。在 Compute 线程上执行表达式求值。

#### Limit

```cpp
// LimitPhysicalOp::execute()
auto child_gen = child_->execute();
int64_t remaining = limit_;
while (auto batch = co_await child_gen.next()) {
    RowBatch output;
    for (auto& row : batch->rows) {
        if (remaining <= 0) {
            if (!output.empty()) co_yield std::move(output);  // 先输出已累积的行
            co_return;
        }
        output.push_back(std::move(row));
        --remaining;
    }
    if (!output.empty()) co_yield std::move(output);
    if (remaining <= 0) co_return;
}
```

Limit 在达到限制前持续拉取子算子输出。达到限制时，先 yield 已累积的行再返回，避免丢弃合法数据。

#### Skip

```cpp
// SkipPhysicalOp::execute()
int64_t remaining = skip_;
while (auto batch = co_await child_gen.next()) {
    RowBatch output;
    for (auto& row : batch->rows) {
        if (remaining > 0) { --remaining; continue; }
        output.push_back(std::move(row));
    }
    if (!output.empty()) co_yield std::move(output);
}
```

Skip 跳过前 N 行，后续行直接传递给父算子。

#### Sort

物化全部子算子输出到内存，使用 `std::sort` 排序。排序依据 `sort_items` 中的表达式，支持多键和 ASC/DESC 方向。排序完成后按批次 yield。

由于需要物化全部数据，Sort 是阻断算子（blocking operator），不适用于无界数据集。

#### Distinct

使用 `std::unordered_set<Row, RowHash, RowEqual>` 记录已见行。`RowHash` 对 Row 中每个 Value 逐个哈希并组合，`RowEqual` 逐位置比较。仅首次出现的行向下游传递。

#### Aggregate

```cpp
// AggregatePhysicalOp 核心逻辑
std::unordered_map<Row, GroupState, RowHash, RowEqual> groups;

// 1. 消费所有子算子输出，按 group key 分组并累积聚合值
for (const auto& row : child_rows) {
    Row key = computeGroupKey(row);
    auto& state = groups[key];
    for (auto& agg : aggregates_) {
        Value v = evaluator_.evaluate(agg.arg, row, input_schema_);
        // 根据 func_name 累积到 state
    }
}

// 2. 每个分组输出一行：group_key 值 + 聚合结果值
for (const auto& [key, state] : groups) {
    Row out_row = key + computeAggResults(state);
    co_yield out_row;
}
```

聚合算子支持 5 种聚合函数：

| 函数 | 累积方式 | 返回类型 |
|------|----------|----------|
| `count(*)` / `count(expr)` | 计数 | `int64_t` |
| `sum(expr)` | 求和 | `int64_t` |
| `avg(expr)` | 求和 + 计数，输出 sum/count | `double` |
| `min(expr)` | 最小值 | `int64_t` |
| `max(expr)` | 最大值 | `int64_t` |

支持 `DISTINCT` 聚合（如 `count(DISTINCT n.city)`），通过 `unordered_set<Value>` 去重后累积。无 GROUP BY 的全局聚合在无输入数据时仍输出一行（count=0，其余为 null）。

#### CreateNode / CreateEdge

写操作算子。通过 `co_await store_.insertVertex/insertEdge` 异步执行写入，IO 在 IO 线程上完成。

### 3.4 PhysicalPlanner

```cpp
struct PlanOperatorResult {
    std::unique_ptr<PhysicalOperator> op;
    Schema output_schema;
};

class PhysicalPlanner {
    std::variant<unique_ptr<PhysicalOperator>, string>
    plan(LogicalPlan& logical_plan, IAsyncGraphDataStore& store, PlanContext& ctx);

    std::variant<PlanOperatorResult, string>
    planOperator(LogicalOperator& op, IAsyncGraphDataStore& store, PlanContext& ctx, Schema input_schema);
};
```

`PlanContext` 携带：
- `label_name_to_id` / `edge_label_name_to_id`：名称到 ID 的映射
- `label_defs` / `edge_label_defs`：LabelId/EdgeLabelId → LabelDef/EdgeLabelDef 映射（含属性定义、索引定义）
- `variable_vertex_ids` / `variable_edge_ids`：CREATE 语句中的变量到预分配 ID 的映射
- `next_vertex_id` / `next_edge_id`：ID 分配计数器

翻译过程递归遍历逻辑算子树，对每种算子类型创建对应的物理算子，绑定 `IAsyncGraphDataStore` 引用和解析后的 ID。`planOperator` 返回 `PlanOperatorResult`，包含物理算子和输出 schema，供下游算子引用。

**Schema 跟踪**：每个算子的 `planOperator` 返回 `output_schema`，供需要 schema 的下游算子（如 Filter、Project、Aggregate）使用。Scan 算子建立初始 schema（变量名），Expand 扩展 schema（追加目标/边变量），Filter/Limit/Sort/Skip/Distinct 透传子算子 schema，Project 用其列名覆盖 schema，Aggregate 用 group_key_names + agg_names 构建 schema。

**label_defs 传递**：需要属性求值的算子（Filter、Project、Sort、Aggregate）通过 `evaluator_.setLabelDefs(&ctx.label_defs)` 接收 label 定义，用于 PropertyAccess 求值时将属性名解析为 prop_id。

**索引扫描优化**：当 Filter 的子节点是 LabelScan 且谓词匹配可索引模式时，`tryIndexScan` 将 Filter + LabelScan 替换为 `IndexScanPhysicalOp`，跳过全表扫描。

### 3.5 文件结构

```
src/compute_service/physical_plan/
  physical_operator.hpp    -- 物理算子类定义
  physical_operator.cpp    -- 各算子 execute() 实现
  physical_planner.hpp     -- PhysicalPlanner 类声明 + PlanContext
  physical_planner.cpp     -- 逻辑 → 物理翻译实现
```

---

## 四、运行时类型

### 4.1 Value

```cpp
using Value = std::variant<
    std::monostate,  // null
    bool,
    int64_t,
    double,
    std::string,
    VertexValue,     // 顶点（id + 属性 + 标签）
    EdgeValue,       // 边（id + src + dst + label + 属性）
    ListValue        // 列表（递归通过 ValueStorage 包装）
>;
```

使用 `std::variant` 实现类型安全的运行时值。`VertexValue` 和 `EdgeValue` 携带完整的图元素信息。

### 4.2 Row / Schema

```cpp
using Row = std::vector<Value>;        // 位置式行，列索引对应 schema
using Schema = std::vector<string>;    // 列名列表
```

行是位置式的：第 i 个元素对应 Schema 中第 i 个列名。Schema 由逻辑计划阶段的符号表决定。

### 4.3 RowBatch

```cpp
struct RowBatch {
    static constexpr size_t CAPACITY = 1024;
    std::vector<Row> rows;
};
```

批量传输单元，减少协程切换频率。

---

## 五、表达式求值

```cpp
class ExpressionEvaluator {
    Value evaluate(const Expression& expr, const Row& row, const Schema& schema);
    void setLabelDefs(const std::unordered_map<LabelId, LabelDef>* defs);
private:
    const std::unordered_map<LabelId, LabelDef>* label_defs_ = nullptr;
};
```

对 AST 表达式进行运行时求值。`label_defs_` 指向 label 定义映射，用于属性求值时将属性名解析为 prop_id 并从 `VertexValue::properties` 向量中按索引取值。当前支持：

| 表达式类型 | 示例 | 说明 |
|-----------|------|------|
| `Literal` | `42`, `"hello"`, `true`, `null` | 字面量 |
| `Variable` | `n`, `a` | 按 schema 列名查找 |
| `BinaryOp` | `n.age > 20`, `a AND b`, `x + 1` | 比较、逻辑、算术 |
| `UnaryOp` | `NOT x`, `-x`, `x IS NULL` | 逻辑取反、取负、空判断 |
| `PropertyAccess` | `n.id` | 属性访问 |
| `FunctionCall` | `id(n)` | 内置函数 |

---

## 六、QueryExecutor

### 6.1 编排流程

```cpp
folly::coro::Task<ExecutionResult> executeAsync(const string& cypher_query) {
    // 0. 尝试 Index DDL（CREATE/DROP/SHOW INDEX）
    auto ddl_stmt = IndexDdlParser::tryParse(cypher_query);
    if (ddl_stmt) { handleIndexDdl(...); co_return; }

    // 0.5 检查 EXPLAIN 前缀
    if (is_explain) query_to_parse = trimmed.substr(8);

    // 1. 解析
    auto stmt = parser.parse(query_to_parse);

    // 2. 构建逻辑计划
    auto logical_plan = plan_builder.build(stmt);

    // 3. 加载元数据（label/edge_label 映射）
    auto labels = co_await async_meta_.listLabels();
    auto edge_labels = co_await async_meta_.listEdgeLabels();

    // 4. 规划物理算子
    GraphTxnHandle txn = co_await async_data_.beginTran();
    async_data_.setTransaction(txn);
    auto phys_op = physical_planner.plan(logical_plan, async_data_, ctx);

    // 4.5 EXPLAIN 模式：格式化计划树并返回，不执行
    if (is_explain) { formatExplainPlan(...); co_return; }

    // 5. 协程管道执行
    auto gen = phys_op->execute();
    while (auto batch = co_await gen.next()) {
        // 收集结果
    }

    // 6. 提交事务
    co_await async_data_.commitTran(txn);
}
```

**EXPLAIN**：当查询以 `EXPLAIN` 前缀开头时，仅构建物理计划树但不执行，将算子树格式化为文本返回。

**Index DDL**：`CREATE VERTEX INDEX`、`DROP INDEX`、`SHOW INDEXES` 等语句由 `IndexDdlParser` 识别并直接处理，不经过逻辑/物理计划管道。创建索引时包含回填（backfill）流程：扫描已有顶点并插入索引条目。

**列名提取**：`extractColumnsFromLogicalPlan` 从逻辑算子树中提取输出列名。对 `ProjectOp` 取 item alias/表达式名，对 `AggregateOp` 取 `output_names`，对 Sort/Skip/Distinct/Filter/Limit 透传子算子的列名。

### 6.2 同步接口

```cpp
std::vector<Row> executeSync(const string& cypher_query) {
    return folly::coro::blockingWait(executeAsync(cypher_query));
}
```

### 6.3 事务管理

每次查询在独立事务中执行，事务通过 async 接口管理：
1. 查询开始时 `co_await async_data_.beginTran()` 获取事务句柄
2. `async_data_.setTransaction(txn)` 设置事务上下文
3. 查询执行中所有存储操作使用该事务
4. 查询完成后 `co_await async_data_.commitTran(txn)` 提交
5. 出错时 `co_await async_data_.rollbackTran(txn)` 回滚

QueryExecutor 只依赖 `IAsyncGraphDataStore` 和 `IAsyncGraphMetaStore`，不直接依赖任何 sync 接口。

---

## 七、测试覆盖

### 7.1 逻辑计划测试（test_logical_plan.cpp）

覆盖逻辑算子构建和 AST → LogicalPlan 翻译路径。

### 7.2 查询执行器测试（test_query_executor.cpp）

端到端测试，使用 WiredTiger 后端，81 个测试用例：

#### 7.2.1 扫描测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `LabelScanReturnVertex` | `MATCH (n:Person) RETURN n` | 按标签扫描，返回 5 个顶点 |
| `LabelScanSpecificLabel` | `MATCH (n:Person) RETURN n` | 指定标签扫描 |
| `LabelScanOtherLabelEmpty` | `MATCH (n:City) RETURN n` | 不存在的标签返回 0 行 |
| `LabelScanWithWhereTrueAndLimit` | `MATCH (n:Person) WHERE true RETURN n LIMIT 3` | WHERE + LIMIT 组合 |
| `LabelScanMultipleLabelsIndependently` | 多标签插入 + 独立扫描 | Person、City、AllNodeScan 分别正确 |
| `AllNodeScanWithLimit` | `MATCH (n) RETURN n LIMIT 3` | 全节点扫描 + LIMIT |
| `AllNodeScanAllVertices` | `MATCH (n) RETURN n` | 全节点扫描所有标签 |
| `AllNodeScanNoData` | `MATCH (n) RETURN n` | 空图全节点扫描 |
| `AllNodeScanLimitZero` | `MATCH (n) RETURN n LIMIT 0` | LIMIT 0 返回 0 行 |
| `AllNodeScanLimitOne` | `MATCH (n) RETURN n LIMIT 1` | LIMIT 1 返回 1 行 |

#### 7.2.2 WHERE 过滤测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `WhereTruePassesAllRows` | `WHERE true` | 全部通过 |
| `WhereFalseFiltersAllRows` | `WHERE false` | 全部过滤 |
| `WhereTrueWithLimit` | `WHERE true ... LIMIT 2` | WHERE + LIMIT 组合 |
| `WhereAndTrueTrue` | `WHERE true AND true` | AND 逻辑 |
| `WhereAndTrueFalse` | `WHERE true AND false` | AND 过滤 |
| `WhereOrFalseTrue` | `WHERE false OR true` | OR 通过 |
| `WhereOrFalseFalse` | `WHERE false OR false` | OR 过滤 |
| `WhereNotTrue` | `WHERE NOT false` | NOT 取反 |
| `WhereNotFalse` | `WHERE NOT true` | NOT 过滤 |
| `WhereComplexExpression` | `WHERE (true AND true) OR false` | 复合表达式 |
| `WhereComplexExpressionFiltersAll` | `WHERE (true AND false) OR false` | 复合过滤 |
| `WhereOnEmptyData` | 空图 `WHERE true` | 空数据 WHERE |

#### 7.2.3 展开测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `ExpandOutgoingEdges` | `MATCH (a)-[:KNOWS]->(b) RETURN a, b` | 出边展开 |
| `ExpandNoEdgesReturnsEmpty` | 无边时展开 | 返回 0 行 |
| `ExpandMultipleEdgesFromSameSource` | 同一源点 4 条出边 | 返回 4 行 |
| `ExpandWithWhereFilter` | `... WHERE true RETURN a, b` | 展开后 WHERE 过滤 |
| `ExpandWithWhereFalseFiltersAll` | `... WHERE false RETURN a, b` | 展开后 WHERE 全过滤 |
| `ExpandWithLimit` | 展开 4 条边 + LIMIT 2 | 返回 2 行 |
| `ExpandFilterBySingleType` | 2 KNOWS + 2 LIVES_IN 边，`[:KNOWS]` | 仅返回 KNOWS 边 |
| `ExpandFilterByOtherType` | 同上，`[:LIVES_IN]` | 仅返回 LIVES_IN 边 |
| `ExpandFilterByMultipleTypes` | 同上，`[:KNOWS\|LIVES_IN]` | 返回全部 4 条边 |
| `ExpandNonExistentTypeReturnsEmpty` | 同上，`[:NONEXISTENT]` | 返回 0 行 |
| `ExpandWithoutTypeReturnsAll` | 同上，`-->`（无类型） | 返回全部 4 条边 |
| `ExpandTwoHopSameType` | KNOWS 链 1→2→3→4，2 跳 `[:KNOWS]` | 返回 2 行 |
| `ExpandTwoHopMixedTypes` | KNOWS+LIVES_IN，2 跳 `[:KNOWS]->[:LIVES_IN]` | 返回 1 行 |
| `ExpandThreeHopSameType` | KNOWS 链，3 跳 `[:KNOWS]` | 返回 1 行 |
| `ExpandTwoHopNoMatch` | LIVES_IN 后接 KNOWS，无匹配路径 | 返回 0 行 |

#### 7.2.4 创建测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `CreateNode` | `CREATE (n:Person)` | 创建顶点，验证写入 |
| `CreateNodeReturnsId` | `CREATE (n:Person)` | 返回值为 int64_t 类型的顶点 ID |
| `CreateMultipleNodesSequentially` | 连续两次 CREATE | ID 递增，scan 验证 |
| `CreateNodeVerifyInStore` | `CREATE` + cursor 扫描 | 存储层直接验证 |
| `CreateNodeDifferentLabels` | `CREATE Person` + `CREATE City` | 不同标签独立计数 |
| `CreateEdgeReturnsId` | `CREATE (a)-[:KNOWS]->(b)` | 返回边 ID |
| `CreateEdgeVerifyInStore` | 创建边后 scan + expand 验证 | 顶点和边都正确创建 |
| `CreateEdgeThenExpand` | 创建边后 MATCH 展开 | 端到端验证 |

#### 7.2.5 组合与边界测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `EmptyScan` | 空图 `MATCH (n:Person) RETURN n` | 返回 0 行 |
| `EmptyGraphAllOperations` | 空图多种查询 | 全部返回 0 行 |
| `CreateThenScanThenFilter` | 3 次 CREATE + WHERE true | 创建后查询 |
| `CreateThenScanWithLimit` | 5 次 CREATE + LIMIT 2 | 创建后 LIMIT |
| `LimitGreaterThanRowCount` | 5 行 LIMIT 100 | 返回 5 行 |
| `LimitExactlyRowCount` | 5 行 LIMIT 5 | 返回 5 行 |
| `LimitOne` | 5 行 LIMIT 1 | 返回 1 行 |
| `ReturnStar` | `RETURN *` | 全列返回 |
| `ReturnStarWithLimit` | `RETURN * LIMIT 2` | 全列 + LIMIT |

#### 7.2.6 EXPLAIN 测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `ExplainLabelScan` | `EXPLAIN MATCH (n:Person) RETURN n` | 显示 LabelScan 算子 |
| `ExplainAllNodeScan` | `EXPLAIN MATCH (n) RETURN n` | 显示 AllNodeScan 算子 |
| `ExplainWithFilter` | `EXPLAIN ... WHERE true RETURN n` | 显示 Filter 算子 |
| `ExplainWithLimit` | `EXPLAIN ... RETURN n LIMIT 5` | 显示 Limit 算子 |
| `ExplainCreateNode` | `EXPLAIN CREATE (n:Person)` | 显示 CreateNode 算子 |
| `ExplainDoesNotExecute` | `EXPLAIN CREATE ...` | 不实际执行写入 |
| `ExplainCaseInsensitive` | `explain MATCH ...` | 前缀大小写不敏感 |

#### 7.2.7 SKIP 测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `SkipRows` | `SKIP 2` | 跳过 2 行 |
| `SkipZero` | `SKIP 0` | 不跳过任何行 |
| `SkipAll` | `SKIP 100`（超过行数） | 返回 0 行 |
| `SkipWithLimit` | `SKIP 2 LIMIT 2` | 跳过 + 限制组合 |
| `SkipWithOrderBy` | `ORDER BY n.age SKIP 1` | 排序后跳过 |

#### 7.2.8 ORDER BY 测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `OrderByPropertyAsc` | `ORDER BY n.age ASC` | 升序排列 |
| `OrderByPropertyDesc` | `ORDER BY n.age DESC` | 降序排列 |
| `OrderByDefaultDirection` | `ORDER BY n.age` | 默认升序 |
| `OrderByMultipleKeys` | `ORDER BY n.city, n.age` | 多键排序 |
| `OrderByEmptyResult` | 空图 `ORDER BY` | 返回 0 行 |

#### 7.2.9 DISTINCT 测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `DistinctProperty` | `RETURN DISTINCT n.city` | 属性去重 |
| `DistinctNoDuplicate` | 无重复数据时 DISTINCT | 行数不变 |
| `DistinctWithOrderBy` | `DISTINCT ... ORDER BY` | 去重 + 排序组合 |

#### 7.2.10 聚合测试

| 测试 | 查询 | 验证点 |
|------|------|--------|
| `CountStar` | `RETURN count(*)` | 全局计数 |
| `CountByGroup` | `RETURN n.city, count(*)` | 分组计数 |
| `SumAge` | `RETURN sum(n.age)` | 求和 |
| `AvgAge` | `RETURN avg(n.age)` | 平均值（double） |
| `MinMaxAge` | `RETURN min(n.age), max(n.age)` | 最小/最大值 |
| `CountDistinct` | `RETURN count(DISTINCT n.city)` | 去重计数 |
| `MultipleAggregates` | `RETURN count(*), sum(n.age), avg(n.age)` | 多聚合函数 |
| `AggregateWithGroupBy` | `RETURN n.city, count(*), avg(n.age)` | 分组 + 多聚合 |
| `AggregateEmptyResult` | 空图 `RETURN count(*)` | 空数据全局聚合返回 count=0 |
| `AggregateWithOrderBy` | `GROUP BY ... ORDER BY cnt DESC` | 聚合后排序 |
| `AggregateWithSkipLimit` | `GROUP BY ... SKIP 1 LIMIT 2` | 聚合后跳过 + 限制 |

#### 7.2.11 Restart 持久化测试

| 测试 | 说明 | 验证点 |
|------|------|--------|
| `DataPersistsAcrossRestart` | 关闭重开后扫描 | 顶点数据持久化 |
| `DataWithPropertiesPersistsAcrossRestart` | 属性持久化 | 属性值正确恢复 |
| `EdgeDataPersistsAcrossRestart` | 边数据持久化 | 边和顶点都正确恢复 |

---

## 八、流式执行

### 8.1 背景与动机

物理算子内部已经是 `AsyncGenerator<RowBatch>` 的 Pull-based 流式模型，但 `QueryExecutor::executeAsync()` 在执行阶段将所有 batch 收集到 `vector<Row>` 中，RPC 层再将完整结果序列化为单个 Thrift 响应返回。存在以下问题：

| 问题 | 说明 |
|------|------|
| 内存压力 | 大查询（全表扫描）结果集全量物化，并发查询可导致 OOM |
| 首字节延迟 | Shell/RPC 客户端必须等到整个查询执行完才能看到结果 |
| 无反压 | executor 端无条件消费所有 batch，下游消费慢时内存堆积 |
| RPC 超时风险 | 单个 Thrift 响应承载全量结果，查询耗时越长越容易超时 |
| 无法提前终止 | 客户端断连后 executor 仍执行完整个查询 |

### 8.2 改造方案概览

将 `executeCypher` RPC 改为 fbthrift 流式响应（`stream`），实现全链路流式传输：

```
物理算子 AsyncGenerator<RowBatch>
    │
    ▼
StreamContext（持有 phys_op + 事务）
    │
    ▼
Handler: AsyncGenerator<ResultRowBatch>  ← 逐批转换 Value → Thrift 类型
    │
    ▼
fbthrift ServerStream<ResultRowBatch>    ← 自动反压（credit-based）
    │
    ▼
RPC 传输层                               ← Rocket 帧流式传输
    │
    ▼
Client: ClientBufferedStream<ResultRowBatch>  ← toAsyncGenerator() 消费
```

### 8.3 Thrift IDL 变更

```thrift
// 新增：流式响应类型
struct QueryStreamMeta {
  1: list<string> columns
}

struct ResultRowBatch {
  1: list<ResultRow> rows
}

// executeCypher 改为流式：先返回元数据（列名），再流式返回行批次
// 删除原有 QueryResult（不再需要）
service EuGraphService {
  // ... 其他方法不变 ...
  QueryStreamMeta,stream<ResultRowBatch> executeCypher(1: string query)
}
```

生成的关键类型：
- **Server handler**: `co_executeCypher` 返回 `ResponseAndServerStream<QueryStreamMeta, ResultRowBatch>`
- **Client**: `co_executeCypher` 返回 `ResponseAndClientBufferedStream<QueryStreamMeta, ResultRowBatch>`
- `ServerStream<T>` 可从 `AsyncGenerator<T&&>` 隐式构造
- `ClientBufferedStream<T>` 提供 `toAsyncGenerator()` 和 `subscribeInline()` 两种消费方式

### 8.4 StreamContext

```cpp
struct StreamContext {
    Schema columns;
    std::string error;
    std::unique_ptr<PhysicalOperator> phys_op;  // 保持物理算子存活
    folly::coro::AsyncGenerator<RowBatch> gen;   // 从 phys_op 创建
    GraphTxnHandle txn;
    IAsyncGraphDataStore* store;
};
```

`shared_ptr<StreamContext>` 确保物理算子和事务在流消费期间始终存活。`phys_op` 通过 `unique_ptr` 持有，地址在 `StreamContext` 生命周期内不变，generator 中对 `phys_op` 的引用始终有效。

### 8.5 QueryExecutor 变更

新增 `prepareStream()` 方法：

```cpp
// 返回流式执行上下文（parse → plan → beginTxn → create generator）
folly::coro::Task<std::shared_ptr<StreamContext>>
prepareStream(const std::string& cypher_query);
```

执行步骤与 `executeAsync()` 一致（parse → logical plan → physical plan → beginTxn），区别在于不执行第 6 步（消费 generator），而是将 generator 和事务句柄打包到 `StreamContext` 中返回给调用者。

`executeAsync()` 保持不变，供测试和非流式场景使用。

### 8.6 Handler 流式实现

```cpp
folly::coro::Task<ResponseAndServerStream<QueryStreamMeta, ResultRowBatch>>
co_executeCypher(std::unique_ptr<std::string> query) override {
    // 1. 准备流式上下文
    auto ctx = co_await executor_.prepareStream(*query);
    if (!ctx->error.empty()) {
        throw std::runtime_error(ctx->error);  // fbthrift 将异常发送给客户端
    }

    // 2. 获取 label 定义（用于 Vertex/Edge → JSON 序列化）
    auto label_defs = ...;
    auto edge_label_defs = ...;

    // 3. 构建初始响应
    QueryStreamMeta meta;
    meta.columns() = std::move(ctx->columns);

    // 4. 创建 AsyncGenerator：逐批拉取 → 转换为 Thrift 类型 → yield
    //    流结束时 commit 事务
    auto gen = [ctx = std::move(ctx),
                label_defs = std::move(label_defs),
                edge_label_defs = std::move(edge_label_defs),
                this]() mutable -> folly::coro::AsyncGenerator<ResultRowBatch&&> {
        while (auto batch = co_await ctx->gen.next()) {
            ResultRowBatch thrift_batch;
            for (auto& row : batch->rows) {
                thrift::ResultRow row_resp;
                for (auto& val : row) {
                    row_resp.values()->push_back(
                        valueToThrift(val, label_defs, edge_label_defs));
                }
                thrift_batch.rows()->push_back(std::move(row_resp));
            }
            co_yield std::move(thrift_batch);
        }
        // 流结束，提交事务
        co_await ctx->store->commitTran(ctx->txn);
    };

    co_return ResponseAndServerStream{std::move(meta), std::move(gen)};
}
```

### 8.7 事务生命周期

| 场景 | 行为 |
|------|------|
| 正常完成 | generator while 循环结束后 `co_await commitTran` |
| 客户端断连 | generator 被销毁，事务隐式回滚（WiredTiger session 关闭时自动 rollback） |
| 执行期错误 | generator 提前结束（不再 yield），事务隐式回滚 |

### 8.8 客户端流式消费

**RPC 模式 Shell**：

```cpp
// sync 流式 API
auto [meta, stream] = client_->sync_executeCypher(query);
std::move(stream).subscribeInline([&](folly::Try<ResultRowBatch> batch) {
    if (batch.hasValue()) {
        for (auto& row : *batch->rows()) {
            // 逐行打印
        }
    }
});
```

**Embedded 模式 Shell**：绕过 handler，直接调用 `executor_.executeAsync()`（本地执行无内存/延迟问题，保持现有表格格式化输出）。

### 8.9 改动范围

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| `proto/eugraph.thrift` | IDL 改为流式 | 新增 `QueryStreamMeta`, `ResultRowBatch`，`executeCypher` 改为 `stream` |
| `src/gen-cpp2/*` | 重新生成 | thrift1 重新编译 |
| `query_executor.hpp/cpp` | 新增 `prepareStream()` | 返回 `shared_ptr<StreamContext>` |
| `eugraph_handler.hpp/cpp` | 重写 `co_executeCypher()` | 返回 `ResponseAndServerStream` |
| `rpc_client.hpp/cpp` | 更新为流式客户端 | `sync_executeCypher` 返回流 |
| `shell_repl.cpp` | RPC 模式消费流式响应 | `subscribeInline` 逐批打印 |

---

## 十、后续扩展

### 当前已实现（M1-M4）

- [x] Cypher Parser + AST（M1）
- [x] 逻辑计划构建（M2）
- [x] 物理计划 + 协程执行基础设施（M3）
- [x] 基础读执行器：Scan / Expand / Filter / Project / Limit
- [x] 基础读增强（M4）：ORDER BY、DISTINCT、聚合（count/sum/avg/min/max + GROUP BY）、SKIP
- [x] EXPLAIN 语句：物理计划树可视化
- [x] 索引扫描：Filter + LabelScan → IndexScan 优化

### 待实现

| 里程碑 | 内容 | 说明 |
|--------|------|------|
| M5 | 写操作完善 | SET, DELETE, MERGE, 批量 CREATE |
| M6 | 多 MATCH + JOIN | 多个 MATCH 子句, HashJoin, Apply |
| M7 | 查询优化器 | 谓词下推, 投影裁剪, 基于 `LogicalPlanVisitor` |
| M8 | WITH 子句 | 流水线断点, 变量作用域重置 |
| M9 | UNWIND + 列表操作 | 展开列表为行 |

---

## 十一、依赖

| 依赖 | 用途 | 版本 |
|------|------|------|
| `folly` | 协程框架（co, AsyncGenerator, Task） | vcpkg |
| `antlr4` | Cypher 语法解析 | 预生成 C++ 代码 |
| `spdlog` | 日志 | vcpkg |
| `gtest` | 单元测试 | vcpkg |
| `wiredtiger` | 存储引擎 | submodule |
