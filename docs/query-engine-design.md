# 查询引擎设计

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

---

## 概述

查询引擎将 Cypher 文本翻译为可执行的物理算子树，通过协程异步执行。

设计目标：
1. **计算层不阻塞**：folly 协程驱动，IO 等待时让出 CPU
2. **IO 与计算分离**：存储引擎调用通过 `IoScheduler` 调度到 IO 线程池
3. **批量流水线**：`RowBatch`（1024 行/批）减少协程切换

运行时执行细节见 [execution-model.md](execution-model.md)。

---

## 执行流水线

```
Cypher 查询文本
    → CypherQueryParser: 字符串 → AST (Statement)
    → LogicalPlanBuilder: AST → LogicalPlan (算子树，名称保持字符串)
    → PhysicalPlanner: LogicalPlan → PhysicalOperator 树 (ID 解析、存储引用绑定)
    → QueryExecutor: 协程管道执行 (Pull-based 火山模型)
```

---

## 一、逻辑计划

### 算子类型（12 种）

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

### 关键设计决策

**逻辑计划保持字符串名称**：label/关系类型名保持为字符串，name→ID 解析推迟到物理计划阶段。这样逻辑计划关注语义正确性，不依赖存储 ID 映射，便于后续做规则优化。

**符号表**：`LogicalPlanBuilder` 内部维护 `SymbolTable`（变量名→列索引），跟踪当前行模式。MATCH 中每个变量绑定分配一个列索引，RETURN/WHERE 通过索引访问列。

### Cypher 子句映射

| Cypher 子句 | 逻辑算子 |
|-------------|----------|
| `MATCH (n)` | `AllNodeScanOp` |
| `MATCH (n:Label)` | `LabelScanOp` |
| `MATCH (a)-[r:TYPE]->(b)` | `ExpandOp`（子节点为 a 的 scan） |
| `WHERE pred` | `FilterOp` |
| `RETURN items` | `ProjectOp`（无聚合）/ `AggregateOp`（有聚合） |
| `RETURN ... ORDER BY` | `SortOp` |
| `RETURN ... SKIP N` | `SkipOp` |
| `RETURN ... LIMIT N` | `LimitOp` |
| `RETURN DISTINCT ...` | `DistinctOp` |
| `CREATE (n:Label)` | `CreateNodeOp` |
| `CREATE (a)-[r:TYPE]->(b)` | `CreateEdgeOp` |

---

## 二、物理计划

### 物理算子

物理算子与逻辑算子一一对应，增加执行所需上下文：

| 物理算子 | 持有的额外信息 |
|----------|---------------|
| `AllNodeScanPhysicalOp` | `IAsyncGraphDataStore&`, label 定义 |
| `LabelScanPhysicalOp` | `IAsyncGraphDataStore&`, `LabelId`, label 定义 |
| `IndexScanPhysicalOp` | `IAsyncGraphDataStore&`, `LabelId`, `prop_id`, `ScanMode` |
| `ExpandPhysicalOp` | `IAsyncGraphDataStore&`, `optional<vector<EdgeLabelId>>`, Schema |
| `FilterPhysicalOp` | `ExpressionEvaluator`, label 定义 |
| `ProjectPhysicalOp` | `ExpressionEvaluator`, label 定义 |
| `AggregatePhysicalOp` | `ExpressionEvaluator`, group_keys, aggregates |
| `SortPhysicalOp` | `ExpressionEvaluator`, sort_items |
| `SkipPhysicalOp` | `int64_t skip` |
| `DistinctPhysicalOp` | — |
| `LimitPhysicalOp` | `int64_t limit` |
| `CreateNodePhysicalOp` | `IAsyncGraphDataStore&`, `VertexId`, label_props |
| `CreateEdgePhysicalOp` | `IAsyncGraphDataStore&`, `EdgeId`, src/dst |

关键转换：
- 字符串 label/rel_type → `LabelId`/`EdgeLabelId`
- 物理算子持有 `IAsyncGraphDataStore&` 引用
- `CreateNodeOp`/`CreateEdgeOp` 在此阶段预分配 VertexId/EdgeId

### 各算子执行逻辑

**Scan**：通过 `AsyncGenerator<vector<VertexId>>` 异步扫描，逐批加载 `VertexValue`（属性+标签）。

**IndexScan**：由物理优化阶段从 `Filter(LabelScan)` 推导产生，支持等值（`ScanMode::EQUALITY`）和范围（`ScanMode::RANGE`）扫描。

**Expand**：嵌套循环——对输入的每一行，调用 `scanEdges(src_id, dir, label_filter)`，每条边产生一行输出。多类型时对每个类型分别调用 scanEdges 合并结果。关系类型不存在时返回 0 行。

**Filter/Project**：纯计算算子，在 Compute 线程上执行表达式求值。

**Sort**：阻断算子——全量物化子算子输出到内存，`std::sort` 排序后分批 yield。支持多键和 ASC/DESC。

