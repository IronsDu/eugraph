# PatternComprehension 已绑定终点折进 Expand

> **状态**：**已被既有实现覆盖（2026-11-20 实测）**。见 §0。正文 §1–§4 保留为当时的分析与假设，已被实测否证。
> **日期**：2026-11-19 起草，2026-11-20 实测更新。
> **范围**：binder（`bindExistsSubPlan`）+ 复用现成的 `dst_bound` 快路径；不改 KV 编码、不改并发/事务模型、不新增公共接口。
> **动机**：`complex-10` 内层 pattern comprehension 的第二跳把已绑定的终点当成未知终点，枚举全部邻居后再过滤。

---

## 0. 实测结论（2026-11-20）

本文 §2–§4 的核心判断「只有 binder 没把已绑定终点交回去」**不成立**：折进已经在
**物理计划层**实现，且对 c10 的形状生效。

- 实现位置：`src/query/physical_plan/physical_planner.cpp` 的 `rewriteDstBoundExpandFilters`
  （commit `8c6bcc6 perf(compute): fold correlated endpoints and probe bound destinations`，2026-11-11，
  已在 `main`）。它在 `collectPlanRequirements` 之前把
  `Filter(id(__exists_dst_N) == id(__exists_saved_N))` 折成 `Expand(dst=__exists_saved_N)`，
  有向时再设 `probe_bound_dst`（集合探针），配套 `scanEdgesExactBatch` 批量精确 seek。
- 设计与实测记录： [engine/correlated-endpoint-set-probe.md](engine/correlated-endpoint-set-probe.md)
  （sf1 c10：490–508 ms → 277–300 ms，PC 子树 180 ms → 63 ms）。
- 回归测试：`tests/test_query_executor.cpp` 的 `ListComprehensionPatternPredicateAcrossWith`
  断言计划必须含 `dst=bound` 与 `probe=set`。

### 0.1 c10 内层形状的实测计划（本机 Ubuntu/WSL，release，`EXPLAIN`）

```cypher
EXPLAIN MATCH (person:Person {id: 933})-[:KNOWS*2..2]-(friend)
WITH person, collect(friend) AS friends
RETURN size([p IN friends WHERE (p)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(person)]);
```

关键两行（完整计划另存）：

```text
Expand(src=__anon_3, dst=__exists_saved_1, edge=__anon_edge_4, labels=[3], direction=IN), dst=bound, probe=set
  Expand(src=p, dst=__anon_3, edge=__anon_edge_2, labels=[2], direction=OUT)
```

- 计划中 `__exists_dst` 出现 **0** 次 → 重命名与事后等值过滤都已消失；
- `dst=bound` 1 次、`probe=set` 1 次；
- `ListComprehensionPatternPredicateAcrossWith` 在同一次构建上 **PASSED**（105 ms）。

### 0.2 仍存在的残余形状（未折进）

同一构建、同一 server，用 `EXPLAIN` 对比（`dst=bound` / `probe=set` / `__exists_dst` 计数）：

| 形状 | dst=bound | probe=set | `__exists_dst` |
|---|---:|---:|---:|
| A. c10 内层（PC，起点 `p` 相关） | 1 | 1 | 0 |
| D. `EXISTS { MATCH (p)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(person) }`（起点 `p` 局部） | 0 | 0 | 4 |

D 的右侧子计划里 `Expand(dst=__exists_dst_0, ...)` 之上叠了 3 个 `Filter`，折进没有发生，
因此该跳仍然「枚举 tag 的全部 `HAS_INTEREST` 入边再过滤」。

**跳过原因已定位（env 诊断，`EUGRAPH_PLAN_DEBUG=1`）**：

```text
[FOLD] filter skipped: child_is_expand=false pred_is_eq=true    ← D：EQ 过滤存在，但孩子不是 Expand
[FOLD] dst=__exists_dst_1 saved=__exists_saved_1 expr_refs=1 field_refs=1 provides=true fold=yes   ← A（c10）
```

