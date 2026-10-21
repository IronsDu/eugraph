# 查询性能排查与优化笔记（complex-3 复盘）

> 目的：把 complex-3 从 ~490 ms 降到 ~136 ms 的排查过程固化成**可复用的方法**，
> 并记录已经验证/否证的优化方向，避免后续重复踩坑。
>
> 结论先行：本轮差距的大头不是「计划没选对」，而是**逐行重复物化/空查询**和
> **属性/标签存储布局**。计划层面 neo4j 与 eugraph 的 join 顺序、date 谓词位置基本一致。

## 一、测量纪律（先定判据，再改代码）

1. **同机、同二进制、交错 A/B**：不要跨时间比较绝对值。本机 CPU 频率会漂移，
   同一二进制可差 ~2×；性能结论必须用 baseline/optimized 交替跑，至少 3 轮，
   记录 min 与 p50。
2. **每轮先 warmup**（3 次），再计时 10 次；客户端长连接复用，避免把建连成本算进去。
3. **一次只验证一个改动**：大改动前做单变量隔离；合并效果不能归因到其中某一个改动。
4. **判据要能区分对错**：性能改动也要有可证伪的预期（例如「去掉 52k 次空查应下降
   ~100 ms；若不变则假设错误」），不能只看“跑完了”。
5. **行数/结果先对齐**：complex-3 在 eugraph 与 neo4j 都是 0 行；0 行查询也可能很慢，
   不能因为结果为空就认为计划走了捷径。

## 二、C++ 协程的采样方法（perf 不友好时的兜底）

`perf -g` 对 folly 协程栈经常拿不到调用链（DWARF/CFI 与协程帧不匹配）。可用 GDB 原生
采样：

```bash
# 1) 起一个持续跑目标查询的客户端（单连接、出错重连）
python3 /tmp/c3_loop.py 120

# 2) 对 server attach，采样 N 次
SPID=$(pgrep -x eugraph-server)
for i in $(seq 1 60); do
  gdb -p $SPID -batch -ex "thread apply all bt 60" -ex "detach" \
    >> /tmp/c3_samples.txt 2>/dev/null
  sleep 0.2
done
```

分析要点：

- **必须 `bt 60` 以上**：物理算子在协程帧里很深，`bt 20` 常常只看到 `resume`/调度器。
- **排除阻塞叶帧**：`futex`、`pthread_cond_wait`、`epoll`、`__wt_cond_wait` 等是空闲
  线程；只统计真正在跑的线程。
- **区分 compute 与 IO 线程**：compute 线程栈里是算子；IO 线程栈里是
  `IoScheduler::dispatch → SyncGraphDataStore::...`。IO 线程看不到算子的协程调用者，
  所以要结合执行计划推断“哪个算子发起了这类存储调用”。
- **采样只用于定位候选热点**，最终结论用 A/B 计时 + 单变量改动验证。

> 相关：`docs/benchmark/query-value-copy-profiling.md` 记录了更早一轮用同一方法定位
> complex-10 值拷贝问题的过程。

## 三、complex-3 案例

### 3.1 环境

- 数据：LDBC SNB sf0.1（327,588 顶点 / 1,477,965 边），`personId=933`；
- eugraph release：compute=8、storage-io=4、WT cache 256MB、`--wt-txn-sync fsync`；
- neo4j 5.26 本机 7687，eugraph Bolt 7688；
- 查询：`interactive-complex-3.cypher`（Angola / Colombia，2010-06-01 ~ 2010-06-29）。

### 3.2 计划对照：两边其实做了同样的事

neo4j `PROFILE`：

```
Expand(All) (friend)<-[:HAS_CREATOR]-(message)                 52,475 rows
  Filter (cache[message.creationDate] < end AND >= start)      787 rows, DB Hits 52,475
    Expand(All) (message)-[:IS_LOCATED_IN]->(country)          787 rows, DB Hits 5,319
      Filter country IN [countryX, countryY]                   18 rows
        EagerAggregation → Top/Sort → CacheProperties → ProduceResults
```

eugraph 计划（`EUGRAPH_PLAN_DEBUG=1`）的形状：

```
Limit → Project → Sort → ProjectionExtract(friend) → Aggregate(keys=1)
  → Filter(country IN ...)
    → Expand(message→country)                    # 只对 date 过滤后的 787 行展开
      → Filter(message.creationDate < end)
      → Filter(message.creationDate >= start)
        → ProjectionExtract(friend/country/message props/edge)
          → Expand(friend←HAS_CREATOR)          # 52,475 行
            → Distinct → ... → VarLenExpand(person→friend) → Label/IndexScan
```

两边一致的关键点：

- **都是 friend → message → date 过滤 → country 展开**；date 谓词都在 country 展开之前；
- **neo4j 也为 52,475 条 message 读了 creationDate**（Filter 的 DB Hits = 52,475），
  没有 creationDate 索引，也没有把 date 下推到 HAS_CREATOR 之前；
- 所以“neo4j 不需要读 creationDate”是错的；差距在**每次读的代价**与**物化时机**。

