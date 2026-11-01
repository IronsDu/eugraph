# PROFILE 设计（neo4j 语法 + EuGraph 专有指标）

> 状态：**Phase 1 已实现**（Bolt + Web Console + Thrift/shell）。
> 目标：让 `PROFILE <cypher>` 像 neo4j 一样执行查询并返回结果摘要，同时在 plan 节点上
> 暴露 EuGraph 自己的调度/存储/预取指标，直接用于定位热点算子。

## 1. 语义

### 1.1 语法

```cypher
PROFILE MATCH (n:Person {id: $id}) RETURN n.name
```

- `PROFILE` 与 `EXPLAIN` 互斥；同时出现是语法错误。
- `PROFILE` **执行**查询，返回正常结果行；profile 是**结果摘要**，不是额外结果列。
- `PROFILE` 的写查询会像普通查询一样真实写入；只想看计划请用 `EXPLAIN`。
- `EXPLAIN` 保持现状：不执行，返回格式化 plan。
- `PROFILE` 重复前缀/`EXPLAIN PROFILE`/`PROFILE EXPLAIN` 均报语法错误。

### 1.2 事务语义

- auto-commit 语句：执行完成后正常 commit；profile 在 commit 之后 finalize，
  因此可在摘要里包含 `commitMs`。
- 显式事务（Bolt `BEGIN…COMMIT`）：每条 `PROFILE` 语句只统计本语句的
  planning/execution；事务 commit 不属于单条语句，不写入该语句的 `commitMs`。
- 语句失败：不返回 profile（走现有错误路径），不能把半成品计划当成功摘要。
- `PROFILE` + 流式结果：只有结果被完整消费（drain）后才产生 profile；
  `PULL` 分页时在最后一个 `SUCCESS` 里返回；`DISCARD`/取消不返回 profile。

### 1.3 与 DDL 的关系

当前 `EXPLAIN` 只支持 regularQuery；数据库 DDL 在 `GraphService::executeCypher` 的
DDL interception 里先于 parser 处理。第一期：

- `PROFILE` + regularQuery：支持；
- `PROFILE` + standalone `CALL`：先明确报错（与 `EXPLAIN` 当前行为一致）；
- `PROFILE` + 数据库 DDL / index DDL：明确报错，不执行 DDL。

后续若要支持 `PROFILE CALL`，复用同一 ProfileCollector，但需要单独定义 procedure 的
plan 节点语义。

## 1.4 Phase 1 实现结果

已落地：

- `PROFILE` 语法/AST/执行模式（`ProfileStatement`，与 `ExplainStatement` 互斥；后续可合并为统一前缀节点）；
- 算子级 profile 树（attach plan 时注册，包含 0 行/未执行分支）；
- 指标：rows/chunks、dbHits、wtSearch/wtSearchNear、ioDispatches、
  prefetchHits/prefetchMisses、self/inclusive wall/CPU、planning/execution/commit；
- Bolt 最终 `PULL` 的 `SUCCESS` 返回 `profile` + 顶层 `eugraph` 摘要，官方 Python/JS
  driver 实测可读；
- Web Console：`PROFILE`/`EXPLAIN` 智能提示、`:profile <cypher>` 命令、Profile 结果页；
- Thrift/shell：`ResultRowBatch.profile` 可选字段 + 最后一个空 batch 携带摘要，shell 打印
  算子级 profile；
- 保持 `EXPLAIN` 行为不变；PROFILE 关闭时零采集开销。

Phase 2（可选增强）：

- Web 的火焰图/排序交互（当前是 Profile 表格 + 热点高亮）；
- `PROFILE CALL` / DDL / procedure 级 profile。

## 2. 数据模型

### 2.1 Profile 节点

```text
QueryProfile
  ├─ planning_ms / execution_ms / commit_ms
  ├─ rows / chunks
  ├─ aggregates: db_hits, io_dispatches, wt_search, wt_search_near,
  │              prefetch_hits, prefetch_misses
  └─ roots: ProfileNode[]

ProfileNode
  ├─ operator_type          // Filter / Expand / Projection / ...
  ├─ details                // 现有 toString() 的参数部分
  ├─ identifiers[]          // 输出列名（现有 outputSchema）
  ├─ rows / chunks
  ├─ db_hits                // WT cursor search + search_near
  ├─ wt_search / wt_search_near
  ├─ io_dispatches           // 该算子发起的 Storage IO dispatch 次数
  ├─ prefetch_hits / prefetch_misses
  ├─ self_time_ms / inclusive_time_ms
  ├─ self_cpu_ms / inclusive_cpu_ms
  └─ children[]
```