**Aggregate**：阻断算子——按 group key 哈希分组累积聚合值，支持 `count`/`sum`/`avg`/`min`/`max`，含 `DISTINCT` 变体。全局聚合无输入时输出一行（count=0）。

**Distinct**：流式——`unordered_set<Row, RowHash, RowEqual>` 去重。

**Skip/Limit**：流式——计数后跳过/截断。

**CreateNode/CreateEdge**：通过 `co_await store_.insertVertex/insertEdge` 异步写入。`CreateNodePhysicalOp` 额外维护索引条目（遍历 `label_defs` 中 `WRITE_ONLY` 和 `PUBLIC` 状态的索引写入）。

### PhysicalPlanner

`PlanContext` 携带：
- `label_name_to_id` / `edge_label_name_to_id`：名称→ID 映射
- `label_defs` / `edge_label_defs`：LabelId→LabelDef 映射
- `variable_vertex_ids` / `variable_edge_ids`：CREATE 变量到预分配 ID
- `next_vertex_id` / `next_edge_id`：ID 分配计数器

**Schema 跟踪**：每个算子的 `planOperator` 返回 `output_schema`。Scan 建立初始 schema，Expand 扩展，Filter/Limit/Sort/Skip/Distinct 透传，Project/Aggregate 覆盖。

**索引扫描优化**：当 Filter 的子节点是 LabelScan 且谓词匹配可索引模式时，`tryIndexScan` 将 Filter+LabelScan 替换为 `IndexScanPhysicalOp`。

---

## 三、运行时类型

### Value

```cpp
using Value = variant<monostate, bool, int64_t, double, string,
                      VertexValue, EdgeValue, ListValue>;
```

`VertexValue`：id + labels + properties。`EdgeValue`：id + src + dst + label + properties。

### Row / Schema / RowBatch

```cpp
using Row = vector<Value>;       // 位置式，列索引对应 Schema
using Schema = vector<string>;   // 列名列表
struct RowBatch { static constexpr size_t CAPACITY = 1024; vector<Row> rows; };
```

---

## 四、表达式求值

`ExpressionEvaluator` 对 AST 表达式运行时求值。`label_defs_` 用于属性求值（属性名→prop_id→`VertexValue::properties` 按索引取值）。

| 表达式类型 | 说明 |
|-----------|------|
| `Literal` | 字面量 |
| `Variable` | 按 schema 列名查找 |
| `BinaryOp` | 比较（=, <>, <, >, <=, >=）、算术（+, -, *, /）、逻辑（AND, OR） |
| `UnaryOp` | NOT, 取负, IS NULL, IS NOT NULL |
| `PropertyAccess` | 属性访问 + 特殊属性 `id` 返回 int64_t |
| `FunctionCall` | `id(node)` 返回顶点/边 ID |

多个表达式类型仅解析未求值，详见 [cypher-syntax.md](cypher-syntax.md)。

---

## 五、QueryExecutor

### 编排流程

```
1. IndexDdlParser::tryParse() → 若为索引 DDL，handleIndexDdl，short-circuit
2. 检测 EXPLAIN 前缀
3. parse: 字符串 → AST
4. buildLogicalPlan: AST → LogicalPlan
5. 加载元数据: listLabels() + listEdgeLabels() + nextVertexId/EdgeId
6. beginTran + setTransaction
7. plan: LogicalPlan → PhysicalOperator
8. 若 EXPLAIN: formatExplainPlan，rollback，返回
9. execute: 全量消费 AsyncGenerator<RowBatch> → vector<Row>
10. commitTran
```

### 流式执行

`prepareStream()` 执行步骤 1-7 后，将 generator + 事务打包为 `shared_ptr<StreamContext>` 返回，由 handler 层流式消费并提交事务。

详细见 [execution-model.md](execution-model.md) 和 [transaction-model.md](transaction-model.md)。

---

## 六、当前实现状态

### 已完成
- Cypher Parser + AST
- 逻辑计划构建（12 种算子）
- 物理计划 + 协程执行基础设施
- 读算子：Scan / Expand / Filter / Project / Limit / Sort / Skip / Distinct / Aggregate
- 聚合：count/sum/avg/min/max + GROUP BY + DISTINCT
- 写算子：CreateNode / CreateEdge（含索引维护）
- EXPLAIN 语句
- IndexScan 优化（Filter+LabelScan → IndexScan）
- 流式执行（StreamContext + Thrift ServerStream）
- 索引 DDL（CREATE/DROP/SHOW INDEX，同步回填）

### 待实现
- SET, DELETE, MERGE, REMOVE 执行
- 多 MATCH + JOIN
- 查询优化器（谓词下推、投影裁剪）
- WITH 子句
- UNWIND + 列表操作
- DDL 异步执行（DdlWorker 后台线程）
- 崩溃恢复（索引 DDL 状态恢复）
