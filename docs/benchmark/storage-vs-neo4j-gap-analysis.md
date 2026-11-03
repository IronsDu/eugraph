# 存储结构 vs Neo4j：LDBC SF0.1 性能差距简报（供外部评审）

> **用途**：交给外部模型（GPT）做独立评审，回答三个问题：
> ① 这套 KV 存储结构相对 neo4j 原生图存储，性能上限大概在什么范围；
> ② 当前 LDBC 差距里，多少来自存储结构，多少来自执行计划/算子/代码；
> ③ 下一步最值得投入的方向是什么。
>
> **使用方式**：把本文全文粘贴给 GPT 即可；§7 已列出建议问题。
>
> **时间**：2026-10-26。**分支**：`feat/storage-vertex-data` @ `adafc818`。
>
> **checkout 提示**：本文写作与基准来自 `feat/storage-vertex-data` 分支；`vertex_data` 的
> 实现与 `docs/storage/vertex-data-design.md` **不在 `main` 上**。若当前 checkout 是 `main`，
> 存储结构相关的代码/文档链接只对 feature 分支有效；§5.5 的 binder/planner 问题在 `main`
> 上同样可核对，但计划 dump 来自 feature 分支的运行实例。
>
> **标注约定**：本文区分「实测」（仓库文档中的计时/计数）、「代码事实」（当前源码可核对）、
> 「估算」（本文的推断，可能错，请优先质疑）。**本文没有重新跑基准**，所有耗时引自仓库记录；
> 绝对值受机器频率影响，跨轮次只应看同轮比值。
>
> **依据**：
> [LDBC SF0.1 对照](../benchmark/ldbc-snb-sf0.1-comparison.md)、
> [vertex_data 设计（feature 分支）](https://github.com/IronsDu/eugraph/blob/feat/storage-vertex-data/docs/storage/vertex-data-design.md)、
> [KV 编码](../storage/kv-encoding.md)、
> [c5/c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md)、
> [c14 锚点分析](../query/pattern-comprehension-anchor-label-scan.md)、
> [complex-3 复盘](../benchmark/query-performance-optimization-notes.md)。

---

## 0. 结论速览

### 0.1 三个直接回答

**一句话**：这套 KV 存储的合理包线是——点查 **0.6–1.0×**、多数 complex **1.3–2.5×**、
访问密集（c9/c14）**3.5–8×**；当前差距是「per-access 成本 × 访问次数 × 算子开销」的乘积，
不是单一存储布局问题。

1. **本分支的存储改造没有带来大幅提升，这是符合预期的。**
   它做的是「把 `label_reverse` + `vprop_{label}` 两张表并成一张 `vertex_data`，并把邻接缓存
   做成可选层」，**没有改变**「一条属性/一条边 = 一条 KV 记录、每次访问 = 一次 B-tree seek」
   这个访问范式。干净的同计划 A/B：c5 约 **-8%**（264 → 243.9 ms），c14 约 **-3.4%**
   （295 → 284.9 ms）；c9 的旧基线本身是错误计划的结果，**没有有效的存储-only 对比**。
2. **当前 2.8×/8×/21× 的差距，是「计划/算子」与「每次访问成本」叠加的结果，且分查询不同。**
   c5/c9 的**逻辑行数**与 neo4j 基本同量级（c5 两边都展开 89k posts；c9 两边都展开 53k 并读
   creationDate）；但 eugraph 的物理访问次数更高（label/property 是分开的 KV 行，neo4j 在
   node record 内一次读）。差距主要是**单次存储访问 1–2 µs vs neo4j 摊算 0.3–0.5 µs**；
   c14 在锚点修复后仍有 24 次 PC 展开，属访问密集 + 冗余前缀；最新计划对照还发现
   **`person1.id` / `person2.id` / `b.id` 都被解析成 14 个候选（所有带 `id` 的标签），
   端点走 `LabelScan(Person)` 而不是 `Person.id` 索引**（§5.5）——这是 binder/planner 的
   可修项，不属于存储布局。c3/c14 早期的巨大改善（3.6×、270×）则纯粹来自计划/算子，
   证明「大计划 bug」确实曾经是主因，但**这些 bug 已经修掉了**。
3. **这套存储的合理性能区间（估算）**：点查/short 已经 **0.6–1.0×**（比 neo4j 快）；complex
   当前中位数约 **2.7×**、几何均值约 **3.1×**；把计划/算子剩余项修完，多数查询可到
   **1.5–2.5×**；再把属性/邻接打包成「每顶点一条记录 + 邻接数组」并有足够 cache，
   c5 这类可到 **1.4–2×**（半连接若减少访问，有机会 ≤1×），但 c9 仍约 **3.5–5×**、
   c14 约 **4–8×**，除非同时减少展开次数/算子开销；**全负载稳定 1× 不现实**，除非改成
   内存图/原生邻接表示。

### 0.2 判断与证据强度

| # | 判断 | 证据强度 | 关键依据 |
|---|---|---|---|
| 1 | 存储改造本身约中性（-8% ~ +6%） | **强**（同计划 A/B） | c5 264→279.7（vertex_data+coalesce，+6%）→243.9（共享 cursor，净 -8%）；c14 295→284.9（-3.4%）；c9 旧 161.7 无效 |
| 2 | 当前大差距的主因不是「表放错了」，而是 per-access 成本 + 访问次数 | **中强** | c5/c9 与 neo4j 同访问量级；单次 edge/属性访问 1–2 µs（§5.2） |
| 3 | 计划/算子仍有剩余收益，且有具体可修项（非单个大 bug） | **中** | c5 批处理 514→264；c9 LIMIT LM 422→245；c14 锚点/探测 80.5 s→0.295 s；c14 14-candidate/索引失效（§5.5）；c3 490→136 |
| 4 | 只靠当前布局，访问密集查询地板约 3.5–8×（c14 不确定性更大） | **中**（估算） | c5/c9 的每访问成本 × 访问数 + 算子开销（§6.1） |
| 5 | 做属性/邻接打包后，非访问密集的多数查询可到 1.3–2×；c9/c14 仍 3.5–8×；全面 1× 不现实 | **弱–中**（估算） | 需要原型验证；见 §6.4、§6.6 |

**对「是不是计划和代码的问题」的直接回答**：是，但要分两层。历史上确实有能被单个修复放大
的计划/算子 bug（c14 270×、c3 3.6×、c5 1.95×、c9 1.7×），这些已经修掉；当前剩余差距主要是
**大量 per-access 的代码/布局细节**（每源 seek、逐行解码、属性行扫描、协程派发）和
**少量计划选择**（c14 的 24 次 PC 冗余、c14 的 14-candidate 弱属性/索引失效、c10 的
展开顺序、缺 CBO）。不是单个大 bug，但这些具体缺陷叠加起来足以吃掉数倍性能。
这也是为什么「把两张表并成一张」这种布局改动不会带来数量级提升。

---

## 1. 系统、数据与测量口径

### 1.1 系统

| 项 | 值 |
|---|---|
| eugraph | C++20 单机图数据库；自有 Cypher 解析/绑定/优化/物理计划；Bolt 服务端 |
| 存储引擎 | WiredTiger，多张 KV 表；表配置 `key_format=u,value_format=u`（变长 key/value） |
| 执行模型 | folly 协程 + 火山式拉取；`DataChunk` 默认 1,024 行；compute 8 线程 / storage-io 4 线程；存储调用经 `IoScheduler` 派发到 IO 线程池 |
| WT 配置（基准） | `--wt-cache-size-mb 256`、`--wt-txn-sync fsync`、evict threads 4 |
| 对照 | neo4j 5.26，同机，Bolt 7687；eugraph 7688 |
| 客户端 | 官方 Bolt driver；每条查询独立连接（不计建连）；同机交错 A/B，warmup 1 轮 + 3 轮取 min；c5/c9/c14 另有 5 轮专项复测 |

> 注意：eugraph 的 `id` 是整数，neo4j 是字符串；本轮对 neo4j 的 `*Id` 参数统一传字符串，
> 避免「类型不匹配 → 0 行 → 虚假加速」。

### 1.2 数据（LDBC SNB Interactive sf0.1）

- 顶点 **327,588**；边 **1,477,965**；Person **1,528**；Message **286,744**。
- 参数：complex/short `personId=933`、`messageId=3`；c13/c14 `person1Id=933, person2Id=1129`。
- 已知数据差异：当前 CsvBasic 导入**缺 Person 的 `email`/`speaks` 多值属性**，c1 的这两列不可比；
  c12 的 `collect(DISTINCT)` 列表顺序不保证；c3/c4 当前参数下两边都是 0 行。
- 索引前提：short-4..7 依赖 `Person/Message/Post/Comment/Forum(id)` 索引；本轮补建了
  `Message.id` 索引，neo4j 侧均 ONLINE。

### 1.3 读数的可信边界

1. **只比较同轮比值**。本机 CPU 频率会漂移，同一二进制跨轮可差约 2×；例如 c14 专项复测
   291.4 ms、全量同轮 351.1 ms，是不同轮次，不应直接比较。
2. **c9 的旧 161.7 ms 不是基线**：它来自 filter-boundary 延迟物化缺陷（Sort 比较了占位值，
   Top-20 不是最大日期），结果错误。正确基线是 422 ms（修复前）、187–232 ms（修复后，取决于轮次）。
3. **update 本轮无效**：8 条 `interactive-update-*` 是写语句，参数来自 LDBC update stream
   fixture，仓库内没有现成 fixture；空参数是 no-op。下表 update 数据沿用上一轮，**不参与本轮结论**。
4. **c1 有数据差异、c12 仅顺序差异、c3/c4 为 0 行**；这些行的耗时不代表完整结果。

---

## 2. 存储结构（本分支之后）

### 2.1 表清单

| WT 表 | 用途 | Key | Value |
|---|---|---|---|
| `vertex_data` | 顶点标签 + 属性（+ 可选邻接缓存） | `{vid:u64BE}{kind:u8}{...}` | 空 / ValueCodec / state / edge_id |
| `label_fwd_{labelId}` | 标签→顶点正向索引（每标签一表） | `{vid:u64BE}` | 空 |
| `vertex_existence` | 所有存活顶点（含无标签） | `{vid:u64BE}` | 空 |
| `edge_index` | 邻接索引（N=0 时全量；N>0 时仅 overflow） | `{vid}{dir:u8}{label:u16BE}{neighbor:u64BE}{seq:u64BE}` | `edge_id` |
| `etype_{labelId}` | 边类型索引（每边类型一表） | `{src}{dst}{seq}` | `edge_id` |
| `eprop_{labelId}` | 边属性（每边类型一表） | `{eid}{prop:u16BE}` | ValueCodec |
| `index_*` / 元数据 | 二级索引、schema、统计 | 见 [KV 编码](../storage/kv-encoding.md) | — |

**本分支删除**：`label_reverse`（vid→labels）、`vprop_{labelId}`（每标签顶点属性表）。
**保留**：`label_fwd`（按标签扫描、DROP LABEL 枚举）、`vertex_existence`、`edge_index`、
`etype/eprop`、二级索引。**不做历史数据迁移**，已有图需重新导入。

**变更摘要**（旧格式的完整说明见 §2.2）：

| 改造前 | 改造后 | 变化 |
|---|---|---|
| `label_reverse`：`{vid}{label}` | `vertex_data` kind `0x00`：`{vid}{0x00}{label}` | 标签并入 `vertex_data` |
| `vprop_{labelId}`：`{vid}{prop}`（每标签一表） | `vertex_data` kind `0x01`：`{vid}{0x01}{label}{prop}` | 属性并入 `vertex_data` |
| `edge_index` 全量 | `edge_index` overflow-only + `vertex_data` 邻接 | 默认 N=0，仍全量 |
| `label_fwd` / `vertex_existence` / `etype` / `eprop` | 不变 | — |

### 2.2 改造前的存储格式（旧）

旧布局（`main`，分支基点）没有 `vertex_data`，标签和属性分表存放：

| WT 表 | 用途 | Key | Value |
|---|---|---|---|
| `label_reverse` | 顶点→标签（全局表） | `{vid:u64BE}{label:u16BE}` | 空 |
| `label_fwd_{labelId}` | 标签→顶点（每标签一表） | `{vid:u64BE}` | 空 |
| `vprop_{labelId}` | 顶点属性（**每标签一表**） | `{vid:u64BE}{prop:u16BE}` | ValueCodec |
| `vertex_existence` | 所有顶点（含无标签） | `{vid:u64BE}` | 空 |
| `edge_index` | 邻接（全量，无 state） | `{vid}{dir}{label}{neighbor}{seq}` | `edge_id` |
| `etype_{labelId}` / `eprop_{labelId}` | 边类型/属性 | 同现在 | `edge_id` / ValueCodec |

**旧写路径**

- 插入顶点：`vertex_existence` 1 行 + 每标签 `label_reverse` 1 行 + `label_fwd` 1 行 +
  每属性 `vprop_{label}` 1 行。
- 删除顶点：先扫 `label_reverse` 得到标签；逐标签删 `label_reverse`/`label_fwd` 并扫
  `vprop_{label}` 前缀；最后删 `vertex_existence`。
- 边写入与现在相同（N=0 时 `etype` + `eprop` + 2 条 `edge_index`）。

**旧读路径**

- `getVertexLabels` / `getVertexLabelsBatch`：`label_reverse` 前缀 `{vid}`，**每顶点一次 seek**。
- `getVertexProperties` / `getVertexPropertiesBatch`：`vprop_{label}` 前缀 `{vid}`，
  **每顶点一次 seek**；由于按标签分表，一个顶点的属性分散在多张表。
- **旧的 coalesce 路径**（`ProjectionExtract`）：先 `getVertexLabelsBatch` 读标签
  （1 seek/行），再对**每个 (label, prop) 候选**调 `batchGetVertexProperties`
  （1 seek/行/命中候选）。c9 的 `coalesce(content, imageFile)` 在 Post 行是
  1 次 label + content + imageFile 两次属性读，Comment 行是 1+1，即约 **2–3 次 seek/行**；
  c5 的 label/property 读同理。新的 `getVertexCoalescePropertiesBatch` 用 `{vid}{0x01}`
  一次前缀扫完该顶点**所有标签**的属性，**1 次 seek/行**，在扫描中按 candidate 过滤。
- Expand：旧/新 N=0 都是 `edge_index` 每 source 一次 seek；边属性仍在 `eprop`。

**旧 → 新访问次数对照**

| 操作 | 旧 | 新（N=0） |
|---|---|---|
| 读标签 | `label_reverse` 前缀，1 seek | `vertex_data` `0x00` 前缀，1 seek |
| 读单标签属性 | `vprop_{label}` 前缀，1 seek | `vertex_data` `0x01{label}` 前缀，1 seek |
| 读多标签 coalesce | 1 label seek + 每命中 (label, prop) 1 次属性 seek | **1 seek** 扫全部属性 |
| Expand（一个 source） | `edge_index` 1 seek | 同 |
| 边属性 | `eprop` 1 seek | 同 |
| 删除顶点 | 每标签一次 `vprop` 前缀扫 | 每标签前缀扫 + 一次 `{vid}` 前缀扫 |

**为什么换掉旧格式**

1. 标签和属性从两张表变一张，coalesce 的 seek 次数从 `1 + 候选标签数` 降到 1；
2. 删除/清理可以用 `{vid}` 前缀统一，不再需要先读 `label_reverse` 再逐标签扫 `vprop`；
3. 为 per-vertex 邻接 key 留出位置（虽然当前默认 N=0）。

**代价**

1. 单表更大，原来按标签分表的页局部性丢失；c5 的 8,814 forum / 89k post 小批量访问
   因此回退约 6%；
2. `DROP LABEL` 从「drop `label_fwd`/`vprop` 表」变成「扫 `label_fwd` 逐顶点删
   `vertex_data` 前缀」，DDL 更慢（不在读 benchmark 内）；
3. 属性 key 多 2 字节 label（`{vid}{label}{prop}` vs `{vid}{prop}`），可忽略；
4. 不做历史迁移，旧图必须重新导入。

### 2.3 `vertex_data` 编码

```text
{v:u64BE}{kind:u8}{...}

0x00 LABEL      {v}{0x00}{label:u16BE}                        -> empty
0x01 PROPERTY   {v}{0x01}{label:u16BE}{prop:u16BE}            -> ValueCodec
0x02 OUT        {v}{0x02}                                     -> {state:u8}{count:u32BE}
                {v}{0x02}{label:u16BE}{neighbor:u64BE}{seq:u64BE} -> edge_id
0x03 IN         {v}{0x03}                                     -> 同上
```

设计意图：一个顶点的标签、属性、可选邻接按 `{v}` 前缀聚簇，`{v}` 前缀一次清扫即可删除
该顶点的全部记录；标签和属性共享一次前缀 seek。邻接 state 的 `COMPLETE` 表示该方向所有边
都在 `vertex_data` 内，`OVERFLOW` 表示已迁到 `edge_index`。

### 2.4 写路径放大（N=0，当前默认）

插入一条边：

```text
etype_{label}    1 行   {src}{dst}{seq} -> eid
eprop_{label}    k 行   {eid}{prop} -> value   （k = 该边实际有值的属性数）
edge_index       2 行   OUT/IN 各一条 {src}{dir}{label}{dst}{seq} -> eid
```

插入一个顶点：`vertex_existence` 1 行 + 每个标签 1 行 `label_fwd` + 1 行 `vertex_data` 标签
+ 每个有值属性 1 行 `vertex_data` 属性。

删除顶点：先按标签删 `label_fwd`/标签行/属性前缀，再扫描 `{v}` 前缀逐行删除（代码里是
`table_scan` 收集 key 后逐条 `table_del`）。删除边：删两个方向的 `edge_index`、`etype`、
`eprop` 前缀。

> **代码事实**：`kAdjCacheCapacity = 0`（`src/storage/data/sync_graph_data_store.cpp:17`）。
> 因此当前**不写** per-vertex state，也**不写**邻接 key；`edge_index` 保存全量边。
> `vertex_data` 只承担 labels + properties。

### 2.5 读路径：一次 Expand 和一次属性读

**Expand（一个 source，方向 OUT）：**

```text
acquire cursor on edge_index
set_key({vid}{OUT}[{label}])
search_near → 定位到该 source 的第一条边
loop:
  get_key / get_value
  decode 27-byte key → {vid, dir, label, neighbor, seq}
  decode 8-byte value → edge_id
  callback(entry)
  next
```

批量路径 `scanEdgesBatch` 每 4,096 个 source 复用一个 cursor / 一次 dispatch，但**每个 source
仍有一次 `search_near`**；共享 cursor 只省掉 open/close 与跨线程 dispatch，省不掉每源定位。
单个 source 的普通 Expand 仍走 `scanEdges`：一次 `io_.dispatch` 创建 cursor，再 `co_viaIfAsync`
回到绑定 IO 线程迭代。

**属性读（coalesce）：**

```text
getVertexCoalescePropertiesBatch(vids, candidates):
  one cursor on vertex_data
  for each vid:
    search_near({vid}{0x01})
    scan all property rows under that prefix
    decode key (label, prop); if (label, prop) in candidates → decode value
```

即：**每个顶点一次前缀 seek + 扫该顶点全部属性行**，不是「一条顶点记录一次 get」。
对于 Message（约 4–5 个属性），一次 coalesce 会扫 4–5 行；Person（约 10 个属性）扫 10 行。

### 2.6 邻接缓存（已实现，默认关闭）

设 **N** = 每个顶点每个方向允许留在 `vertex_data` 的最大边数（`kAdjCacheCapacity`，当前 **0**）。

```text
state: {v}{0x02|0x03} -> {state:u8}{count:u32BE}
COMPLETE: 边在 vertex_data  {v}{kind}{label}{neighbor}{seq} -> eid
OVERFLOW: 边在 edge_index；vertex_data 只留 state
第 N+1 条边：同一事务内把 N+1 条写 edge_index、删缓存 key、state=OVERFLOW
```

- 顶点创建时写 OUT/IN 两条 state（仅 N>0）。
- 度 ≤ N 时全部边在 `vertex_data`，`edge_index` 无该方向条目；高 degree 溢出后只读 `edge_index`。
- 不降级；要回 COMPLETE 只能离线重建。
- **N=64 的实测是负收益**：c5 329 ms（比 N=0 的 279.7 慢 18%）、c14 320 ms（慢 10%），
  原因是 `vertex_data` B-tree 变大、每个 source 多一次 state 读，且 c5 有 8,814 个 forum /
  89k post 的小批量访问。
- N=0 时 4 个 RPC 连接可正常导入（28.3 s）；N>0 时多连接会在 per-vertex state 上产生
  `WT_ROLLBACK -31800`，需单连接或 bulk build。

### 2.7 与 neo4j 的结构对照

| 维度 | neo4j 5.26 | eugraph（本分支） |
|---|---|---|
| 顶点身份 | 定长 node record id（稠密） | 外部 u64 vid；无固定槽位 |
| 标签 | node record 内 label bitmap | `vertex_data` 中每标签一行 |
| 属性 | node record 内 property 指针 → property record | `vertex_data` 中每属性一行，key 含 vid+label+prop |
| 邻接 | relationship record + 双向链表，指针直连 | B-tree 按 `{vid}{dir}{label}{neighbor}{seq}` 排序，范围扫 |
| 边属性 | relationship record + property 指针 | 独立 `eprop_{label}` 表，按 eid 查 |
| 一次遍历 | 指针解引用（页内/对象缓存） | B-tree search + key/value 解码 |
| 局部性 | node/rel/property record 按页聚簇 | 按 key 排序聚簇；边按 source，属性按 vertex |
| 缓存 | page cache + 对象缓存 | WT cache（256 MB）+ OS page cache；无图对象缓存 |
| 写 | 原地改定长 record + 事务日志 | KV 插入/删除（变长 key），WT 事务 |

> 这不是「谁更先进」的评价，而是解释 per-access 差异：neo4j 的一次邻接读取通常是
> 指针 + 页内偏移；eugraph 是一次 B-tree 搜索 + 变长 key 解码。前者可以做到 0.1–0.3 µs，
> 后者在当前实现里是 0.9–1.9 µs（§5.2）。

### 2.8 这次改造**没有**改变什么

1. 仍是**一条属性 = 一条 KV 记录**（不是一条顶点记录）；
2. 仍是**一条边 = 两条 `edge_index` 记录**（N=0），Expand 仍是**每 source 一次 seek**；
3. 边属性仍在**独立表**，读边属性仍要第二次 seek；
4. 仍**没有**按 source 的邻接数组/打包编码，仍逐条 key 解码；
5. 仍**没有** CBO/度数统计（N=0 时连 state.count 都没有）、没有 creationDate 索引；
6. **存储 value 解码仍是逐值**（表达式层有向量化求值，但 KV 读取/解码不是列式批量）；
   算子仍以行为单位、经协程/回调传递 `Value`。

> 结论：这次改造是「布局整理 + 为未来邻接缓存留接口」，**不是**访问范式的改变。
> 因此它不可能单独带来数量级提升；它的价值主要是统一了标签/属性读取（c9 coalesce 受益）
> 和为 per-vertex 邻接铺路，代价是 `vertex_data` B-tree 变大（c5 约 +6%）。

---

## 3. 分支时间线与实测

### 3.1 提交序列（与本分析相关）

| 提交 | 内容 | 类型 |
|---|---|---|
| `2fb72283` | `vertex_data` 合并标签/属性，删 `label_reverse`/`vprop` | 存储布局 |
| `f02573d7` | `vertex_data` 邻接 key + OVERFLOW 分层 | 存储布局（默认关） |
| `47ca565a` | 邻接缓存流式 cursor；记录导入回归 | 存储/游标 |
| `252e1e99` | 融合 labels + coalesce 属性批量读 | 存储读路径 |
| `3372ecf8` | 默认关闭邻接缓存（N=0），保留融合 | 配置决策 |
| `5a1d408a` | `scanEdgesBatch` 每 4,096 source 共享 cursor | 存储/批量 |
| `93e8c538` | 移除错误的 filter-boundary sort 延迟物化 | **计划/正确性** |
| `7162f373` | 全量 21 条复跑，补 `Message.id` 索引 | 基准 |
| `adafc818` | 说明 update 缺 fixture | 基准 |

### 3.2 归因表（关键：计划/算子 vs 存储）

| 查询 | 改动 | 改前 → 改后 | 归因 |
|---|---|---|---|
| c14 | PC anchor `AllNodeScan` → `LabelScan(Person)` + 端点谓词下推 | 80.5 s → 0.81 s（另一轮 835 ms） | **计划**（binder） |
| c14 | `CrossProduct(CorrelatedSource, LabelScan)` → `CorrelatedVertexProbe` | 835 ms → 295 ms | **计划/算子** |
| c5 | correlated `LeftJoin` 每 512 行批处理 | 514 ms → 264 ms | **算子** |
| c9 | LIMIT 边界 LM + Top-N Sort | 422 ms → 245 ms | **算子/计划** |
| c9 | `vertex_data` + coalesce 融合（+ 共享 cursor） | 245 ms → 187–232 ms | **存储读路径**（245 是 LIMIT LM 的中间态；旧 161.7 无效，不能干净归因） |
| c5 | `scanEdgesBatch` 共享 cursor | 279.7 ms → 243.9 ms | **存储/批量**（-13%） |
| c5 | `vertex_data` + coalesce 融合（同计划，N=0） | 264 ms → 279.7 ms | **存储布局**（+6%） |
| c14 | `vertex_data` + coalesce 融合（同计划，N=0） | 295 ms → 290.7 ms | **存储布局**（-1.5%） |
| c9 | `vertex_data` + coalesce 融合（同计划） | 161.7 ms → 131.4 ms | **不可比**（两边都是错误计划） |
| c3 | batch 去重 + 共享 `VertexValuePtr` + 无属性边跳过 eprop | 490 ms → 136 ms（17–21× → 3–5×） | **算子/物化** |

> **最重要的读法**：把所有「存储-only」行加起来，c5 净 -8%、c14 净 -3.4%、c9 未知；
> 而计划/算子行是 2–270×。但计划/算子的大头是**历史修复**，不是「现在还有一个大 bug」。
> 当前剩余差距要看 §5。

---

## 4. 最新全量同轮 A/B（2026-10-25）

### 4.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | eugraph/neo4j |
|---|---:|---:|---:|
| short-5 | 1.16 | 1.81 | 0.64× |
| short-6 | 1.44 | 2.06 | 0.70× |
| short-4 | 0.95 | 1.34 | 0.71× |
| short-1 | 1.76 | 2.13 | 0.83× |
| short-3 | 1.51 | 1.80 | 0.84× |
| short-7 | 1.42 | 1.45 | 0.98×（0 行） |
| short-2 | 5.21 | 2.88 | 1.81× |

> 7 条 short 的几何均值约 **0.88×**；除 short-2 外 eugraph 都更快。说明**点查/索引 seek
> 这条路径本身没有问题**，差距不在「KV 引擎一定慢」。

### 4.2 Complex reads（按比值升序）

| 查询 | eugraph (ms) | neo4j (ms) | 比值 | 备注 |
|---|---:|---:|---:|---|
| complex-13 | 1.49 | 2.07 | **0.72×** | shortestPath，eugraph 更快 |
| complex-8 | 4.58 | 3.24 | 1.41× | |
| complex-7 | 6.26 | 3.10 | 2.02× | |
| complex-11 | 7.63 | 3.74 | 2.04× | |
| complex-4 | 8.49 | 3.74 | 2.27× | 0 行 |
| complex-2 | 9.96 | 3.94 | 2.53× | |
| complex-6 | 77.32 | 29.08 | 2.66× | |
| complex-5 | 249.76 | 89.53 | **2.79×** | 89k posts × 2 expands |
| complex-1 | 18.26 | 5.22 | 3.50× | 行数 20/15，数据差异 |
| complex-12 | 380.55 | 99.05 | 3.84× | 仅 tag 列表顺序不同 |
| complex-3 | 109.85 | 27.66 | 3.97× | 0 行 |
| complex-10 | 44.29 | 9.24 | 4.79× | |
| complex-9 | 231.90 | 28.71 | **8.08×** | 53k expand + 53k 属性读 |
| complex-14 | 351.14 | 16.46 | **21.33×** | 24 次 PC 展开 |

> **汇总**：14 条 complex 的中位数约 **2.7×**，几何均值约 **3.1×**；若剔除最极端的 c14，
> 几何均值约 **2.7×**。专项复测的同轮比值是 c5 2.8×、c9 6.7×（187.2/28.1）、
> c14 16.5×（291.4/17.7）；与全量表的差异来自轮次/频率，不要混用。

### 4.3 Updates（上一轮口径，本轮不含结论）

| 语句 | eugraph (ms) | neo4j (ms) | eugraph/neo4j |
|---|---:|---:|---:|
| update-3 | 7.75 | 57.62 | 0.13× |
| update-2 | 7.55 | 51.95 | 0.15× |
| update-5 | 7.64 | 15.15 | 0.50× |
| update-8 | 7.45 | 13.34 | 0.56× |
| update-6 | 26.62 | 20.64 | 1.29× |
| update-4 | 26.08 | 17.01 | 1.53× |
| update-1 | 53.7 | 23.9 | 2.25× |
| update-7 | 440.07 | 96.08 | 4.58× |

> 写路径并非全面落后：4 条更快，4 条更慢。这说明「KV 存储」本身不是均匀的劣势，
> 具体取决于访问模式；update-7 的 4.6× 值得单独 profile，但不在本问题范围内。

---

## 5. 差距从哪里来

### 5.1 已兑现的计划/算子收益（证明「计划/代码」确实曾经是主因）

| 案例 | 改动 | 效果 | 说明 |
|---|---|---:|---|
| c14 | `AllNodeScan` → `LabelScan` + 端点谓词下推 | 80.5 s → 0.81 s | 纯 binder/优化器；存储未动 |
| c14 | 去掉 `CrossProduct(CorrelatedSource, LabelScan)` | 835 ms → 295 ms | 纯算子/计划 |
| c3 | batch 去重、共享 `VertexValuePtr`、无属性边跳过 eprop | 490 → 136 ms；17–21× → 3–5× | 纯算子/物化 |
| c5 | correlated `LeftJoin` 512 行批处理 | 514 → 264 ms | 纯算子 |
| c9 | LIMIT 边界 LM + Top-N | 422 → 245 ms | 纯算子/计划 |

这些数字说明：**在 2026-10 之前，计划/算子的确可以造成 2–270× 的差距**。所以「我们的执行
计划/代码有问题」这个判断，历史上是对的。但它们已经被修掉了；**当前剩余差距不能再用这些
历史数字解释**。

### 5.2 每次访问的成本（当前主要地板）

> 以下都是「数量级」估算，来自不同查询/不同轮次的阶段计时，不是同一层测量的严格对比。

| 操作 | 实测/推算 | 来源 |
|---|---:|---|
| c9 Expand `HAS_CREATOR` 53,192 条 | 47 ms → **0.88 µs/edge** | c5/c9 阶段计时（旧路径） |
| c9 每 message 读 label + creationDate | 98 ms / 53,192 → **1.84 µs/行** | 同上；融合前是两次 seek |
| c5 每 source `createEdgeScanCursor` | 98k 次 / 184 ms → **1.88 µs/次** | c5 批处理前实测 |
| c5 每 edge（CONTAINER_OF + HAS_CREATOR） | 250 ms / 178,830 → **1.40 µs/edge** | 当前总耗时摊算 |
| c3 每 message 两次属性读 | 80–90 ms / 52,475 → **1.5–1.7 µs/行** | complex-3 复盘 |
| neo4j 每 message 属性读（c3） | +15.6 ms / 52,475 → **0.30 µs/行** | neo4j PROFILE |
| neo4j c9 全部（53k expand + 属性 + sort + 投影） | 28.7 ms / 53,192 → **0.54 µs/edge** | 总耗时摊算 |
| neo4j c5 全部（89k posts × 2 expands） | 89.5 ms / 178,830 → **0.50 µs/edge** | 总耗时摊算 |

**读法**：eugraph 的每访问成本约 **1–2 µs**，neo4j 摊到每 edge/行的全部成本约 **0.3–0.5 µs**；
单看存储层差距约 **3–6×**。但查询总时间不只存储，所以最终比值落在 1.4–21× 之间。

### 5.3 代码级的具体放大点（可核对）

> 这些是「读代码能看到」的候选，不代表每一条都已在 profile 中确认。按预期影响排序。

1. **每个 source 一次 `search_near`**：`scanEdgesBatch` 只共享 cursor，不共享 seek；
   低 degree、多 source 的 LDBC 展开（c5/c9/c14）每次仍要定位 B-tree。
2. **coalesce 扫整个顶点前缀**：`getVertexCoalescePropertiesBatch` 对每个 vid 扫该顶点
   **全部属性行**，只在 decode 后按候选过滤；Message 4–5 行、Person ~10 行。
   若改成「一条顶点记录 + 只 decode 需要的属性」，可省掉行过滤与多次 key decode。
3. **每 edge 的 key 拷贝**：`scanEdgesBatch` 中 `std::string key = getKeyFromCursor(c);`
   对 27 字节 key 会堆分配（超过常见 SSO 15/22 字节）；每 edge 一次。
   `decodeEdgeIndexKey` 本可直接用 `string_view`。
4. **逐 edge 回调 + 间接调用**：`scanEdges`/`scanEdgesBatch` 的 callback 已从
   `std::function` 改为 `folly::FunctionRef`（去掉拥有型类型擦除/堆分配），但调用仍是
   间接调用，不能像模板一样内联；这是接口边界的剩余成本。
5. **`scanEdges` 每 source 预留 65,536 条**：`async_graph_data_store.hpp` 中
   `constexpr size_t BATCH = 65536; batch.reserve(BATCH);`。对只有 1–2 条边的 source，
   这会分配约 2 MB（`EdgeIndexEntry` 约 32 B）再丢弃，而且下一次循环还会再分配一个空
   batch；VLE/shortestPath 的逐顶点扫描可能被放大。是否命中热路径需实测（c9 主 Expand
   走 `scanEdgesBatch`，不走这里）。
   `scanEdgesBatch` 的上限是 65,536 条（main）/ 4,096 source（feature），**但 `Expand`
   每次只传一个 DataChunk（≤1,024 行）**，所以 c5 实际每批约 1–2k 条（40–80 KB）、
   约 96 次调用，总分配量 ~8 MB；只有高 fanout 的 chunk 才接近 2.5 MB 上限。
   它是**按批分配 + `co_yield std::move` 转移所有权**，malloc/free + 重分配拷贝约
   **0.1 ms 量级**，不是 250 ms 的主因；真正的固定成本是每 source 的 cursor/seek 和
   每 edge 的 key/value 解码。**要优先修的是 `scanEdges` 的 per-source 2 MB，以及每
   source 的 cursor/seek；不是 `scanEdgesBatch` 的批上限。**
6. **边属性独立表**：Expand 后若要边属性（如 `creationDate`），是第二次 B-tree seek；
   LDBC 很多边有属性，但当前基准的 c5/c9 主要读顶点属性，未单独量化。
7. **无 CBO/度数统计**：优化器是 RBO；N=0 时没有 `state.count`，无法用度数选 join 顺序/
   驱动侧。c14 的 24 次 PC 展开顺序、c10 的 VLE 后展开顺序都受影响。
8. **无 `creationDate` 索引**：c3/c9 的高选择性日期谓词只能逐行读属性；neo4j 也没走索引
   （两边同计划），所以这不是「对齐 neo4j」的必需项，但它是减少访问次数的可选杠杆。
9. **单一 `vertex_data` B-tree 混合 kind**：标签、属性、可选邻接共享一棵树；N=64 时
   B-tree 变大，点查 page 局部性下降（c5 +18%）。这是「合并表」的代价。
10. **DataChunk 1,024 / LeftJoin 批 512 的限制**：批 1,024/2,048 会触发 chunk 边界丢值
    （c5 `postCount` 10→6），根因未定位；含 PATH 的右子计划回退逐行。这限制了批处理收益。
11. **WT cache 256 MB vs 工作集**：按 1.48M 边、约 8M 行估算，原始 payload 约 0.2–0.3 GB，
    加 WT 开销约 0.3–0.6 GB；256 MB cache 不能全覆盖，随机 seek 可能落到 OS page cache/磁盘。
    OS page cache 会兜底，但不可控；需用「加大 WT cache」实验判断 I/O 敏感度。
12. **弱属性 candidates 未按变量标签裁剪（c14 直接可见，见 §5.5）**：
    `bind_expression.cpp:377` / `bind_match.cpp:2037` 对非 CREATE 变量扫描全 catalog 标签；
    `id` 得到 14 个候选，物理计划插入 `ProjectionExtract(vprop-coalesce[14])`，
    `Filter` 在其上，`tryBoundIndexScan` 看不到下面的 `LabelScan`；结果端点走
    `LabelScan(Person)` 而不是 `Person.id` 索引，PC 的 `b.id` 还要对每条路径检查 14 组
    `(label, prop)`。这条**同时**是计划问题和每行解码成本，优先级应高于第 5 条。
13. **`tryExtractBoundCondition` 用 `candidates[0]`**：`physical_planner.cpp:1298` 对多候选
    弱属性取第一个 label 作为索引条件；如果第一个候选不是变量真实标签，可能选错索引或漏选。
    这是 §5.5 之外的独立隐患。

### 5.4 混淆因素（会误导判断）

1. **c9 的旧 161.7 ms 是错误结果**；任何「存储改造让 c9 从 161.7 降到 131.4」的说法都无效。
2. **跨轮绝对值差 ±20–30%**：c14 291.4 vs 351.1、c9 187.2 vs 231.9 是不同轮次；只能看同轮比值。
3. **c1 数据差异**（缺 email/speaks）、**c12 列表顺序**、**c3/c4 0 行**：这些行不应当作
   完整语义的性能结论。
4. **update 缺 fixture**：本轮 update 没有有效对比。
5. **没有 LDBC 官方 driver 的审计/校验**：计时用 Python driver，包含客户端/协议开销；
   对 1–5 ms 的 short 查询，这部分不可忽略（但两边同口径）。
6. **没有 CPU 绑核/频率固定**：文档明确记录同二进制可差约 2×。

### 5.5 c14 计划对照：14-candidate 弱属性 + 索引失效（附件 `fenxi.txt`）

用户提供的 c14 计划对照（sf0.1，`person1Id=933, person2Id=1129`）暴露了两个具体的
planner/binder 问题。以下是简化后的计划形状（省略顶层 Project/Sort）：

```text
# eugraph
allShortestPaths
  CrossProduct
    Filter
      ProjectionExtract(person1.id, vprop-coalesce[14 candidates])
        LabelScan(Person)                         # 1,528 行
    Filter
      ProjectionExtract(person2.id, vprop-coalesce[14 candidates])
        LabelScan(Person)                         # 1,528 行
...
PatternComprehensionApply (weight1)
  ...
    Filter
      Filter
        ProjectionExtract(..., b.id, vprop-coalesce[14 candidates])
          Expand(Post -> b, HAS_CREATOR, OUT)
            Expand(Comment -> Post, REPLY_OF, OUT)
              Expand(a -> Comment, HAS_CREATOR, IN)
                Filter
                  Filter
                    CorrelatedVertexProbe
                      ... LabelScan(a:Person)
```

14 个候选是 `[2.3,3.1,4.0,5.6,6.0,7.3,8.0,9.0,10.3,11.0,12.0,13.0,14.0,15.0]`，
正好是**所有带 `id` 属性的 14 个标签**。但 `person1`、`person2`、`b` 在查询里都静态标了
`:Person`，只需要 `Person.id` 一个候选。

neo4j 侧贴出的部分是：

```text
ShortestPath
  CartesianProduct
    NodeIndexSeek person2:Person(id) WHERE id = $autoint_1
    （person1 的 NodeIndexSeek 应在下一行；附件在 367 行截断）
```

**问题 1：端点没有走 `Person.id` 索引**

- eugraph 对两个端点各做 `LabelScan(Person)`（各 1,528 行）+ 14-candidate
  `ProjectionExtract` + `Filter`；
- neo4j 是 `NodeIndexSeek`，O(1)；
- short-1/2/3 已经证明 `Person.id` 索引存在且可用（1–2 ms），所以这不是索引缺失，
  而是**计划没有选中索引**。复测前先 `SHOW INDEXES` 确认 loader 建的
  `idx_Person_id_unique` 是 `PUBLIC`；若不是，先建/修索引再判断计划问题。

**最小对照**：下面两条只差「`{id: ...}` 在普通 MATCH 还是 allShortestPaths 端点」：

```cypher
EXPLAIN MATCH (p:Person {id: 933}) RETURN p;                 -- 预期 IndexScan
EXPLAIN MATCH path = allShortestPaths(
  (p1:Person {id: 933})-[:KNOWS*0..]-(p2:Person {id: 1129})) RETURN path;
                                                             -- 当前 LabelScan + PE + Filter
```

第一条如果走 `IndexScan`、第二条不走，就说明索引选择没有覆盖 shortestPath 端点上的
inline property；这也是后续回归测试的判据。

**问题 2：PC 内的 `b.id` 也是 14-candidate**

每个 PC 在 3 跳展开后，对**每条候选路径**执行 `ProjectionExtract(b.id, 14 candidates)`，
然后才做 `a.id/b.id` 过滤。`b` 静态是 `Person`，本应只读 `Person.id`；14 个候选意味着
每条路径都要扫该顶点的属性行并检查 14 组 `(label, prop)`。这会叠加在约 280 ms 的 PC
展开上，是 §5.2 的 per-access 成本之外的**额外每行解码**。

**影响量级（估算，需 profile 确认）**

- **端点**：2 × 1,528 行，按 2–10 µs/行算约 **6–30 ms**（291 ms 的 2–10%）。即使索引命中，
  c14 仍约 280 ms；所以端点未走索引主要是**计划质量**问题，单独修复对 c14 总时长影响小。
- **PC 的 `b.id`**：每条候选路径一次。若 24 次 PC 合计产生 2–4 万条路径，按 1–3 µs/行算
  约 **20–120 ms**（280 ms 的 7–40%）；这是该计划问题里**真正可能影响性能**的部分，
  但占比必须用 profile 确认。
- **剩余 160–250 ms** 是 24 次 PC 的 3 跳展开（Comment→Post→Person）+ 过滤本身；
  neo4j 用 17.7 ms 完成同样的逻辑。即使把 14-candidate 全部消掉，c14 大概率仍在
  **10–14×**，到不了 2×；要接近 neo4j 还得减少 PC 执行次数/共享前缀，并降低 per-edge 成本。

**根因链（代码级）**

1. **非 CREATE 变量的弱属性访问扫描全 catalog 标签**：
   `bind_expression.cpp:377` 用 `lookupPropertyAcrossLabels(all_labels, ...)`，
   `col_info->source_labels` 只在 CREATE 变量分支使用；PC/子计划的同类路径在
   `bind_match.cpp:2037`。`id` 在 14 个标签上都有，于是 candidates=14。
2. **物理计划把弱属性物化成 `ProjectionExtract`，放在 `Filter` 之下**；
   `tryBoundIndexScan` 只匹配 `Filter(LabelScan)`（`physical_planner.cpp:2952`），
   看不到 PE 后面的 `LabelScan`，索引规则不触发。
3. **即使触发，`tryExtractBoundCondition` 用 `candidates[0]` 当条件标签**
   （`physical_planner.cpp:1298`）；多候选弱属性可能选到错误 label，是独立隐患。

**修复方向**

- 为 pattern/path 变量传播已知标签（`Person`），把弱属性 candidates 裁剪到交叠标签；
  单标签时直接生成一个 accessor（或 strong accessor）。
- 让索引规则能穿透 PE/Project，或在插入 PE 之前先尝试
  `Filter(LabelScan) → IndexScan`；单标签等值谓词命中索引后，PE 只负责物化顶点。
- 回归判据：c14 端点的 `EXPLAIN` 出现 `IndexScan(Person.id)`；PC 的 `b.id` 的
  `vprop-coalesce` 只剩 1 个候选（或没有 coalesce）。
- A/B：先只改候选裁剪，再改索引穿透，单变量验证；预期端点扫描消失、PC 每行属性解码
  下降，具体幅度需实测。

> **注意**：计划打印没有输出 Filter 谓词；上面的 `id` 是从「14 candidates = 所有带 `id`
> 的标签」+ 查询中的 `person1.id` / `person2.id` / `b.id` 推断的。建议用
> `EUGRAPH_PLAN_DEBUG=1` 或谓词调试再确认一次。

### 5.6 计划 vs 存储：谁占大头？（粗估）

把查询时间拆成：

```text
T ≈ A(plan) × c(storage) + T_operator
```

- `A(plan)`：计划决定的**访问次数**（展开多少边、读多少属性、执行多少次 PC）；
- `c(storage)`：**每次访问的成本**（B-tree seek / 解码 / 协程派发）；
- `T_operator`：排序、聚合、投影、值拷贝。

| 查询 | 当前 vs neo4j | 访问次数 A | 每次成本 c | 主因（粗估） |
|---|---:|---|---:|---|
| c5 | 2.8× | ~1×（两边都 89k posts × 2 expands） | ~2.8× | **存储 70–80%**；计划浪费有限 |
| c9 | 6.7–8.1× | ~1×（两边都 53k expand + 属性读） | ~4–8× | **存储 60–70%** + 算子/排序 |
| c14 | 16.5–21× | 2–4×（24 次 PC、14-candidate） | 2–4× | **计划 ~50–60%**（14-candidate/PC 冗余） |
| 小 complex（c7/c8/c11/c13） | 1.4–2.0× | ~1× | 1.4–2× | 算子/属性物化 ~50–60% |
| 中 complex（c1/c2/c6/c10/c12） | 2.5–4.8× | 1–2× | 2–3× | 计划/算子与存储各半 |

> 这些是数量级估算，不是 profile 的精确比例。`A` 是**逻辑行数**；eugraph 的物理 KV
> 访问次数通常更高（label/property 分行、边两方向），这部分折算进 `c`，不单独列。
> `A` 和 `c` 是乘法关系：A 多 2×、c 贵 3×，总时间就是 6×；修任何一个都能减半。
> 所以「谁占大头」会随查询变化，不能用一个百分比概括整个 suite。

**历史证据**

- 计划/算子单独修复：c14 80.5 s→0.295 s（270×）、c3 490→136 ms（3.6×）、c5 514→264 ms
  （1.95×）、c9 422→245 ms（1.7×）——**计划 bug 可以单独造成数量级差距**。
- 存储布局单独修复（本分支 `vertex_data`）：c5 -8%、c14 -3.4%、c9 不可比——
  **只改布局、不改访问模型，约中性**。
- 这两组证据说明：**大差距往往先是计划 bug；bug 修完后，剩余的 2.5–8× 地板由每次访问
  成本决定。**

**结论**

1. **当前 residual：存储访问模型略大于计划**——c5/c9 存储为主；c14 计划为主。
2. **历史大差距：计划为主**，而且这些具体 bug 已经修掉。
3. **性价比**：先修计划（候选裁剪、索引穿透、PC 共享前缀、CBO），再改存储访问模型
   （邻接打包、属性共置、批量解码）；不要再用「换个表布局」期待大收益。
4. **要确认比例**：做 2×2 实验——baseline / 只修计划 / 只改存储 / 两者，同轮 A/B 测
   c5/c9/c14；否则百分比只是估算。

### 5.7 两项底层优化：per-source seek 与 per-edge decode/copy

> 以 c5（250 ms、98k source、约 176k edge）为口径的数量级估算；各项**不可简单相加**，
> 且必须同轮 A/B。

**A. 每 source 的 cursor/seek**

| 方案 | 预期收益 | 风险/代价 |
|---|---:|---|
| 共享 cursor（feature 已有） | 实测 c5 -36 ms（279.7→243.9） | 低；只省 open/close，不省 `search_near` |
| 对 `src_ids` 排序/去重 | 提升 B-tree page 局部性；约 0–15 ms | 输出边顺序变化（Cypher 无 ORDER BY 不保证，但 TCK 需验） |
| 混合 `next`/`search_near`（排序后，gap 小就顺序扫） | 对稠密 source 可再省 0–30 ms | 稀疏 source 会扫大量无关边；需密度阈值 |
| 每 source 一条 packed adjacency 记录 | 每 source 一次 `get` + 连续数组解码；去掉 per-edge `next`/key 解码，约 **15–30 ms** | KV 编码/更新/超级节点溢出；`search_near` 本身仍在 |
| 内存 CSR / 热顶点邻接缓存 | 去掉 per-source B-tree seek，约 **50–100 ms**；最接近 neo4j | 内存（sf0.1 约 100 MB 量级）、更新/MVCC/一致性、失效策略 |

**B. 每 edge 的 key/value 解码与 `BatchedEdge` 拷贝**

| 方案 | 预期收益 | 风险/代价 |
|---|---:|---|
| `getKeyFromCursor` → `getKeyViewFromCursor` | 去掉每 edge 27 B 的 `std::string` 堆分配；约 **5–15 ms** | 低；`decodeEdgeIndexKey` 已接受 `string_view` |
| 专用解码（已知 vid/dir/label，只读 neighbor/seq） | 约 **2–4 ms** | 低；需处理无 label filter 的情况 |
| 去掉 per-edge `std::function`（已完成 `FunctionRef`）；剩余 `BatchedEdge` 临时对象 | 剩余约 **1–2 ms** | `BatchedEdge`/vector-fill 仍需单独重构 |
| packed adjacency value（neighbor/seq/eid 连续） | 约 **10–20 ms** | KV 编码兼容性；与 A 的 packed record 是同一改动 |
| 直接写输出 chunk，去掉中间 `edges` 拷贝 | 约 **1–2 ms** | 算子层重构；收益小 |

> **feature/main 的 tradeoff**：feature 的共享 cursor 省了 36 ms，但它的 `scanEdgesBatch`
> 里 `std::string key = getKeyFromCursor(c)` 又引入了每 edge 的堆分配；main 的
> `EdgeScanCursorImpl` 用 `string_view`，但每 source 建 cursor。**把两者合并
>（共享 cursor + string_view）是第一个低风险 win。**

**优先级**：string_view（几乎免费）→ 共享 cursor 回移 → `scanEdges` 的 per-source 2 MB
修复 → packed adjacency 原型 → 内存 CSR / 热缓存。前两项预计 c5 省 10–20 ms；
packed adjacency 再省 20–40 ms；CSR 才可能把 c5 拉到 100–150 ms（1.1–1.7× neo4j）。

---

## 6. 这套存储能到 neo4j 的多少？（估算，请重点质疑）

### 6.1 分解模型

把查询时间近似拆成：

```text
T ≈ N_access × c_access + N_row × c_row + T_fixed
```

- `N_access`：B-tree seek / 属性行扫描 / 边扫描次数；
- `c_access`：每次访问的存储 + 解码 + 回调 + 协程/派发摊销成本；
- `N_row`：算子层处理的行数（表达式、聚合、排序、投影）；
- `c_row`：每行算子成本；`T_fixed`：解析/规划/建连等。

当前观测（§5.2）：`c_access(eugraph) ≈ 1–2 µs`，`c_access(neo4j) ≈ 0.3–0.5 µs`（摊算）。
因此：

- **访问次数与 neo4j 同量级**的查询（c5/c9），比值主要由 `c_access` 及属性读/算子占比决定
  → **2.5–6×**（c5 2.8×；c9 8× 还叠加了 53k 行属性读与 sort/投影）；
- **访问次数被计划放大**的查询（历史 c14、c3）会出现 10–270×，但计划修复后可回落；
- **访问次数很少**的查询（short、c13）由固定开销/算子主导 → **0.6–2×**。

### 6.2 分查询类别的估算

> 下表是「工程估算」，不是承诺。「只修计划/算子」指不改存储布局；「存储打包/共置」指
> 每顶点一条记录、邻接数组、边属性共置等结构性改动。

| 类别 | 当前实测 | 主要成本 | 只修计划/算子 | 再做存储打包/共置 | 到 1.3–2× 的条件 |
|---|---:|---|---:|---:|---|
| 点查/short | 0.6–1.0× | index seek + 1–2 hops | 0.6–1.0× | 0.6–1.0× | 已达成 |
| 小 complex（c7/c8/c11/c13） | 1.4–2.0× | 数百–数千次访问 + 算子 | 1.2–1.8× | 1.2–1.8× | 批量/减少每行算子开销 |
| 中 complex（c1/c2/c6/c10/c12） | 2.5–4.8× | 数万次属性/边访问 | 1.8–2.5× | 1.3–2× | 属性共置、批量读、CBO |
| c5 | 2.8× | 89k posts × 2 expands | 2–2.8× | **1.4–2×**（半连接有机会 <1×） | 邻接打包；半连接减少访问 |
| c9 | 6.7–8.1× | 53k expand + 53k 属性读 + sort/投影 | 4–6× | **3.5–5×**（乐观 3–4×） | 属性共置 + 批量 + 降算子开销 |
| c14 | 16.5–21× | 24 次 PC 展开（约 280 ms） | 6–10×（PC 前缀融合；再修 §5.5 可更低） | **4–8×**（再加邻接打包） | PC 去相关/共享前缀 + 邻接打包 |

> c14 的 4–8× 估算**没有计入 §5.5 的两个 planner 修复**（14-candidate 裁剪 + 索引穿透）。
> 若 `b.id` 的 14-candidate 解码占 280 ms 的显著部分，修复后 c14 可能落到 3–6×；
> 幅度需 A/B。

**整体**：

- 当前：complex 中位数 **2.7×**、几何均值 **3.1×**；
- 只做计划/算子剩余项：中位数约 **2×**；c5 2–2.8×、c9 4–6×、c14 6–10×；
- 再做存储打包（每顶点记录/邻接数组/属性共置）+ cache 调优：多数 **1.3–2×**，
  c5 **1.4–2×**（有机会 ≤1×），c9 约 **3.5–5×**，c14 约 **4–8×**；
- **全负载 1× 不现实**；点查/short 和小 complex 可以达到或超过 neo4j。

### 6.3 为什么不是 1×

1. **每次访问的固定成本**：B-tree search + 变长 key 解码 + `Value` 构造 + 回调 + 协程
   摊销，很难降到 neo4j 的指针解引用水平；即使存储层做到 0.3 µs，算子层仍有成本。
2. **c9 的非存储开销已经不小**：当前 232 ms 中，Expand + 属性读按 §5.2 估算约 130–150 ms，
   其余约 85 ms 是 VLE/sort/投影/聚合/PE。即使存储访问减半（145→72 ms），总时间仍约
   160 ms（5.5×）；要到 3.5×，需要存储与算子开销同时明显下降，而不是只换存储布局。
3. **c14 的 24 次 PC 展开是真实工作**：锚点修复后仍有约 280 ms 是 Comment/Post 展开；
   要大幅下降，必须减少展开次数（PC 共享前缀/去相关）或把邻接打包到接近内存扫描。
4. **更新/事务/多版本**：KV 布局要支持删除/DDL/多标签，不能像只读内存图那样任意重排。

### 6.4 能把上限抬高的改动（按预期收益/风险）

| 方向 | 预期收益 | 风险/代价 | 备注 |
|---|---|---|---|
| **每顶点一条打包记录**（label bitmap + 热点属性，必要时溢出） | 属性读从「前缀扫 4–10 行」变「1 次 get + 解码」；c9/c3 类受益最大 | KV 编码/兼容性/DDL/大属性溢出；是外部兼容面 | 最值得做原型 |
| **每顶点/每方向邻接数组**（packed neighbor/eid/seq，或独立 adjacency 表） | 每 source 一次 seek + 连续解码；c5/c14 受益 | 超级节点溢出、更新/删除、跨表一致性 | 比「N=64 小缓存」更彻底 |
| **边属性与邻接共置** | 去掉每边第二次 eprop seek | 写放大；属性变更 | LDBC 边属性常用 |
| **批量/向量化解码** | 降 `c_access` 的代码开销（key 拷贝、`std::function`、逐行 `Value`） | 改动面大但语义风险低 | 可与上两项叠加 |
| **CBO + 度数统计** | 改善 join 顺序/驱动侧；c10/c14 可能受益 | 需要统计信息与代价模型 | N=0 时没有 degree，需另建 |
| **cache/压缩/page size** | 降低随机 seek 的 I/O 比例 | 参数调优，非结构性 | 先用加大 cache 实验判断 |
| **PC 共享前缀/去相关** | c14 的 24 次展开可能大幅减少 | 语义守卫复杂，需 TCK | 与存储改动独立 |

### 6.5 已被试过/不建议重复

1. **把邻接 key 塞进 `vertex_data`（N=64）**：相对 N=0，c5 329（+18%）、c14 320（+10%），
   因为 B-tree 变大 + 每 source 多一次 state 读。**除非同时打包，否则不要重复**。
2. **只共享 `scanEdgesBatch` cursor**：c5 -13%，有用但不是数量级；瓶颈仍是每源 seek。
3. **手工 join 重排替代 CBO**：曾实测退化到 35 s+，不要用简单 pattern 重排替代代价模型。
4. **LeftJoin 批 1,024/2,048**：c5 出现丢值（postCount 10→6），根因未定位；当前用 512 规避。
5. **「把 labels + 属性并表」本身**：c9 的 coalesce 融合确实有效，但 c5 回退 6%；
   合并表不是免费收益，必须按访问模式设计记录布局。

### 6.6 能证伪本文的测量

1. **同计划 old-vs-new 存储 A/B**（修复 c9 后，分别用旧 `label_reverse/vprop` 与新
   `vertex_data` 跑 c9），量化存储-only 效果；目前 c9 缺这个数据。
2. **per-operator 计时 + 存储调用计数**：把 Expand / 属性读 / Sort / 投影分开；确认
   c9 的 232 ms 里存储 vs 算子各占多少。
3. **WT 统计**：cache hit ratio、pages read、search ops、cursor open/close 次数。
4. **CPU/off-CPU profile**：WT search、key decode、`Value` 构造、协程调度、`std::function`
   各占多少；当前 GDB 采样只能证明 WT search 在 c3 占主导，尚未覆盖 c5/c9/c14 全路径。
5. **分配计数**：每查询的 key/value 字符串分配次数（验证 §5.3 第 3 条）。
6. **加大 WT cache 到 ≥2 GB**：若 c5/c9/c14 明显下降，说明 I/O/缓存是重要变量；
   若不变，说明是 CPU/访问次数。
7. **打包原型**：选 c9 做「每顶点一条记录」原型，c5 做「邻接数组」原型，同轮 A/B；
   预期 c9 存储部分 -40%~-60%、c5 -30%~-50%，否则本文估算偏乐观。

---

## 7. 给 GPT 的问题清单

请以「图数据库存储引擎 + 查询执行」专家身份，基于本文给出的事实做独立判断；不要假设本文
未给出的实现细节。可以指出事实错误或估算不合理之处。

1. **性能包线**：基于 §2 的布局和 §5.2 的 per-access 成本，你认为这套 KV 存储在 LDBC
   SNB Interactive 这类 workload 上，相对 neo4j 的合理性能区间是多少？请按
   「点查/short、小 complex、访问密集 complex、写」四类给出范围，并说明假设。
2. **per-access 是否合理**：1–2 µs/edge、1.8 µs/（message 的 label+property 读），对
   WiredTiger B-tree + 逐行解码 + 协程派发来说是正常水平，还是明显有可去掉的引擎开销？
   如果是后者，最可能在哪一层？
3. **计划 vs 存储**：在 c5（2.8×）、c9（8×）、c14（21×）中，你认为各自有多少比例是
   计划/算子、多少是存储访问？需要什么实验才能把两者干净分开？
4. **下一步优先级**：如果只能做一件事，你选
   (a) 每顶点打包记录（labels + 热点属性）、
   (b) 邻接数组/独立 adjacency 表、
   (c) 算子批处理/向量化解码、
   (d) CBO/度数统计、
   (e) cache/压缩调优？
   请给出排序、预期收益和风险，并说明为什么。
5. **能否到 2×**：在不改「KV + B-tree」范式的前提下，c5/c9/c14 有可能做到 ≤2× neo4j 吗？
   如果可能，必须满足哪些条件？如果不可能，现实的地板是多少？
6. **1× 是否现实**：本文认为「全负载稳定 1× 不现实；非访问密集的多数查询 1.3–2× 是现实目标，
   但 c9/c14 仍在 3.5–8×」。你同意吗？有没有被忽略的架构选项（例如内存邻接缓存、
   列式属性、delta 存储、多版本快照）？
7. **测量设计**：给定 c9 旧基线无效、跨轮 CPU 频率漂移、c1 数据差异这些坑，
   你会如何设计一轮能区分「存储 vs 计划」的 A/B？需要哪些计数器/统计？
8. **便宜的大收益**：有没有本文没列出的低成本改动（索引、页大小、压缩、prefetch、
   huge pages、WT 配置、批大小、协议层）可能显著改变排名？
9. **c14 专项**：24 次 PC 展开约 280 ms，neo4j 17.7 ms。结合 §5.5 的计划，你判断其中
   多少是「PC 计划冗余」、多少是「per-access 地板」、多少是「`b.id` 的 14-candidate 解码」？
   最安全的共享/去相关方案是什么？
10. **可证伪性**：本文哪些判断最可能错？你建议先做哪个实验来证伪它？
11. **c14 的 14-candidate / 索引失效**：附件计划显示 `person1.id`、`person2.id`、`b.id`
   都被解析成 14 个候选（所有带 `id` 的标签），端点走 `LabelScan` 而不是 `Person.id`
   索引，PC 的 `b.id` 每行检查 14 组候选。你认为最小且安全的修复顺序是什么：
   (a) 按变量标签裁剪 candidates，(b) 让 `IndexScan` 穿透 PE/Project，(c) 两者都做？
   各自可能回归哪些查询/语义？

---

## 8. 附录

### 8.1 数据规模与行数估算（非实测）

| 项 | 数量 | 说明 |
|---|---:|---|
| 顶点 | 327,588 | 实测 |
| 边 | 1,477,965 | 实测 |
| `etype` 行 | ~1.48M | 1 行/边 |
| `edge_index` 行 | ~2.96M | 2 行/边（N=0） |
| `eprop` 行 | ~1.0–1.5M | 仅有属性的边（估算） |
| `vertex_data` 标签行 | ~0.4–0.6M | 每顶点 1–3 个标签（估算） |
| `vertex_data` 属性行 | ~1.2–1.8M | 每顶点 2–10 个属性（估算） |
| `label_fwd` 行 | ~0.4–0.6M | 每顶点每标签 1 行 |
| 合计 | **~7–9M 行** | 量级估算 |
| 原始 payload | **~0.2–0.3 GB** | key+value 粗算 |
| 加 WT 开销 | **~0.3–0.6 GB** | 页填充/内部节点/空闲空间（估算） |

> 工作集与 256 MB WT cache 的关系是 §6 估算的重要不确定性；OS page cache 可能兜底，
> 也可能不。建议用「加大 WT cache」实验确认。

### 8.2 复现命令（摘自文档）

```bash
# eugraph release server（与 A/B 相同参数）
./build-release/eugraph-server --thrift-port 9090 --bolt-port 7688 \
  --data-dir /path/to/eugraph-ldbc-sf01-bench \
  --compute-threads 8 --storage-io-threads 4 --thrift-io-threads 4 \
  --bolt-io-threads 1 --wt-cache-size-mb 256 --wt-evict-threads-max 4 \
  --wt-txn-sync fsync

# 计划/谓词
EUGRAPH_PLAN_DEBUG=1 EUGRAPH_PRED_DEBUG=1 ./build-release/eugraph-server ...

# 同机交错 A/B
python3 scripts/bench_ldbc_ab.py \
  --queries-dir /path/to/ldbc_snb_interactive_v1_impls/cypher/queries \
  --rounds 3 --warmup 1 --json /tmp/ldbc.json
```

### 8.3 相关文档

- [LDBC SNB SF0.1 对比测试](ldbc-snb-sf0.1-comparison.md)：最新全量表、口径、数据差异。
- [vertex_data 设计（feature 分支）](https://github.com/IronsDu/eugraph/blob/feat/storage-vertex-data/docs/storage/vertex-data-design.md)：邻接缓存、N=64/N=0 实测、导入回归。
- [KV 编码](../storage/kv-encoding.md)：所有表的 key/value 格式。
- [c5/c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md)：阶段计时、LM、批处理。
- [c14 锚点分析](../query/pattern-comprehension-anchor-label-scan.md)：80.5 s→0.295 s 的过程、
  24 次 PC 的剩余成本。
- [complex-3 复盘](query-performance-optimization-notes.md)：per-access/计划归因方法、
  neo4j PROFILE 对照、GDB 采样。

### 8.4 三条重点查询的简化形状

```text
c9: person -[:KNOWS*1..2]- friend
    friend <-[:HAS_CREATOR]- message
    WHERE message.creationDate < max
    RETURN friend, message  ORDER BY date DESC, id  LIMIT 20
    （53,192 条 message；7,706 条通过日期过滤；输出 20 行）

c5: person -[:KNOWS*1..2]- friend
    friend <-[:HAS_MEMBER]- forum
    OPTIONAL { forum <-[:CONTAINER_OF]- post <-[:HAS_CREATOR]- creator
               WHERE creator IN friends }
    RETURN forum, count(...)  ORDER BY ...  LIMIT 20
    （8,814 个 forum；89,415 条 post；creator 过滤后 262 条）

c14: allShortestPaths(person1 -[:KNOWS*]- person2) AS r
     for each r: 2 个 pattern comprehension（× 12 条 r = 24 次）计算端点间权重
     （4 行输出；约 280 ms 是 Comment/Post 的实际展开）
```

> 这三条是「访问密集」的代表：c9 是 53k 条 message 的 HAS_CREATOR 展开 + 日期读；c5 是 89k 条
> post 的 CONTAINER_OF/HAS_CREATOR 展开与 creator 过滤；c14 是 24 次 Comment/Post 模式展开。
> 它们的共同点是**逻辑行数由查询语义决定，减少空间有限**，所以 per-access 成本直接决定最终比值。

---

> **给评审者的最后一句话**：本文的核心不是「我们做得对/错」，而是想把「存储结构的上限」
> 和「当前实现的可修项」分开。请优先挑战 §5.2 的 per-access 数字和 §6.2 的区间估算；
> 如果这两个估算偏了，整个优先级排序都要改。