### 2.2 各指标定义

| 指标 | 来源 | 语义 |
|---|---|---|
| `rows` | `DataChunk::count` | 该算子输出行数，不含子算子 |
| `chunks` | `AsyncGenerator::next()` 次数 | 输出批次数 |
| `db_hits` | `recordWtSearch` + `recordWtSearchNear` | WT 游标 seek 次数；**不等价于 neo4j 的 page cache dbHits** |
| `wt_search` / `wt_search_near` | `StorageOpCounters` | 分开暴露，便于定位点查/范围查 |
| `io_dispatches` | `IoScheduler::dispatch` | 该算子发起的跨池调度次数，直接对应调度开销 |
| `prefetch_hits` | evaluator prefetch 命中 | 求值时命中预取缓存、无需补 IO 的次数 |
| `prefetch_misses` | `missingPrefetchCount` 的细化 | 预取未覆盖、求值读不到数据的次数（当前会产出 NULL） |
| `self_time_ms` | `steady_clock` 分段 | 扣除直接子算子 inclusive 后的时间 |
| `inclusive_time_ms` | `steady_clock` 分段 | 该算子自己的执行区间，包含子算子 |
| `self_cpu_ms` / `inclusive_cpu_ms` | `CLOCK_THREAD_CPUTIME_ID` | 线程 CPU 时间；跨线程恢复时只统计同线程区间（沿用现有逻辑） |

**归属规则（关键）**：`io_dispatches` / `wt_search` 归属到**发起该存储调用的算子**，
而不是它所在的父算子。实现上使用现有的
`tls_storage_op_counters`（见 `src/storage/storage_op_metrics.hpp`）：
算子执行时把自己的 `StorageOpCounters` 设为 thread-local，存储层的 `dispatch` 捕获
这个指针并在 IO 线程安装 `StorageOpMetricsScope`。这样异步完成后计数仍落到正确节点，
不需要按时间差猜。

`prefetch_*` 同样走这个 counters 对象：evaluator 通过 thread-local 指针递增，
不再依赖全局 `missingPrefetchCount()` 做近似。

### 2.3 汇总规则

- `QueryProfile.aggregates` = 各节点 `io_dispatches`/`wt_search`/... 之和，加上
  不归属算子的 planning/commit 存储操作（单独字段）。
- 父节点的 `inclusive_time` 包含子节点；`self = inclusive - Σ(child inclusive)`
  （负值截零，防御异常）。
- `db_hits` 只统计 profile 开启期间的 WT seek；profile 关闭时零开销。
- 暂不伪造 neo4j 的 `pageCacheHits/Misses`：EuGraph 没有按算子暴露 WT page cache
  统计。输出中省略该字段，或在 `eugraph.pageCacheStatsAvailable=false` 中明确说明。

## 3. 执行与采集

### 3.1 语法 / AST

改动：

- `grammar/CypherLexer.g4`：新增 `PROFILE : 'PROFILE';`
- `grammar/CypherParser.g4`：
  ```antlr
  query
      : (EXPLAIN | PROFILE)? regularQuery
      | standaloneCall
      ;
  ```
  两个前缀同时出现由 parser 语法直接拒绝。
- `src/query/parser/ast.hpp`：当前实现为 `ProfileStatement`（与 `ExplainStatement`
  并列）；后续可合并为统一的 `PrefixedStatement { Mode mode; ... }`。
- `src/query/parser/cypher_parser.cpp`：读取前缀，生成 mode。
- `scripts/gen-grammar.sh` 重新生成并提交 generated parser。

### 3.2 QueryContext / plan 节点注册

现有 `OpTraceRecord` 只在算子第一次 `executeChunkTraced()` 时懒创建，父节点靠
thread-local 执行栈推断。PROFILE 需要**完整 plan 树**（包括没执行的分支/0 行节点），
因此改为在 attached plan 时注册：

- `PhysicalOperator::setQueryContext()` 增加 parent 参数（内部重载）：
  ```
  root->setQueryContext(ctx)
    -> 若 ctx->profiling(): node = ctx->addProfileNode(toString(), parent)
    -> 保存 operator->profile_node
    -> 递归 children，parent=node
  ```
