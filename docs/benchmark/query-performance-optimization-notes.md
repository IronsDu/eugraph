# 查询性能排查与优化笔记（complex-3 复盘）

> 目的：把 complex-3 从 ~490 ms 降到 ~136 ms 的排查过程固化成**可复用的方法**，
> 并记录已经验证/否证的优化方向，避免后续重复踩坑。
>
> 结论先行：本轮差距的大头不是「计划没选对」，而是**逐行重复物化/空查询**和
> **属性/标签存储布局**。计划层面 neo4j 与 eugraph 的 join 顺序、date 谓词位置基本一致。
>
> **存储状态**：本文中的 `vprop` / `label_reverse` 是 2026-10-24 存储改造前的口径。
> §5.1 的「labels + 常用属性收进同一条 vertex 记录」已经落地为 `vertex_data`（见
> [kv-encoding.md](../storage/kv-encoding.md)）；保留原文是为了记录当时的推理与实测，
> 不要按旧表名理解当前实现。

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

2026-10-25 实测：把 `scanEdgesBatch` 改为每 4,096 个 source 复用一个 cursor 后，
c5 从 279.7 ms 降到 243.9 ms（约 -13%）；但仍然省不掉每源 `search_near`，
剩余瓶颈仍是每源的 B-tree 定位。2026-10-26 的 caller-owned page/去拷贝没有稳定收益，
不要继续做无测量的微调。

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

- **不要**继续做无测量的 cursor/vector 微调：共享 cursor 的一次性收益约 -13%，
  之后 caller-owned page、去 key/value 拷贝的 A/B 在噪声/轻微回退范围；瓶颈是每源 search 与计划形状。
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

## 八、2026-10-29 当前状态（临时记录，供继续优化）

> 远端 58（10.87.56.58）在最后一轮 Expand 优化后 SSH 22 端口变为
> `Connection refused`（ping 正常），因此 **Expand typed-buffer 改动尚未部署到 58**；
> 58 上运行的仍是上一版 canonical-slot Release。下面本地数据是把 58 的
> `eugraph-data` 拷到本地 idle 机器测得，不代表 58 当前负载下的绝对值。

### 8.1 测量口径

- 数据：58 的 LDBC sf0.1 `eugraph-data`（327,588 顶点 / 1,477,965 边）；
- 本地 Release，4 compute / 4 storage IO / 4 thrift IO / 1 bolt IO；
- `scripts/bench_ldbc_interactive.py`，warmup 2 + 3 轮取 min；
- old/new 用同一份数据、同一参数，c5/c9/c14 参数为
  `person1Id=933, person2Id=1129, messageId=3`；
- 21 条读查询结果用 Python driver 取回后做 sha256 对比，old/new 逐条一致。

### 8.2 临时结果

| 查询 | 最初 | canonical 修复后 | + Expand typed buffer |
|---|---:|---:|---:|
| complex-14 | 138 ms | 117–125 ms | **114.5 ms** |
| complex-5 | 276 ms | 225–247 ms | **229.6 ms** |
| complex-9 | 184 ms | 159–173 ms | **160.6 ms** |

合计约 **13–17%**，距离 neo4j 仍远（c14 ~6.4×、c5 ~2.5×、c9 ~5.5×）。
远端 58 负载高（load 10–14），同一二进制在 58 上可到 c14 400ms+、c5 900ms+，
只能看相对趋势。

### 8.3 已落地改动

1. `aac518a5`：`EUGRAPH_OP_TRACE=1` 算子级 rows/chunks/self/inclusive CPU+wall；
   `executeChunkTraced` 默认关闭时直接返回原 generator。
2. `04c501dd`：`buildDeferredPlans` / `dispatchProjectionExtract` 按 canonical slot
   匹配 deferred set；修复别名链上 PE 列仍在 LIMIT 前物化的问题。
3. `30c1fdca`：Expand 每个 chunk 预解析 `row_src_ids`，`VertexRef` / `EdgeKey`
   直接写输出 typed buffer，去掉逐边 `Value` + `Column::setValue`。

### 8.4 已尝试但回退

- **Filter 边界延迟物化**：c5/c9 慢约 10%，3 个 list/pattern comprehension 测试回归；
  回退。
- **coalesce 属性合并读取**：同一 source 的多 spec 合并为一次 `batchGetVertexData`
  或新增 keyed coalesce API，c5/c9/c14 均变慢（读了过多无关属性）；回退，接口改动未保留。

### 8.5 当前瓶颈与下一步

