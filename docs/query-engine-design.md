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

使用 `std::variant<unique_ptr<XxxOp>>` 模式（与 AST 一致），8 种算子：

| 算子 | 说明 | 子节点 |
|------|------|--------|
| `AllNodeScanOp` | 全顶点扫描 | 0 |
| `LabelScanOp` | 按标签扫描顶点 | 0 |
| `ExpandOp` | 从顶点展开邻居 | 1（输入） |
| `FilterOp` | 过滤行 | 1（输入） |
| `ProjectOp` | 投影/重映射列 | 1（输入） |
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
| `RETURN items` | `ProjectOp` | 投影 |
| `RETURN ... LIMIT N` | `LimitOp` | 限制行数 |
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

物理算子与逻辑算子一一对应，但增加了执行所需的上下文：

| 物理算子 | 额外信息 | 说明 |
|----------|----------|------|
| `AllNodeScanPhysicalOp` | `IAsyncGraphDataStore&`, `label_map` | 扫描所有已知标签 |
| `LabelScanPhysicalOp` | `IAsyncGraphDataStore&`, `LabelId` | 按标签 ID 扫描 |
| `ExpandPhysicalOp` | `IAsyncGraphDataStore&`, `EdgeLabelId` | 展开，按关系类型 ID 过滤 |
| `FilterPhysicalOp` | `ExpressionEvaluator` | 行内表达式求值过滤 |
| `ProjectPhysicalOp` | `ExpressionEvaluator` | 行内表达式求值投影 |
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
        Row row;
        row.push_back(Value(static_cast<int64_t>(vid)));
        output.push_back(std::move(row));
    }
    if (!output.empty()) co_yield std::move(output);
}
```

Scan 算子通过 `IAsyncGraphDataStore` 发起异步扫描，每次拉取一批 VertexId，转为 `RowBatch` 向上输出。

#### Expand 算子

```cpp
// ExpandPhysicalOp::execute()
auto child_gen = child_->execute();
while (auto child_batch = co_await child_gen.next()) {
    for (const auto& row : child_batch->rows) {
        VertexId src_id = extractSrcId(row);
        auto edge_gen = store_.scanEdges(src_id, dir, label_filter_);
        while (auto edge_batch = co_await edge_gen.next()) {
            // 每条边产生一个新 row（原 row + 目标顶点 ID + 可选边 ID）
        }
    }
    co_yield output;
}
```

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

#### CreateNode / CreateEdge

写操作算子。通过 `co_await store_.insertVertex/insertEdge` 异步执行写入，IO 在 IO 线程上完成。

### 3.4 PhysicalPlanner

```cpp
class PhysicalPlanner {
    std::variant<unique_ptr<PhysicalOperator>, string>
    plan(LogicalPlan& logical_plan, IAsyncGraphDataStore& store, PlanContext& ctx);
};
```

`PlanContext` 携带：
- `label_name_to_id` / `edge_label_name_to_id`：名称到 ID 的映射
- `variable_vertex_ids` / `variable_edge_ids`：CREATE 语句中的变量到预分配 ID 的映射
- `next_vertex_id` / `next_edge_id`：ID 分配计数器

翻译过程递归遍历逻辑算子树，对每种算子类型创建对应的物理算子，绑定 `IAsyncGraphDataStore` 引用和解析后的 ID。

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
};
```

对 AST 表达式进行运行时求值。当前支持：

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
folly::coro::AsyncGenerator<RowBatch> execute(const string& cypher_query) {
    // 1. 解析
    auto stmt = parser.parse(cypher_query);

    // 2. 构建逻辑计划
    auto logical_plan = plan_builder.build(stmt);

    // 3. 规划物理算子
    GraphTxnHandle txn = co_await async_data_.beginTran();
    async_data_.setTransaction(txn);
    auto phys_op = physical_planner.plan(logical_plan, async_data_, ctx);

    // 4. 协程管道执行
    auto gen = phys_op->execute();
    while (auto batch = co_await gen.next()) {
        co_yield std::move(*batch);
    }

    // 5. 提交事务
    co_await async_data_.commitTran(txn);
}
```

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

端到端测试，使用 WiredTiger 后端，约 40 个测试用例：

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

---

## 八、后续扩展

### 当前已实现（M1-M3）

- [x] Cypher Parser + AST（M1）
- [x] 逻辑计划构建（M2）
- [x] 物理计划 + 协程执行基础设施（M3）
- [x] 基础读执行器：Scan / Expand / Filter / Project / Limit

### 待实现

| 里程碑 | 内容 | 说明 |
|--------|------|------|
| M4 | 基础读增强 | ORDER BY, DISTINCT, 聚合（count/sum/avg）, Skip |
| M5 | 写操作完善 | SET, DELETE, MERGE, 批量 CREATE |
| M6 | 多 MATCH + JOIN | 多个 MATCH 子句, HashJoin, Apply |
| M7 | 查询优化器 | 谓词下推, 投影裁剪, 基于 `LogicalPlanVisitor` |
| M8 | WITH 子句 | 流水线断点, 变量作用域重置 |
| M9 | UNWIND + 列表操作 | 展开列表为行 |

---

## 九、依赖

| 依赖 | 用途 | 版本 |
|------|------|------|
| `folly` | 协程框架（co, AsyncGenerator, Task） | vcpkg |
| `antlr4` | Cypher 语法解析 | 预生成 C++ 代码 |
| `spdlog` | 日志 | vcpkg |
| `gtest` | 单元测试 | vcpkg |
| `wiredtiger` | 存储引擎 | submodule |