- 执行时 `executeChunkTracedImpl()` 复用已注册节点，不再懒创建；TLS
  `tls_current_op_trace` 仍用于嵌套执行/可选日志，但父子关系以 plan 为准。
- `QueryContext` 构造时接收 `bool profile_requested`；`EUGRAPH_OP_TRACE` 仍可通过
  env 打开同一套 trace，但 PROFILE 不依赖 env。
- `StreamContext` 增加：
  - `bool profile_requested`
  - `std::optional<ProfileResult> profile`
  - `finalizeProfile()`（在 stream drain/commit 后调用）
  - planning 开始/结束时间戳。

### 3.3 计数归集

`StorageOpCounters` 扩展：

```cpp
struct StorageOpCounters {
    std::atomic<uint64_t> wt_search{0};
    std::atomic<uint64_t> wt_search_near{0};
    std::atomic<uint64_t> io_dispatches{0};
    std::atomic<uint64_t> prefetch_hits{0};
    std::atomic<uint64_t> prefetch_misses{0};
};
```

- `PhysicalOperator` 持有一个 `shared_ptr<StorageOpCounters>`（随 profile 节点创建）；
- `executeChunkTracedImpl()` 在调用 `executeChunk()`/`next()` 期间安装
  `StorageOpMetricsScope(&node.counters)`，退出/让出时恢复父节点 counters；
- `AsyncGraphDataStore::dispatch()`：
  - profile 开启时，捕获当前 TLS counters；
  - `io_dispatches.fetch_add(1)`（发起线程）；
  - IO lambda 里安装同一个 counters scope，WT seek 计数落到该节点；
  - 关闭 profile 时保持现有零开销路径。
- evaluator 的 prefetch 计数通过同一 TLS counters 递增；
  删除/保留 `missingPrefetchCount()` 作为兼容诊断，但 profile 使用 counters。

`AsyncGraphDataStore` 不再只靠 `EUGRAPH_IO_TRACE` 决定是否包 scope；新增
`setProfileCounters()` 或构造参数，避免全局 env 影响正常查询。

### 3.4 时间

- `planning_ms`：从 `prepareStream` 入口（parse 前）到 physical plan 完成；
- `execution_ms`：从第一次 `gen.next()` 到 generator 结束；
- `commit_ms`：auto-commit 的 `commitAsync()` 前后；
- 节点 `self/inclusive` 沿用 `OpTraceRecord` 的计时方式，在 finalize 时算差；
- 计时使用 `steady_clock` / 线程 CPU，不依赖 wall clock 频率。

### 3.5 关闭时零开销

- 没有 `PROFILE`：`executeChunkTraced()` 直接返回 `executeChunk()`（沿用现有逻辑），
  不创建 profile 节点，不安装 counters，不计算 self/inclusive；
- `EUGRAPH_OP_TRACE` 仍可用于日志，但与 PROFILE 摘要解耦。

## 3.6 如何用这份 profile 定位性能问题

1. **先看语句级 planning/execution/commit**：区分规划、执行、提交各占多少；execution
   高再看算子。
2. **按 inclusive time 找子树，按 self time 找具体算子**：inclusive 大但 self 小说明
   时间在子算子；self 大才是该算子自身开销。
3. **看 `rows`/`chunks` 判断放大**：上游 rows 很小、下游 rows 很大，说明展开/连接放大；
   两者与实际结果对比可发现过滤下推/裁剪是否生效。
4. **看 `dbHits`/`wtSearch`/`wtSearchNear`**：确认成本是否在存储访问；点查多还是范围扫多。
5. **看 `ioDispatches`**：高 dbHits 且高 dispatch，说明跨池调度本身可能是瓶颈，优先
   batch/coalesce；低 dbHits 但高 dispatch，说明小 IO 太多。
6. **看 `prefetchMisses`**：非 0 表示求值阶段仍有未预取数据（当前会产出 NULL）；应回到
   算子 prefetch 覆盖。
7. Web Console 的 Profile 页会把 self time 最大的算子高亮，先看高亮行，再向下钻。

## 4. 传输与展示

### 4.1 Bolt（首要目标）

`PROFILE` 在最终 `PULL` 的 `SUCCESS` metadata 里增加：