- **c14**：两个 PC 共享的 `a <- HAS_CREATOR - comment` 前缀只占约 25% 中间边，
  单独做 PC 共享前缀不足以解释 6.5×；优先级低于每边 Expand 成本。
- **c5**：89k post/creator 先展开再过滤；需要 friends 集合下推、反向邻接或 semi-join。
- **c9**：53k message 仍要读 id + creationDate；需要更合适的覆盖访问路径。
- **Expand 下一步**：目前仍是 `scanEdgesBatch -> vector<BatchedEdge> -> 输出列`；
  可继续做“IO 线程直接填 typed 列”或按 `(src,dir,label)` 的邻接局部性/缓存。
  这是 c5/c9/c14 的共同项，优先于 PC 融合。

### 8.6 复现

```bash
# 本地用 58 的数据副本
rsync -a root@10.87.56.58:/root/eugraph/eugraph-data/ /tmp/eugraph-data-remote/
cmake --build --preset release --target eugraph-server -j2
./build/release/eugraph-server --data-dir /tmp/eugraph-data-remote \
  --bolt-port 17688 --compute-threads 4 --storage-io-threads 4 \
  --thrift-io-threads 4 --bolt-io-threads 1

python3 scripts/bench_ldbc_interactive.py \
  --queries-dir /tmp/ldbc_impls/cypher/queries --uri bolt://127.0.0.1:17688 \
  --warmup 2 --iters 3 --only complex-14,complex-9,complex-5 \
  --override person1Id=933,person2Id=1129,messageId=3
```

### 8.7 58 恢复后的远端复测（2026-10-29 10:36）

SSH 恢复后在 58 上部署了 Expand typed-buffer Release，并与 58 上的
`eugraph-server.bak.canonical` 做了同机顺序 A/B（warmup 3 + 5 轮，4/4 配置）：

| 查询 | canonical | + Expand | 变化 |
|---|---:|---:|---:|
| complex-14 | 412.0 ms | 398.7 ms | -3% |
| complex-5 | 895.0 ms | 815.9 ms | -9% |
| complex-9 | 579.1 ms | 584.8 ms | ~0（噪声） |

58 的负载在测量期间从 0.8 漂到 3.8，绝对值明显高于本地 idle（c14 ~115ms、
c5 ~230ms、c9 ~161ms），远端 A/B 只能确认 Expand 对 c5 有稳定收益，c14/c9 被噪声掩盖。
当前 58 上运行的是 Expand 版本，端口 7688。

### 8.9 已回退：Message.creationDate 索引 join 不计入公平对比

2026-10-29 曾临时给 eugraph 建 `Message.creationDate` 索引并放宽
`tryPlanFilterDestinationIndexJoin` 对显式 `(message:Message)` 的限制，本地 c9
从 160.6ms 降到 71.4ms。但 **neo4j 基线没有该索引**，这属于给 eugraph 单方面增加
访问路径，不能作为同 schema 的对比结论。

已处理：

- 回退 planner 改动（`d0cccef6`、记录 `3667da3f` 均已 revert）；
- 删除 58 与本地测试数据上的 `idx_message_creationDate`；
- 58 公平 baseline c9 复测约 **594ms**（1GB cache、无该索引）。

后续若要评估日期索引，必须同时给 neo4j 建等价索引并重新 A/B，或单独作为
“eugraph 可选索引收益”记录，不得混入 LDBC 公平对比表。

### 8.10 已回退：PC 前缀 Expand cache

为验证 c14 共享前缀的实际收益，曾按 `(src, direction, label)` 缓存 PC 右子计划中
Expand 的原始邻接结果，并把缓存放在语句级 `QueryContext`（上限 100k edge），由
`PatternComprehensionApply` 启用。

本地结果：c14 114.5→112.9ms（约 1%），c5/c9 无稳定变化。原因是共享的
`a <- HAS_CREATOR - comment` 只占总中间边约四分之一，而缓存查找/存储又把收益吃掉。
复杂度与收益不匹配，已回退，未提交。

### 8.11 继续线索（2026-10-29）

**分支/提交**

- 分支：`perf/c14-pc-fusion`
- 保留提交：`aac518a5`（op trace）、`04c501dd`（canonical deferred）、
  `30c1fdca`（Expand typed buffer）
- 索引 join 与其文档已 revert：`acc1c652` / `4484b6d5`，公平性说明 `9caea9ea`
- PC 前缀 Expand cache 试算未提交实现，只记录 `75efa1b8`

**公平 baseline（本地 idle，58 数据副本，4/4，warmup 2 + 3 轮）**