### 3.3 根因链（按贡献）

1. **`ProjectionExtract` 逐行深拷贝 `VertexValue`**
   `WITH DISTINCT friend, countryX, countryY` 后三者在每一行都是完整 VERTEX；
   52,475 条 message 展开时，同一批 friend/country 被反复深拷贝
   `unordered_map<LabelId, Properties>`。
2. **batch API 对重复 vid 逐个 `search_near`**
   一 batch 1024 行里同一个 friend/country vid 可能重复上千次；
   `getVertexLabelsBatch` / `getVertexPropertiesBatch` / `...Projected` 仍逐元素查 WT。
3. **无条件 anon 属性查询**
   每个 `ConstructVertex` 行都查一次 `__anon__` 属性，即使顶点没有 anon 标签。
4. **无属性边逐行查 eprop**
   `ConstructEdge` 对每行都 `getEdgeProperties`，而 `HAS_CREATOR` / `IS_LOCATED_IN`
   根本没有声明属性——52k 条 message 产生 52k 次空 WT 查询（约 130 ms）。
5. **剩余：labels + creationDate 的两次 WT 查询**
   每条 message 先查 `label_reverse` 判断 Post/Comment，再查 `vprop_{label}` 的
   creationDate；52k × 2 次 B-tree search ≈ 80–90 ms。neo4j 的 label 内联在 node
   record、属性记录/指针直读，所以同一逻辑只需一次近似 O(1) 的读。

### 3.4 已落地改动

| 改动 | 文件 | 效果 |
|---|---|---:|
| batch API 按唯一 vid 查询后按原顺序展开 | `storage/data/sync_graph_data_store.cpp` | 490→410 ms |
| ConstructVertex 共享 `VertexValuePtr`；同 vid 只物化一次；anon 按标签门控 | `query/physical_plan/operator/projection_extract_physical_op.cpp` | 410→270 ms |
| `ConstructEdge` 按 schema 跳过无属性边的 eprop 空查 | `projection_extract_physical_op.*`、`physical_planner.cpp` | 270→136 ms |

对应的提交：`1a20bbde`、`8efcf0ea`（分支 `perf/complex-3-vertex-sharing`）。

### 3.5 最终 A/B（三轮交错，min / p50）

| 指标 | main | 优化后 | 提升 |
|---|---:|---:|---:|
| complex-3 | 489.8 / 497.0 ms | **136.0 / 141.9 ms** | **3.60× / 3.50×** |
| message 展开子查询 | 199.4 / 206.0 ms | **31.1 / 32.7 ms** | **6.4× / 6.3×** |

neo4j 同查询 27–44 ms（取决于机器负载）；优化后差距从 ~17–21× 收窄到 **3–5×**。

### 3.6 阶段对照

| 阶段 | neo4j | eugraph（优化后） |
|---|---:|---:|
| 52k HAS_CREATOR 展开 | 11.5 ms | ~31–41 ms |
| + creationDate 过滤 | +15.6 ms | +~96 ms |
| 完整 complex-3 | 27–44 ms | 136 ms |

eugraph 的 creationDate 过滤包含两次 WT 查询/条（labels + property）；neo4j 的
52,475 次 DB Hits 是节点内属性读，所以同为“读 52k 次”，代价差 5–6×。

## 四、可复用的检查清单

排查“行数不多但很慢”的查询时，按顺序检查：

1. **算子里是否逐行做存储 I/O？** 特别是 `ProjectionExtract` 的 Construct*、Load* 分支。
2. **batch API 是否处理了重复 id？** 一个已绑定顶点被 Expand 复制到整批行是常态；
   重复 vid 不应重复 `search_near`。
3. **是否在查“空”的东西？**
   - 顶点没有 `__anon__` 标签却查 anon 属性；
   - 边类型没有声明属性却查 eprop；
   - 没有候选标签却查属性。
   用 schema 元数据先裁剪，而不是先查再判空。
4. **是否逐行深拷贝大对象（`VertexValue`/`EdgeValue`/`Properties`/`PathValue`）？**
   只读物化）优先共享 `shared_ptr`；需要修改时写时复制（COW）。
5. **是否每行都经过不必要的算子/投影？** 看计划里 `ProjectionExtract` 出现的位置，
   尽量晚物化完整实体。
6. **存储调用次数 × 单次成本**：用 GDB 采样确认是不是 WT `search_near`/`__wt_row_search`
   占主导；如果是，优化方向是“减少调用次数/改存储布局”，而不是算子里加循环。

## 五、后续优化思路（按预期收益/风险排序）

> 下面都是**未实施**的设计级方向；涉及 KV 编码/存储格式，必须先做兼容性设计评审。

### 5.1 把 labels + 常用属性收进同一条 vertex 记录（收益最大）

现状：`label_reverse`（vid→labels）和 `vprop_{label}`（vid+prop→value）是两张表，
读一个属性要两次 B-tree search。若引入一条 vertex record：

```
key:   vid
value: {label_bitmap, prop_map}   // 或 label_bitmap + 固定槽位常用属性
```