```json
{
  "profile": {
    "operatorType": "ProduceResults@eugraph",
    "args": {
      "Details": "...",
      "DbHits": 0,
      "Rows": 1,
      "eugraph.ioDispatches": 0,
      "eugraph.selfTimeMs": 0.12
    },
    "identifiers": ["n.name"],
    "dbHits": 0,
    "rows": 1,
    "children": [ ... ],
    "eugraph": {
      "chunks": 1,
      "selfTimeMs": 0.12,
      "inclusiveTimeMs": 0.12,
      "selfCpuMs": 0.08,
      "ioDispatches": 0,
      "wtSearch": 0,
      "wtSearchNear": 0,
      "prefetchHits": 1,
      "prefetchMisses": 0
    }
  },
  "eugraph": {
    "planningMs": 0.6,
    "executionMs": 1.2,
    "commitMs": 0.1,
    "rows": 1,
    "chunks": 1,
    "dbHits": 1,
    "ioDispatches": 2,
    "wtSearch": 1,
    "wtSearchNear": 1,
    "prefetchHits": 1,
    "prefetchMisses": 0
  }
}
```

- 标准字段对齐 neo4j driver 的 `profile` 读取路径：
  `operatorType` / `args` / `identifiers` / `dbHits` / `rows` / `children` / `time`；
- EuGraph 扩展同时写进每节点 `args.eugraph`（嵌套 map）与节点顶层 `eugraph` map：
  Python driver 保留原始 metadata；JS driver 的 `ProfiledPlan` 只保留
  `args`/标准字段，因此 Web Console 从 `arguments.eugraph.*` 读取；cypher-shell 的
  `Other` 列也显示这个嵌套 map，便于阅读；
- 不伪造 `pageCacheHits/Misses`；若后续能从 WT 拿到，再按 neo4j 字段补；
- `time` 使用整数毫秒（Java `ProfiledPlan.time()` 为 `long`）；精确时间在
  `eugraph.selfTimeMs/inclusiveTimeMs`；
- `PULL {n}` 未取完时 `has_more=true`，不带 profile；最后一次 `SUCCESS` 才带；
- `PROFILE` 的 `RUN` 仍返回普通 `fields`/`t_first`，不提前给未完成的 profile。

**验收**：用官方 Python/Java/JS driver 各跑一次 `PROFILE RETURN 1`，确认
`summary.profile` 可读、未知 `eugraph` 字段不导致解析失败。

### 4.2 Thrift / shell

Thrift 流没有“结果摘要”消息，且 `QueryStreamMeta` 在结果之前发送。方案：

- `ResultRowBatch` 已增加 `2: optional QueryProfile profile`；服务器在 commit 后产生
  一个 **rows 为空的最终 batch** 携带 profile；
- 旧客户端忽略未知 optional 字段；loader/TCK 的 0 行 batch 不影响结果；
- shell 在 drain 后打印：

```text
Profile: 1 row, 1 chunk
  planning 0.6 ms, execution 1.2 ms, commit 0.1 ms
  dbHits 1, ioDispatches 2, wtSearch 1, wtSearchNear 1, prefetch 1/0
  +- Projection  rows=1 chunks=1 self=0.1ms dbHits=0 io=0
     +- Filter    rows=1 chunks=1 self=0.2ms dbHits=1 io=1
```

- `EXPLAIN` 现有输出保持不变；`PROFILE` 不把 profile 当结果列。

### 4.3 Web Console

Web Console 直接用官方 neo4j driver，所以只要 Bolt 的 `profile` 摘要正确，
前端可以通过 `summary.profile` 渲染；第一期可先不画图，只在结果摘要区展示
profile JSON/tree，后续再做可视化。

### 4.4 HTTP / 其他客户端

本期不新增 HTTP 查询 API；Web 继续走 Bolt。若将来需要 HTTP，复用同一 `QueryProfile`
序列化。

## 5. 与现有 EXPLAIN/op trace 的关系

- `EXPLAIN`：静态 plan，不执行；当前文本输出不变；
- `EUGRAPH_OP_TRACE`：日志诊断，继续保留；PROFILE 复用其计时/树结构，但由语句显式开启；
- `EUGRAPH_IO_TRACE`：日志诊断；PROFILE 不依赖 env，使用 programmatic counters；
- `StreamContext` 析构时的 op-trace 日志保留，但 PROFILE 摘要应在 drain/commit 时
  finalize，不依赖析构日志。

## 6. 实现分期

### Phase 1（核心闭环）