| 查询 | 最初 | 当前 |
|---|---:|---:|
| c14 | 138 ms | 114.5 ms |
| c5 | 276 ms | 229.6 ms |
| c9 | 184 ms | 160.6 ms |

- 21 条 LDBC 读查询结果哈希与最初逐条一致；
- 未跑全量 ctest；只跑 Expand/Path/Edge/MultiLabel/Order 等定向测试（233 个）。

**58 状态**

- 当前运行无 `Message.creationDate` 索引的公平 Release；
- 端口 7688，`--wt-cache-size-mb=1024`；
- 公平 c9 复测约 594 ms；远端负载高，c14/c5 绝对值不可直接引用。

**可复现数据/脚本**

- 58 数据副本：`/tmp/eugraph-data-remote`；
- LDBC queries：`/tmp/ldbc_impls/cypher/queries`；
- 单引擎 benchmark：`scripts/bench_ldbc_interactive.py`；
- 21 条结果哈希脚本可参考 `/tmp/hash_all.py`（临时脚本，未入库）。

**下一步优先做 c5，不要先做 c14 PC 融合**

- c14 PC 前缀共享上限约 25%，实测 cache 只约 1%，不足以弥补 6× 差距；
- c5 是 89k post/creator 行先展开再过滤，需要：
  1. friends 集合下推；
  2. 反向邻接（从 174 个 friend 反向 `HAS_CREATOR`）；
  3. 或 vertex-list semi-join；
- 任何新访问路径都必须与 neo4j 同 schema 对比，**不得单方面加索引**。

### 8.12 2026-10-29 第二轮：Filter 边界物化 + 实体引用不物化

延续 §8.11 的 c5 优先。先用 op-trace + IO trace + neo4j PROFILE 复测，发现两处
指标/执行层面的问题，再落地两个通用机制。

**指标可信化（先修工具）**

`[op-trace]` 的 TLS 指针在 `co_yield` 时没有还给调用方：一个算子先 pull 左孩子、
再 pull 右孩子时，右孩子的 parent 被记成左孩子。c5 因此把 `LeftJoin` 打印成
`self_cpu=170.6ms`，而 `incl=235.7` 减去左 64.3、右 167.0 只剩 **4.4ms**——
右子计划被错误缩进到左孩子下面。另有一处 `incl_cpu_ms=18446744072266`，是跨线程
恢复后 `CLOCK_THREAD_CPUTIME_ID` 差值为负造成的 uint64 下溢。

修复：yield 前恢复 `tls_current_op_trace`、resume 后重新安装；CPU 只在同一线程
计时，负差值丢弃。新增 `OperatorTraceTest.SiblingChildrenAttachToTheirOwnParent`
（修复前失败）并保留。

**机制 1：Filter 边界延迟物化（`physical_planner.cpp`）**

`computeFilterDeferredSlots` 从 PE 输出 slot 中剔除谓词引用、子树引用、外层已延迟的
slot；子树规划时置 `ctx.deferred_slots`，Filter 包装后用普通 PE
（`materializeDeferred(..., as_late_materialize=false)`）把列追加回来，**再让父算子
（Sort/Join）按新 layout 编译**。这是与 2026-10-25 失败尝试的关键区别（那次在 Sort
下方放 LateMaterialize，但 Sort 仍按旧 layout 编译，sort key 是占位值）。

同时补了两处 join 健壮性：

- `collectOpSlots` 收集 `BoundSemiJoinOp` / `BoundLeftJoinOp` /
  `BoundPatternComprehensionApplyOp` 的 correlation slot，边界不得延迟它们；
- join 解析 correlation slot 前 `ensureSlotLayout`：延迟 PE 可能让 Expand /
  VarLenExpand 这类规划期不发布 layout 的孩子保持空 layout。c10 因此曾报
  `SemiJoin: left slot 2 not found in left slot layout (size 0)`，已修复并用
  `AntiJoinAboveVarLenExpandResolvesCorrelationSlot` 回归。

**机制 2：实体引用不物化（`row.hpp` + `column_rewrite.cpp`）**

- `valueEquals` 把 `VertexRef`/`VertexValuePtr`、`EdgeKey`/`EdgeValuePtr` 折叠到 id；
- `rewriteRefCompatiblePredicates` 把 `x IN list` 的 x 降到 source `VERTEX_REF`/`EDGE_KEY`；
- `rewriteInternalEdgeUniqueness` 把 `__edge_unique` 参数降到 source `EdgeKey`。

c5 的 `friend IN friends` 因此不再要求 Filter 前构造 friend；`__edge_unique` 也不再
要求构造两条匿名边，PE 在 89,415 行上直接消失（Filter 前只剩 Expand + Filter）。