即：不是 `safe_to_fold` / `subtreeProvidesCorrelatedVariable` 拦住的，而是**绑定期形状不满足
「`Filter` 直接位于 `Expand` 之上」**——`EXPLAIN` 打印的是优化后的物理计划，看起来贴着，
但 `rewriteDstBoundExpandFilters` 在 requirements 收集前看到的 bound 计划里，那个 EQ 过滤器的
孩子不是 `Expand`。折进只对 binder 恰好把等值过滤直接套在 Expand 上的路径（pattern comprehension）
生效，EXISTS 子查询这条路径不满足。

### 0.3 因此后续工作方向

1. 不再重复实现 binder 级折进（与既有物理层折进重复，且现有实现已有单测 + TCK 覆盖）；
2. 若继续这个方向，先给折进加**跳过原因计数器**（env 开关 + PROFILE 标记），量化 D 类形状在
   LDBC 21 条里的出现频度，再决定是否值得改 binder 或放宽守卫；
3. §1 的 18,321 次 / 2.6M 行 / 541 ms 是 2026-11-11 之前的数，不能再当作现状引用。

---

## 1. 问题

> 本节及 §4 描述的是 2026-11-11 之前的计划形状；当前 HEAD 已折进，见 §0。

官方 LDBC `complex-10` 的内层推导：

```cypher
size([p IN posts WHERE (p)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(person)])
```

这里 `p`（来自外层 `posts`）和 `person`（外层相关变量）**都是已绑定**的，未知的只有中间那个匿名 tag。

当前计划（`PROFILE` / `EXPLAIN` 形状）：

```text
Filter (__exists_dst_N == __exists_saved_N)          ← 事后用等值过滤挑出 person
  Expand(src=tag, dst=__exists_dst_N, IN, HAS_INTEREST)   ← 枚举该 tag 的所有感兴趣者
    Expand(src=p,  dst=tag, OUT, HAS_TAG)                 ← 每个 post 的 tag
      CorrelatedSource(p, person)
```

第二跳的语义本应是「判断边 `(tag, IN, HAS_INTEREST, person)` 是否存在」，实际执行成「把 tag 的入边全部读出来，再丢掉不等于 person 的行」。

仓库记录里的量级（[`66e9156`](../..) 提交说明与 `docs/benchmark/ldbc-storage-unchanged-optimization-plan.md`）：

- 第二跳展开 **18,321 次**；
- 中间结果约 **2.6M 行**；
- 最终只保留 **647 行**；
- `complex-10` 在 SF1 上约 **541 ms**。

> 以上数字来自仓库既有记录，本文未重新测量。

## 2. 代码链路

| 位置 | 事实 |
|---|---|
| `src/query/planner/binder/bind_match.cpp:1505-1560` | 链上节点若引用外层变量，**刻意**分配新名 `__exists_dst_N` 与 `__exists_saved_N`，并追加事后的等值过滤。注释给出的理由是：复用原名会让 `Expand` 的输出 schema 同时含 passthrough 与 dst 两份，`makeSlotLayout` 会把它们压到同一个 slot，破坏等值过滤 |
| `src/query/physical_plan/physical_planner.cpp:3897-3899` | `int dst_existing = findColumn(child_schema, v.dst_variable); bool dst_bound = dst_existing >= 0;` —— 终点是否“已绑定”**完全靠名字能否在 child schema 里找到** |
| `src/query/physical_plan/physical_planner.cpp:3914` 附近 | 只有 `!dst_bound` 时才向输出 schema 追加 dst 列 |
| `src/query/physical_plan/operator/expand_physical_op.cpp:1005` | `const bool append_dst = !dst_bound_ && !dst_var_.empty();` |
| `src/query/physical_plan/operator/expand_physical_op.cpp:558`、`:600` | `dst_bound_` 的精确 seek 分支（`{src}{dir}{label}{dst}` 前缀） |

结论：`Expand` 侧的快路径与“不追加重复列”的保护都已存在；**只有 binder 没把已绑定终点交回去**。

## 3. 为什么注释里的顾虑已不成立