1. grammar/AST/parser + generated files；
2. `QueryContext` profile 节点注册、`StorageOpCounters` 扩展、dispatch 归属；
3. `StreamContext` profile finalize（planning/execution/commit、rows/chunks）；
4. Bolt final `SUCCESS.profile` + Python driver 集成测试；
5. Web Console 的 Profile 结果页与 `:profile` 命令、`PROFILE` 智能提示；
6. Thrift `ResultRowBatch.profile` + shell 文本树；
7. 单元测试、Bolt/Thrift driver 集成测试、TCK 回归、无 profile 路径开销检查。

### Phase 2（指标完善）

- prefetch hits/misses 的精细分类（顶点/边/属性）；
- WT page cache 统计（若可拿）；
- `CALL`/procedure profile；
- Web Console profile 可视化；
- 多语句/UNION 的根节点分组展示。

### Phase 3（可选）

- `PROFILE` 的 JSON/CSV 导出；
- 采样/低开销模式（只统计行数，不统计时间）；
- 与现有 `EXPLAIN` 文本统一成 plan/profile 两种 renderer。

## 7. 测试计划

| 层级 | 用例 |
|---|---|
| Parser | `PROFILE MATCH...`、`EXPLAIN MATCH...`、`PROFILE EXPLAIN` 报错 |
| Query | profile 结果行与非 profile 一致；树节点 rows/chunks 与结果一致；写查询真实生效 |
| Metrics | 构造有 IO 的查询，`ioDispatches`/`wtSearch`/`wtSearchNear` 非零；关闭 profile 时 counters 不变 |
| Prefetch | 稀疏/稠密属性访问分别产生 hits/misses；数值与 `missingPrefetchCount` 对齐 |
| Bolt | Python/Java/JS driver：`summary.profile` 可读；`PROFILE` 结果行正确；`PULL` 分页最后才带 profile；`EXPLAIN` 不执行 |
| Thrift/shell | 最后一个 batch 携带 profile；shell 表格后打印 tree；loader/TCK 忽略空 batch（已覆盖 RPC 测试 + shell 手动验证） |
| 回归 | 全量 unit/ctest；TCK 3907/3907；无 `EUGRAPH_*` env 时性能不退化 |
| 性能 | 在 c5/c9/c12 上比较 normal vs PROFILE 的 p50；记录 profiling 开销 |

## 8. 风险与待确认

1. **Bolt 兼容性**：`profile` map 的标准字段必须被三个官方 driver 接受；`eugraph`
   扩展字段是否被 driver 保留/忽略需要在 Phase 1 实测。若不兼容，降级为
   `args.eugraph` 或只放在 `eugraph` 根字段。
2. **Thrift 兼容性**：给 `ResultRowBatch` 加 optional 字段要重新生成 Thrift；旧客户端
   看到最终空 batch 是否安全需要 loader/TCK/shell 全量验证。
3. **指标归属精度**：跨线程 storage 调用通过 TLS counters 归属到发起算子；
   异步操作如果由父算子发起、子算子等待，语义按“发起者”统计，需在文档写清。
4. **`dbHits` 语义**：EuGraph 的 WT seek 次数不能与 neo4j page-cache dbHits 直接横比；
   文档/字段名要明确。
5. **`PROFILE` 写查询**：会真实写入；是否需要在 shell 提示/确认？neo4j 语义是执行。
6. **DDL/CALL 范围**：第一期是否只支持 regularQuery？建议是；需要明确错误信息。
7. **时间字段**：neo4j driver 的 profile 不保证有 `time`；我们把时间放在
   `eugraph.selfTimeMs/inclusiveTimeMs`，不强行塞进标准字段。
8. **是否保留 `EUGRAPH_OP_TRACE` 作为内部诊断**：建议保留，PROFILE 不依赖它。

## 9. 已采用的最小决策

1. 首期范围：regularQuery 的 `PROFILE` + Bolt + Web + Thrift/shell；CALL/DDL 后置。
2. Thrift `ResultRowBatch` 增加可选 profile 字段；旧客户端忽略该字段。
3. `dbHits = WT search + search_near`（文档明确标注不可与 neo4j page-cache dbHits 横比）。
4. `PROFILE` 写查询按 neo4j 语义真实执行并提交。
5. 扩展字段同时放 `args.eugraph`（嵌套 map）和节点顶层 `eugraph` map：兼容 JS driver，也保留
   Python 原始 metadata。