**探针与结果**

单查询探针（同一二进制、warmup 3 + 5 轮 min，4/4，cache 1024）：

| 探针 | 结果 |
|---|---:|
| c9 带 `ORDER BY message.id` | 181.8 ms |
| c9 去掉 `message.id`（只作探针） | **105.3 ms**（id 预取约 76.5 ms） |
| c5 右子计划 PE（Filter 前 89,415 行） | 114.0 + 33.8 ms |
| c5 只跑左支（VLE + HAS_MEMBER） | 56.4 ms |

21 条 LDBC 读查询同机交错 A/B（2026-10-29 16:28，base = HEAD，opt = 本改动；
构建目录 `build-release`（Release，`-O3 -DNDEBUG`），base→opt→opt→base 各 2 轮取 min，
4/4，无 `Message.creationDate` 索引）：

| 查询 | base | opt | 变化 |
|---|---:|---:|---:|
| c5 | 240.1 | 179.8 | -25.1% |
| c9 | 181.5 | 120.7 | -33.5% |
| c14 | 118.2 | 111.5 | -5.7% |
| c10 | 34.6 | 31.3 | -9.5% |
| c3 | 104.9 | 67.8 | -35.4% |
| c12 | 293.8 | 253.0 | -13.9% |
| short-4 | 1048.7 | 302.1 | -71.2% |

其余查询无 >10% 回退（c13 0.8→0.9 ms、short-5/6 +1%，均为噪声）；21 条结果哈希与
base 逐条一致，c5/c9/c14 与 neo4j 逐行一致（id 的 int/string 表示除外）。
完整 21 条 eugraph/neo4j 表见 [ldbc-snb-sf0.1-comparison.md](ldbc-snb-sf0.1-comparison.md)
§2.2（CsvComposite 副本、8/4）；本节 base/opt 是 4/4、vdata 副本上的优化 A/B。

**剩余瓶颈**

- c5 179.8 ms（同轮 neo4j 85.5）：Filter 前已是 Expand(CONTAINER_OF) +
  Expand(HAS_CREATOR)（约 126 ms CPU）+ Filter，per-edge 成本主导。下一步是
  `dst IN list` 下推进 Expand（减少 89,415 行的输出/后置过滤）或从 174 个 friend
  反向展开再与 forum 集合半连接（join reorder）。
- c9 120.7 ms（neo4j 27.9）：53k 条 HAS_CREATOR 边 + creationDate coalesce + 53k→7.7k
  过滤仍占大头；id 预取已消除。
- c14 111.5 ms（neo4j 14.6）：两路 PC 各约 57 ms，filter 引用 a/b/r，可延迟列有限；
  剩余是 per-edge Expand/PE，需单独拆解。

**收口（同日 15:05）**

- 含 PatternComprehensionApply 的语句整体关闭 Filter 边界（`ctx.defer_disabled_by_pc`），
  PC 的 right 子计划内也关闭（`correlated_subplan_depth`）。原因：PC 的 list/unwind/pattern
  管线会消费“没有任何 slot 引用命名”的构造表示（如 `collect(post)` 里的已构造 post），
  把构造移到 Filter 之上会改变 PC 看到的值——`ListComprehensionPatternPredicate*` 两个
  回退即此形态。
- LeftJoin / SemiJoin 的 right 子计划只清空外层 deferred 集合，**不**关闭其内部 Filter
  边界（c5 的收益正来自 LeftJoin right 内部的 `friend IN friends` Filter 边界）。
- 同日 idle 单查询（4/4，warmup 3 + 5 轮 min，CPU 较高频时）：c5 **157.7 ms**、
  c9 **109.6 ms**、c14 **105.4 ms**、c10 **29.3 ms**；21 条结果哈希与 base 一致。
- 2026-10-29 16:28 重新做全量 A/B（CPU 2.6–4.3 GHz，base→opt→opt→base）：
  c5 240.1→**179.8**、c9 181.5→**120.7**、c14 118.2→**111.5**、c10 34.6→**31.3** ms；
  21 条哈希与 base 逐条一致。完整表见 comparison §2.2。
- 计划形状测试 `PropertyExtractPlanTest.EdgePropertyExtract` 已按“Filter 上方多一个
  PE”更新；此前失败的两个 list-comprehension 测试恢复通过。
- 按开发者要求未跑全量 TCK；本地 ctest 去掉 `tck_tests` 后全部通过
  （`bolt_python_driver_integration_tests` 首次因本机基准 server 占用 19090 失败，
  释放端口后单测通过，不是代码问题）。
