# 表达式求值 Prefetch 设计

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

## 1. 问题

拓扑阶段（Scan / Expand / VarLenExpand / ShortestPath）产出的 `VertexValue` 可以只有
`id`，属性需要按需从存储补齐；`startNode(edge)` / `endNode(edge)` 还会在表达式求值
过程中新造一个裸顶点。旧实现把“补齐”放在 `ExpressionEvaluator::evalPropertyRef` /
`evalDynamicPropertyRef` 内部，用 `folly::coro::blockingWait` 现场读存储：

```cpp
auto labels  = folly::coro::blockingWait(store->getVertexLabels(vid));
auto props   = folly::coro::blockingWait(store->getVertexProperties(vid, lid));
auto ldef    = folly::coro::blockingWait(meta->getLabelDefById(lid));
```

这带来三个问题：

1. **计算线程被阻塞**：表达式求值发生算子的 `executeChunk()` 里，`blockingWait`
   把算子线程按住等 IO，违背 IO/Compute 分离；
2. **阻塞点在求值器内部**：`IoScheduler` 真正跨线程后，`blockingWait` 的 continuation
   会停在 Storage IO 线程，算子线程与存储线程的归属变得不可预测；
3. **N+1**：每个裸顶点、每个 label、每次动态属性访问都可能各发一次 IO；`__anon__`
   旁路还会额外逐点查询。

## 2. 设计：算子边界的两阶段

### 2.1 prefetch（异步，唯一 IO 点）

`ExpressionEvaluator::prefetch(exprs, chunk)` 是协程，在 `evaluate()` 之前运行：

1. **扫描 chunk**：遍历所有列的值（递归进入 `ListValue` / `MapValue` / `PathValue`），
   收集 `properties.empty()` 的 `VertexValue` id，并收集出现过的 label / edge-label id；
2. **批量加载**：一次 `IAsyncGraphDataStore::batchGetVertexData(ids)`；每个请求 id 都写入
   缓存（存储没有返回的 id 也写入空条目，用于区分“已加载但为空”和“未 prefetch”）；
3. **标签定义**：对缓存数据中不在 `EvalContext.label_defs` / `Catalog` / 本地缓存里的
   label，异步 `getLabelDefById`；edge label 同理；
4. **dry pass（合成实体）**：对包含 `startNode` / `endNode` 的表达式，先同步求值一次，
   求值器把“缓存中不存在的裸顶点 id”记录到 `prefetch_pending_ids_`（而不是发 IO）；
   批量加载这些 id 后重试，最多 3 轮。这样 `MATCH ()-[r:KNOWS]->() RETURN
   startNode(r).name` 这种端点不在 chunk 列里的查询也能覆盖；
5. **`__anon__`**：若表达式含动态属性访问，异步解析一次 `__anon__` label id 并缓存。

### 2.2 evaluate（同步，零 IO）

`evaluate` / `evaluatePredicate` 保持同步 API（算子调用点不需要全部改成协程），但
`evalPropertyRef` / `evalDynamicPropertyRef` 只读：

- 裸顶点：查 `prefetched_vertex_data_`；未命中则 `missingPrefetchCount()++` 并返回
  NULL，**绝不回退到 blocking IO**；
- label/edge-label：只查 `EvalContext.label_defs` / `Catalog` / prefetch 填入的缓存；
- 动态属性：`propIdFor` 只查上述缓存；`__anon__` 只读 prefetch 解析好的 id。

`ExpressionEvaluator::missingPrefetchCount()` 是诊断/测试钩子。测试在跑完相关查询后断言
它为 0；非 0 表示某个 operator 漏调 `prefetch`，会在结果里表现为 NULL（而不是偷偷阻塞）。

### 2.3 调用点

每个“按 chunk 求值”的算子在 `evaluate` 前调用一次 `prefetch`：

| 算子 | 预取表达式 |
|------|-----------|
| Filter | predicate |
| Project | 所有 item.expr |
| Aggregate | group keys + 所有 aggregate arguments |
| Sort | 所有 sort key |
| Unwind / Foreach | list_expr |
| Set | 所有 value expr |
| CreateNode | label_prop_exprs_ + resolved_pending_ |
| CreateEdge | prop_exprs_ + resolved_pending |
| Delete | 所有 target.expr |
| Merge | start/end/edge prop filters + pending props + ON CREATE/ON MATCH value expr；SET item 在 merged_chunk 上单独 prefetch |

`Limit` / `Skip` 的变量无关表达式在空 chunk 上求值，不涉及图数据，不调用 prefetch。
`PathElementPropertyRead` 仍走自己的 `batchGetVertexData` 批量加载路径，不经求值器。

## 3. 缓存语义

- 缓存按 `ExpressionEvaluator` 实例生命周期**累加**：同一个算子的多个 chunk、以及
  Merge 对合成 chunk 的 prefetch 不会丢掉输入 chunk 的条目；
- 存储返回的 id 全部落缓存（含空条目），因此“顶点存在但没有属性”不会被误判为
  “未加载”；
- 写算子只在写入前读取输入值；跨算子的新数据由下游算子自己的 evaluator 重新 prefetch。
  如果未来出现“同一 operator 内写后复读同一裸顶点”的语义，必须在该算子内清空/刷新
  缓存（当前测试/TCK 未覆盖到这种场景）。

## 4. 验证

- `QueryExecutorTest.ShortestPathEndpointPropertiesTriggerPrefetch`
- `QueryExecutorTest.ShortestPathEndpointDynamicPropertyTriggersPrefetch`
- `QueryExecutorTest.StartNodePropertyWithoutEndpointColumnPrefetches`
  （端点不在 chunk 列里，专门覆盖 dry pass）
- 上述测试均断言 `ExpressionEvaluator::missingPrefetchCount() == 0`
- 全量 `query_executor_tests`（603 用例）、`evaluator_typed_tests`、RPC 集成、
  后续 TCK 回归

## 5. 相关文件

- `src/query/evaluator/expression_evaluator.hpp`
- `src/query/evaluator/expr/eval_prefetch.cpp`
- `src/query/evaluator/expr/eval_property.cpp`
- `src/query/physical_plan/operator/*.cpp`（prefetch 调用点）
- `src/query/planner/bound_expression/bound_expression_walk.hpp`（共享的表达式 child 遍历）