那条注释写在 `dst_bound` 检测之前。现在 planner 在构造输出 schema **之前**就完成了 `dst_bound` 判定，并且只在 `!dst_bound` 时追加 dst 列，因此只要名字能被 `findColumn` 找到：

- 不会出现两列同名；
- 不会触发 slot 合并；
- 等值过滤也不需要了（终点就是输入列）。

也就是说，`__exists_dst_N` 这个重命名在绕开一个已经不存在的问题，代价是让快路径永远命中不了。

## 4. 改法

在 `bindExistsSubPlan` 构造链上 `Expand` 的位置加一个分支：

```cpp
// 该节点的变量来自外层：correlated source 的输出 schema 里已经有这一列
if (saved_ctx.symbols.count(chain_var) &&
    findColumn(correlated_source_schema, chain_var) >= 0) {
    expand->dst_variable     = chain_var;      // 复用已绑定列
    expand->dst_column_index = correlated_col;
    // 不生成 __exists_dst_* / __exists_saved_*
    // 不追加事后的等值 Filter
}
```

守卫（任一不满足即保留原路径，fail closed）：

1. 变量确实出现在 **correlated source 的输出 schema** 中，而不只是外层 `ctx_` 里有；
2. 该 `__exists_saved_*` 列**没有其他表达式引用**（嵌套 EXISTS、pattern predicate、`extra_corr_vars` 都可能引用）；
3. 目标节点上的标签/属性谓词仍要生效——`dst_bound` 路径下由 scan filter 承担，需要单独回归。

## 5. 验证判据

| 级别 | 内容 |
|---|---|
| 单测 | 新增 `PatternComprehensionBindsExistingDestination`：断言计划中**没有** `__exists_dst_*` 与对应等值 Filter，且结果逐行一致（旧代码必失败） |
| `PROFILE` | c10 第二跳 `Expand` 的 rows 从 2.6M 降到 `18,321 × (0\|1)` 量级，`wt_search` 同步下降 |
| 语义 | `scripts/verify_ldbc_results.py` 21/21；c10 的 `commonPostCount` 逐行一致 |
| TCK | `existentialSubqueries` + `pattern` 子集；`query_executor_tests` 全量 |
| 性能 | 同二进制交错 A/B（见 `AGENTS.md` §2.5），c10 与 c9/c6 一起看，确认收益不是搬运 |

## 6. 风险与回退

- `__exists_saved_*` 目前承担相关值在子计划内的引用身份，删列前必须确认没有其他消费者；嵌套 EXISTS 是重点回归对象。
- 第一版建议加环境变量开关（仓库既有 `EUGRAPH_*` 惯例），确认 A/B 后再默认开启。
- **不要**给 `BoundExpandOp` 新增 bool 字段来传递这个信息：那会进入 `AGENTS.md §5.3` 的跨层字段清单（clone / visitor / requirements），且这类标志编译期不报错、运行期改路径。复用列名判定即可。

## 7. 预期与不做的事

- 预期：中间结果从 10⁶ 降到 10³ 量级；`complex-10` 的 wall 时间取决于剩余算子，**必须 A/B 说话，不预设数字**。
- 这是 binder 级改动，所有「EXISTS / pattern comprehension 中终点是外层变量」的查询都受益，不是 c10 特判。
- 不做：不是把 PC 去相关成 join（那是另一个量级的工作，需要先给左行分配稳定身份）；也不改 `Expand` 的访问路径选择。

## 8. 复现

```bash
# 计划形状（无需数据）
EXPLAIN MATCH (person:Person {id: 933})-[:KNOWS*2..2]-(friend)
WITH person, collect(friend) AS friends
RETURN size([p IN friends WHERE (p)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(person)])

# 归纳指标
EUGRAPH_OP_TRACE=1 EUGRAPH_IO_TRACE=1 <运行 c10>
```

关注三个数：第二跳 `Expand` 的 `rows`、该算子的 `wt_search`/`wt_search_near`、以及 `ioDispatches`。