则 `message.creationDate` 一次读完成，52k 次读从 ~96 ms 降到 ~40–50 ms（甚至更低）。
代价：KV 编码/写入路径/DDL/迁移全部要改，是外部兼容性表面，需独立设计文档与评审；
大属性集是否全塞进一条记录也需要取舍（可只放“热点/固定 schema”属性）。

### 5.2 按源连续的邻接存储（降低 Expand 的每源 B-tree search）

现状：全局 `edge_index` 按 `{vid}{dir}{label}{neighbor}` 排序；一次 Expand 的每个
source 都要一次 `search_near`，52k source ≈ 30–40 ms。

可选：
- 每个顶点一条 adjacency row/table（源内连续，顺序扫描）；
- 或给 `edge_index` 增加按源的覆盖索引/聚簇。

`scanEdgesBatch` 用一把 cursor 只省 open/close，省不掉每源 `search_near`——本轮实测
收益 <5%，已回退，不要重复投入。

### 5.3 高选择性谓词的索引/CBO（creationDate）

`Message.creationDate` 在 28 天范围内的选择率是 787/52,475（~1.5%）。若存在二级索引，
可以从 787 条 message 出发再 join friend/country，避免 52k 次属性读。

但注意：
- neo4j 当前计划**没有**走 creationDate 索引，join 顺序与 eugraph 相同，所以这不是
  “对齐 neo4j”的必需项；
- 需要 CBO 能做 join 重排/索引选择。手工把 country 放前面的改写实测会退化成 35s+ 的
  灾难计划（CrossProduct），不要用简单 pattern 重排替代 CBO；
- 索引本身是额外写入成本，要按 workload 评估。

### 5.4 `LoadVertexProp` / `LoadVertexLabels` 批处理（已落地）

`ConstructVertex` / `LoadVertexPropCoalesce` 之外，`LoadVertexProp` / `LoadVertexLabels`
原先仍是逐行 `co_await`（point read）。本轮已改为按 chunk 收集唯一 vid、一次 batch 调用
（`batchGetVertexPropertiesProjected` / `getVertexLabelsBatch`），再按行回填（重复 vid
只占一个结果槽，避免 52k 行 × 2 属性 = 10 万次 async 点读）。

效果（八条 complex 查询复测，详见 [§11](ldbc-snb-sf0.1-comparison.md#11-八条-complex-查询复测2026-10-21)）：
complex-12 396→337 ms、complex-10 34→30 ms、complex-2 15.7→13.7 ms、complex-4/7/8 也各有下降。

**仍可继续的合并**：coalesce 路径每个 chunk 会发多次 async dispatch（labels 一次、每个候选
label 的属性一次），可合并为一个 `getVertexPropCoalesce(vids, candidates)` 的 sync 方法，
在**一次 IO dispatch** 内完成 labels + property 查询；预期收益主要是协程/调度开销
（~10–20 ms 量级），WT search 次数不变，属于低风险后续项。

### 5.5 不要做的事

- **不要**为了“省 cursor”继续改 `scanEdgesBatch`：瓶颈是每源 search，不是 open/close。
- **不要**用“从 country 反向展开”的手写计划替代 CBO：已实测灾难性退化。
- **不要**把 `WITH DISTINCT` 的完整 VERTEX 深拷贝换成“每行仍拷贝、只是少查一次”；
  要么共享句柄，要么推迟物化。
- **不要**在未确认 value 是否会被原地修改前共享 `VertexValuePtr`；本轮只对只读路径共享，
  需要清标签/补 anon 时写时复制。

## 六、复现与脚本

```bash
# eugraph release server（与 A/B 相同参数）
./build-release/eugraph-server --thrift-port 9090 --bolt-port 7688 \
  --data-dir /home/dodo/code/fuck/eugraph-ldbc-sf01-bench \
  --compute-threads 8 --storage-io-threads 4 --thrift-io-threads 4 \
  --bolt-io-threads 1 --wt-cache-size-mb 256 --wt-evict-threads-max 4 \
  --wt-txn-sync fsync

# 计划/谓词
EUGRAPH_PLAN_DEBUG=1 EUGRAPH_PRED_DEBUG=1 ./build-release/eugraph-server ...

# neo4j 计划
# cypher-shell -a bolt://127.0.0.1:7687 ... "PROFILE MATCH ..."
```

基准脚本：`scripts/bench_ldbc_ab.py`、`scripts/bench_bolt_qps.py`；
详细数据见 [ldbc-snb-sf0.1-comparison.md](ldbc-snb-sf0.1-comparison.md) §10。

## 七、相关文档

- [LDBC SNB SF0.1 对比测试](ldbc-snb-sf0.1-comparison.md) §10：complex-3 的 A/B 数据、neo4j PROFILE 对照；
- [查询值拷贝性能剖析](query-value-copy-profiling.md)：complex-10 的逐层归因与 GDB 采样方法；
- [ProjectionExtract 设计](../query/engine/projection-extract-design.md) §3.4：本轮的批处理/共享句柄不变量；
- [存储接口](../storage/interfaces.md)：batch API 的去重语义与失败约定。
