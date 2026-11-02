# LDBC SNB Interactive SF0.1 对照（最新状态）

> 只保留当前结论：可运行性、最近一次有效耗时，以及剩余性能缺口的原因/思路。
> 历史轮次的过程数据已删除；需要复现时以本文的查询参数和测量口径为准。

## 1. 口径

- 数据：LDBC SNB Interactive **sf0.1**，CsvComposite 副本。eugraph 于 2026-10-31 用当前
  loader 重新导入（loader 行级标签写入行为变更后，旧数据目录不再可用）；neo4j 于
  2026-11-01 用同一份 CSV **重新导入干净库**。数据目录
  `eugraph-data-sf01-composite-20261031`；schema 为
  `docs/program/design/ldbc-sf01-composite.schema.json`。两边完整计数一致：
  327,588 顶点 / 1,477,965 边；Person 1,528、Message 286,744（Post 135,701 / Comment
  151,043）、Forum 13,750、Tag 16,080、TagClass 71、Place 1,460（City 1,343 /
  Country 111 / Continent 6）、Organisation 7,955（Company 1,575 / University 6,380）。
- **neo4j 干净导入**：`neo4j-admin database import full`，`--id-type=INTEGER`；
  CsvComposite 的 `Person.language` 映射为 `speaks:STRING[]`、`email` 保持
  `STRING[]`、日期为 `LONG`；`Place/Organisation` 的 `type` 列写成首字母大写的
  `:LABEL`（City/Country/Continent、Company/University），`Post/Comment` 加派生
  `Message` 标签；边属性 `creationDate/joinDate` 为 `LONG`，`classYear/workFrom` 为
  `INT`。导入结果与 eugraph 的 14 个标签、15 种边的计数逐项相等（例如 KNOWS 14,073、
  HAS_TAG 290,118、HAS_CREATOR 286,744、REPLY_OF 151,043）。
- **neo4j 实例**：本地独立实例 Neo4j 5.26.32，Bolt `127.0.0.1:8687`，auth disabled，
  数据目录 `/home/dodo/code/fuck/neo4j-local/data`；系统 `7687` 的旧库未改动。
- 环境：eugraph 为 **Release**（`build/release`，`-O3 -DNDEBUG`）；8 compute /
  4 storage IO / 1GB WT cache / `--wt-txn-sync none`（读查询）。neo4j：1GB heap /
  512MB pagecache。update 未在本轮测量（无 update-stream fixture）。
- 客户端：官方 Bolt driver，每条查询独立连接。**id 类型**：eugraph 是 `INT64`，
  neo4j 导入为 `INTEGER`；`scripts/bench_ldbc_ab.py` 与
  `scripts/verify_ldbc_results.py` 的 `--neo4j-id-type` 默认都是 `int`，
  `string` 仅用于仍是字符串 id 的旧实例。
- 同机、同轮 eugraph / neo4j；**15 轮计时 + 3 轮 warmup，每轮两引擎各执行一次，取 min**。
  绝对值受 CPU 频率/负载影响（本轮峰值约 4.2 GHz），跨轮次只看同轮比值。
- 参数：complex/short 的 `personId=933`、`messageId=3`；complex-13/14
  `person1Id=933, person2Id=1129`（官方默认 ID 不在本地 sf0.1）。complex-1 的 `firstName`
  保持文件默认 `Jose`。`bench_ldbc_ab.py` / `verify_ldbc_results.py` 默认使用这组参数。
- **结果一致性**：`verify_ldbc_results.py` 跑全量 21 条，**21/21 语义一致**（c1/c12 的
  collect 列表按集合归一；c14 的 `ORDER BY pathWeight desc` 在 17.0 并列时两引擎输出
  顺序不同，属同多重集）。没有数据/结果差异，因此性能表可以按同数据对照解读。
- **索引前提**：两边都只建主键索引 + `Message(id)`：eugraph 为各主标签唯一 id 索引 +
  `idx_message_id`；neo4j 为 Person/Forum/Post/Comment/Place/Organisation/Tag/TagClass
  的 `id` 唯一约束 + `Message(id)` 索引。**不建 `Message.creationDate` 索引**，也不建
  `Country(name)`/`Person(firstName)` 等额外索引，保持访问路径对等。

**速度图例**（以同查询 neo4j 为基准，按 eugraph/neo4j 耗时比值）：

| 图标 | 含义 |
|---|---|
| 🟢 | eugraph 更快（<1×） |
| 🟡 | neo4j 更快，慢 1–3× |
| 🟠 | neo4j 更快，慢 3–5× |
| 🔴 | neo4j 更快，慢 5–10×（红） |
| 🔴🔴 | neo4j 更快，慢 10× 以上 |

各性能表按 eugraph/neo4j 比值升序排列：eugraph 相对最快（或差距最小）的在前，最慢的在后。

## 2. 可运行性与耗时

> **最新全量复跑（2026-11-01，干净同源数据，Release 8/4，id 两边均为 INTEGER）**：
> complex-5 **180.9 ms** vs neo4j 84.5 ms（2.1×）、complex-9 **128.7 ms** vs 27.6 ms（4.7×）、
> complex-12 **695.5 ms** vs 87.9 ms（7.9×）、complex-14 **144.1 ms** vs 15.95 ms（9.0×）。
> **2026-11-02 VLE 优化后**：complex-12 同机 5 轮交错 A/B 为 **158.4 ms** vs 95.4 ms（1.7×），
> 相对优化前基线的 min 735→152 ms；详见 §3.2。
> **21/21 查询结果语义一致**；c14 仅 `ORDER BY pathWeight desc` 在权重 17.0 并列时
> 输出顺序不同（同多重集，见 §4）。

### 2.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| short-4 | 0.75 | 1.14 | 🟢 快 1.5× |
| short-3 | 1.12 | 1.40 | 🟢 快 1.3× |
| short-5 | 0.79 | 0.95 | 🟢 快 1.2× |
| short-1 | 1.14 | 1.30 | 🟢 快 1.1× |
| short-6 | 1.12 | 1.06 | 🟡 约 1.0× |
| short-7 | 1.15 | 0.94 | 🟡 慢 1.2×（0 行） |
| short-2 | 4.07 | 2.00 | 🟡 慢 2.0× |

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-13 | 1.19 | 2.14 | 🟢 快 1.8×（shortestPath） |
| complex-8 | 3.29 | 2.54 | 🟡 慢 1.3× |
| complex-2 | 6.99 | 4.16 | 🟡 慢 1.7× |
| complex-7 | 5.81 | 3.05 | 🟡 慢 1.9× |
| complex-11 | 7.67 | 3.95 | 🟡 慢 1.9× |
| complex-5 | **180.9** | 84.5 | 🟡 慢 2.1× |
| complex-4 | 8.73 | 3.88 | 🟡 慢 2.3×（0 行） |
| complex-6 | 83.1 | 27.0 | 🟠 慢 3.1× |
| complex-1 | 23.2 | 6.49 | 🟠 慢 3.6× |
| complex-3 | 100.9 | 26.1 | 🟠 慢 3.9×（0 行） |
| complex-9 | **128.7** | 27.6 | 🟠 慢 4.7× |
| complex-10 | 52.6 | 9.49 | 🔴 慢 5.5× |
| complex-12 | **158.4** | 95.4 | 🟡 慢 1.7×（VLE 优化后；优化前 695.5） |
| complex-14 | **144.1** | 15.95 | 🔴 慢 9.0×（ORDER BY 并列，同多重集） |

> 每轮两引擎各一次，15 轮取 min、3 轮 warmup；绝对值受 CPU 频率影响，跨轮次只看同轮比值。
> c3/c4、short-7 两边都是 0 行，耗时只代表扫描/过滤成本。complex-12 的 VLE 优化见 §3.2；
> c14 的顺序差异只是 `pathWeight=17.0` 并列，不是结果差异。

### 2.3 Updates

> 本轮只跑 21 条读查询，**update 未重跑**（无 update-stream fixture）；下表为上一次有效测量，
> 保留作参考。update 的 begin/commit 现在已和读路径一样经 Storage IO pool 异步执行。

update 查询文件没有 `:param` 块（参数来自 LDBC update stream）。上一轮按 LDBC 语义从图里
合成合法参数：新增类语句查重后分配新 id；like/membership/friendship 先选尚无该关系的
person/target；每条语句执行 3 次取 min，eugraph/neo4j 按 E→N→N→E 交错，Release 8/4。
两边 `id` 都是 INTEGER，update-7 的原查询可直接执行。复现脚本：
[scripts/bench_ldbc_updates.py](../../scripts/bench_ldbc_updates.py)（单引擎，交错跑两次）。

| 语句 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| update-4（add forum） | **7.16** | 16.95 | 🟢 快 2.4× |
| update-7（add comment） | **7.44** | 17.80 | 🟢 快 2.4× |
| update-6（add post） | **7.58** | 17.47 | 🟢 快 2.3× |
| update-1（add person） | 26.00 | 31.25 | 🟢 快 1.2× |
| update-2（like post） | 6.84 | 7.89 | 🟢 快 1.2× |
| update-3（like comment） | 6.87 | 8.09 | 🟢 快 1.2× |
| update-5（add membership） | 7.02 | 7.80 | 🟢 快 1.1× |
| update-8（add friendship） | 6.95 | 7.76 | 🟢 快 1.1× |

> update-7 的 `MATCH (message:Message {id: $replyToPostId + $replyToCommentId + 1})`
> 右值原先是一个参数表达式，eugraph 的索引选择只认常量，退化为 286k Message 全扫描
> （332 ms）。Binder 对参数（已是 literal）的 `+`/`-`/`*` 做常量折叠后，谓词变成
> `id = <literal>`，走 `Message(id)` IndexScan；溢出/除零/字符串拼接不折叠，保持运行时语义。
>
> update-4/6/7 还有一个共同热点：`UNWIND $tagIds AS tagId MATCH (t:Tag {id: tagId})`
> 旧计划是 `CrossProduct + LabelScan(Tag)`，为每个 tag 扫 16,080 行；新规则识别
> `Unwind + CrossProduct + Filter(prop = unwind_var)` 后改成
> `Apply(left=Unwind, right=IndexScanValues)` 的逐行索引探测，保留重复 key 的多重性。
> update-1 的 tag 循环同样受益。两条优化叠加后 update-4/6/7 从 20–29 ms 降到 7 ms 量级。
>
> 早前本地 neo4j 是字符串 id 时，update-7 的表达式会做字符串拼接并 MATCH 0 行（no-op），
> 现已把本地库转成 INTEGER，表里的 neo4j 时间是真实插入。脚本对每条语句都做了效果校验
> （新节点/边必须存在），no-op 会直接报错。

### 2.4 2026-11-02 优化后全量复跑

三处改动（动态属性去拷贝、PC 邻居缓存、prefetch 快路径，见 §3.5–§3.7）后的全量
21 条读查询，同机交错 12 轮 + 2 warmup 取 min（neo4j 8687，eugraph Release 8/4）：

| 查询 | eugraph | neo4j | eugraph/neo4j |
|---|---:|---:|---:|
| short-4 | 0.74 | 0.77 | 1.0× |
| short-5 | 0.77 | 0.80 | 1.0× |
| short-1 | 0.99 | 0.79 | 1.3× |
| short-3 | 1.01 | 0.81 | 1.2× |
| short-6 | 1.30 | 0.92 | 1.4× |
| short-7 | 1.50 | 0.98 | 1.5×（0 行） |
| complex-13 | 1.07 | 1.09 | 1.0× |
| complex-8 | 3.30 | 2.17 | 1.5× |
| short-2 | 4.44 | 1.83 | 2.4× |
| complex-11 | 5.18 | 2.36 | 2.2× |
| complex-7 | 5.43 | 1.88 | 2.9× |
| complex-2 | 7.69 | 3.23 | 2.4× |
| complex-4 | 8.41 | 2.95 | 2.9×（0 行） |
| complex-1 | 22.68 | 4.56 | 5.0× |
| complex-10 | 52.71 | 7.61 | 6.9× |
| complex-6 | 69.72 | 28.68 | 2.4× |
| complex-14 | 94.96 | 15.72 | 6.0× |
| complex-3 | 100.31 | 26.71 | 3.8×（0 行） |
| complex-9 | 122.18 | 28.58 | 4.3× |
| complex-12 | 147.99 | 101.44 | 1.5× |
| complex-5 | 188.54 | 90.81 | 2.1× |

> 绝对值随 CPU 频率漂移（本轮峰值低于 §2 的 2026-11-01 轮），跨轮只比同轮比值。
> 与 §2 相比：c14 9.4×→6.0×、c6 3.1×→2.4×；c5/c9/c3/c10/c12 的比值基本不变，
> 与 §3.5–§3.7 的 A/B 一致。计划质量与剩余瓶颈的逐算子对照见
> [ldbc-plan-quality-analysis.md](ldbc-plan-quality-analysis.md)。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P1 | 🔴 complex-14 | 144.1 ms vs 15.95 ms（约 9.0×） | allShortestPaths 后两路 PC 的实际 Comment/Post 展开；同数据 4 条路径，结果一致 | PC 去相关/展开批量化；见 [pattern-comprehension-anchor-label-scan.md](../query/pattern-comprehension-anchor-label-scan.md) |
| P1 | 🔴 complex-10 | 52.6 ms vs 9.49 ms（约 5.5×） | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P1 | 🟠 complex-9 | 125.2 ms vs 29.7 ms（约 4.2×） | 属性 coalesce 探测已按 id 排序；剩余是 53k 条 `HAS_CREATOR` 的 per-edge 成本 | 展开侧共享 cursor / 目标属性过滤下推；见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P1 | 🟠 complex-1 | 23.2 ms vs 6.49 ms（约 3.6×） | shortestPath 后还有 OPTIONAL/collect 属性展开 | profile 后处理；绝对值 ~23 ms，收益有限 |
| P1 | 🟡 complex-5 | 180.9 ms vs 84.5 ms（约 2.1×） | correlated `LeftJoin` 已按 512 行批处理；剩余是 89k posts/creator 的两次 Expand（per-edge 成本） | `dst IN list` 下推进 Expand，或从 174 个 friend 反向展开后与 forum 半连接 |
| P2 | 🟠 complex-6 | 83.1 ms vs 27.0 ms（约 3.1×） | 已通过标签前向扫描从 ~9× 降到 ~3.1×；剩余是 post/tag 展开与属性读取 | 继续 profile 该路径 |
| P2 | 🟡 complex-11 | 7.67 ms vs 3.95 ms（约 1.9×） | 多跳展开/属性读取，具体热点未拆 | profile 该路径 |
| P2 | 🟡 complex-7 | 5.81 ms vs 3.05 ms（约 1.9×） | 大结果集的 LIKES/HAS_CREATOR 展开与 `head(collect(...))` 聚合 | 排序/聚合前裁剪；绝对值 ~6 ms |
| P2 | 🟡 complex-12 | 158.4 ms vs 95.4 ms（约 1.7×） | VLE 逐跳 dispatch 已批量化（47,787→192）；剩余是 65,380 行 VLE 结果流 + 上层 4,086 行展开 | 后续可继续减少 VLE 结果到下游的中间行，见 §3.2 |

优先级按“差距 + 下一步可执行性”综合。complex-14 的绝对差距最大；complex-12 已从 P1
降到 P2，剩余优化空间是 VLE 结果到下游的中间行。

### 3.2 complex-12 VLE 批量化（2026-11-02）

`complex-12` 的 VLE `HAS_TYPE|IS_SUBCLASS_OF*0..` 在 65,380 行上产生了 47,787 次
`ioDispatches`。优化分两层：

1. **邻居标签批量解析**：原来 `hasDstLabels` 对每个邻居调用一次 `getVertexLabels`
   （一次 storage dispatch）；改为先对当前邻接表的邻居调用
   `getVertexLabelsBatch`，用 1,024 个一组填充 `dst_label_cache`。
2. **frontier 邻接预取**：某个 DFS frame 的所有 child 顶点不再各调用一次
   `scanEdges`，而是用 `scanEdgesBatch` 一次扫描并填充 `adjacency_cache`；DFS 顺序
   和每源边顺序保持不变，只是把跨线程访问合并。

同机交替 A/B（同一份 data copy、同一份 query、15 轮取 min）：

```text
baseline:  c12 min 735 ms   profile VLE 792 ms / ioDispatches 47,787
optimized: c12 min 152 ms   profile VLE  80 ms / ioDispatches 192
```

端到端交错 A/B（5 轮）为 **158.4 ms vs neo4j 95.4 ms（1.7×）**，结果 3/3 行一致；
`verify_ldbc_results.py` 全量 21/21 语义一致。VLE 相关 41 个单测和
`query_executor_tests` 604/604 通过。

neo4j profile 的对照：neo4j 的 `VarLengthExpand(All)` 约 65,689 行 / 131,307 dbHits，
没有 EuGraph 的跨线程 dispatch 成本；优化后 EuGraph VLE 的 dbHits 仍较高，但 dispatch
已不再是主瓶颈。

### 3.3 complex-9 属性探测顺序（2026-11-02）

GDB 采样显示 c9 的 `ProjectionExtract`/`Filter` 主要耗在
`SyncGraphDataStore::getVertexCoalescePropertiesBatch`。该函数对 Expand 产出的
message id 逐个 `search_near`；id 按 friend 分组，WT 页面访问跳跃。

改动：批量探测前按 `VertexId` 排序探测顺序，并复用 prefix 缓冲区；结果仍按调用方
输入下标回填，输出行序不变。新增 unsorted vids 的回归单测。

同机 A/B：

```text
c9 baseline:  min 134.6 / 132.7 ms
c9 sorted:    min 122.1 / 120.2 ms
```

端到端（5 轮）complex-9 约 **125 ms** vs neo4j 30 ms（4.2×），21/21 结果仍一致。
complex-5 的 A/B 在噪声范围内（约 195–210 ms），不宣称收益。

### 3.4 complex-14 采样结论（2026-11-02，PC 本身未改）

c14 当前约 166 ms（neo4j 18 ms）。PROFILE：`allShortestPaths` 约 16 ms；剩余主要是
两个 `PatternComprehensionApply` 分支：每个分支把 4 条路径的 12 条关系展开成
3,780–3,788 行，再做 `a.id/b.id` 端点过滤。GDB 采样热点是
`ExpressionEvaluator`（PC 端点谓词/`reduce`）与 `ExpandPhysicalOp`，不是单点存储
miss；没有低风险的单算子批量化可直接落地。

下一步应做 **PC 去相关**：从 path 的端点 id 直接 index-probe `a`/`b`，或把两个 PC
的公共路径展开合并；这需要 planner/算子层改动，不能靠局部阈值。

> 2026-11-02 后续：§3.5 已消除采样中 `ExpressionEvaluator` 的逐行整顶点深拷贝；
> PC 的两分支展开仍是剩余大头。

### 3.5 evaluator 动态属性读取去拷贝（2026-11-02）

§3.4 定位到的 `ExpressionEvaluator` 热点中，有一部分是**逐行整顶点深拷贝**，不需要
planner 改动即可消除：

- 根因：`evalDynamicPropertyRef` 对每个 `VertexValuePtr` 执行
  `mk<VertexValue>(*held)`（`eval_property.cpp` 旧 335 行），深拷贝整份
  `properties`（`unordered_map<LabelId, vector<optional<PropertyValue>>>`，Person 的
  `speaks`/`email` 还是 `vector<string>`）；拓扑顶点（`startNode`/`endNode`）还会把
  prefetch 缓存里的 `Properties` 再拷一次。c14 的 PC 谓词每行有 4 个属性引用
  （`a.id`/`b.id`/`startNode(r).id`/`endNode(r).id`），3,788 行 × 2 个分支。
- 证据：GDB 全线程采样在 c14 compute 线程上命中
  `evalDynamicPropertyRef → _Hashtable::_M_assign`；PROFILE 的 Filter self CPU 为
  14.7 ms/分支。同一二进制内做语义等价改写（`a.id = startNode(r).id` →
  `a = startNode(r)`，结果逐行一致）得到 c14 **158.9 → 121.8 ms** 的上界。
- 改动：动态属性求值改为**只读视图**——owned `properties` 优先；拓扑顶点直接读
  `prefetched_vertex_data_`；`__anon__` 仅从缓存读取，不再合并进副本。
  `startNode`/`endNode` 对 `CONSTANT` 边列只合成一次端点（PC apply 的常见形态）。
- 同机交错 A/B（15 轮 + 3 warmup，取 min；两棵二进制各用一份同源 data copy，
  交替顺序）：

```text
query        base min   opt min   opt/base
complex-14    158.19     133.72     0.845
complex-12    155.98     146.46     0.939
complex-5     199.41     191.09     0.958
complex-6      90.43      88.57     0.979
complex-3      99.10      96.99     0.979
complex-9     119.27     119.64     1.003
short-2         3.92       3.91     0.997
complex-10     51.39      52.67     1.025
complex-1      22.94      23.58     1.028
```

  c14 的 Filter self CPU 从 14.7 ms/分支降到 8.4–8.9 ms/分支；c10/c1 的 +2~3% 与 p50
  同向但绝对差 ~1 ms。把两份 data copy 对调后复测（25 轮），c10/c1 回到 +1.0%/+1.2%、
  c6 为 −1.5%，说明这部分主要是进程/数据副本的局部性，而非改动回归；结果逐行一致
  （9/9 查询 `same=True`）。
- 验证：新增 9 个用例（6 个 evaluator 单测 + 3 个 executor 端到端）；`query_executor_tests`
  **607/607**；`ctest --preset debug` **1435/1435**（含 TCK）；`verify_ldbc_results.py`
  **21/21** 语义一致。
- 剩余：c14 仍有约 134 ms vs neo4j 14.6 ms，瓶颈回到两个 PC 分支各 3,788 行的
  Expand/PE（§3.4 的 PC 去相关方向）；c6 的 Filter self CPU 仍约 25 ms，GDB 显示是
  `prefetch` 的全列/全行 Value 遍历（下一步候选）。

### 3.6 c14 PC 相关子计划的邻居缓存（2026-11-02）

c14 的 PC 按「每条路径关系 × 两个端点」执行：4 条路径 12 条关系，去重后只有 8 个人，
`CorrelatedVertexProbe` 却发射 24 个锚点，同一个人（933/1129 等）的
`a←HAS_CREATOR←comment` 展开被重复 4 次；两个 PC（`→:Post` / `→:Comment`）又把相同
前缀各跑一遍。PROFILE/GDB 显示这些重复 expand 的存储等待是 c14 的主要成本。

改动（`ExpandPhysicalOp`）：

- 每个 Expand 实例增加有界邻居缓存：键 = `src_vertex_id`，值 = **经 `dst_label_ids_`
  过滤后**的邻居（dst/edge_id/label/seq/physical_out），保持原扫描顺序。
- 只在 `read_only && correlated_subplan_depth > 0`（PC right subplan）启用；
  `dst_bound`/`edge_bound`/allowed-filter/undirected 拆分路径不启用。
- 命中直接复用邻居，跳过 `scanEdgesBatch` 与 `getVertexLabelsBatch`；未命中沿用原
  扫描路径，过滤后写入缓存；命中/未命中混合时按原 edge-major 顺序重组，输出行序不变。
- 上限 64K 边 / 64K 源；超限停用（保留已有条目），避免无重复的大扫描占内存。
- `read_only` 在 `planBound`/`planChosen` 入口用 `collectOpSlots(..., has_write)` 计算；
  写语句一律不缓存，缓存正确性依赖只读语句的单一 WT 快照。

同机交错 A/B（15 轮 + 3 warmup，min；两棵二进制、两份同源 data copy，正常/对调各一轮）：

```text
query        base min   opt min   opt/base     (对调 data copy)
complex-14    153.96      94.34     0.613     149.82 → 95.17（0.635）
complex-5     203.57     200.36     0.984     202.20 → 207.96（1.028）
complex-9     127.14     124.07     0.976     128.08 → 128.48（1.003）
complex-6      93.30      91.11     0.977      90.29 →  90.66（1.004）
complex-10     54.06      54.85     1.015      57.22 →  51.85（0.906）
```

c14 稳定 **−36~39%**（约 154 → 95 ms），与 neo4j 14.6 ms 的差距从 ~9.4× 收窄到 ~6.5×；
其余查询在噪声内。结果逐行一致。

验证：新增 `PatternComprehensionReusesCorrelatedAnchorExpansions`（断言结果 +
`ExpandPhysicalOp::cacheHitCount() > 0`，旧代码上该断言必失败）；`query_executor_tests`
**608/608**；`ctest --preset debug` **1436/1436**（含 TCK）；`verify_ldbc_results.py` **21/21**。

剩余：命中的 2/3 已去掉，未命中的 1/3 与两个 PC 的 compute（Filter/PE）仍是 ~95 ms 的
主体；进一步需要合并两个 PC 的公共前缀（planner/CSE）或做 PC 去相关 join。

### 3.7 evaluator prefetch 遍历快路径（2026-11-02）

- 根因：`ExpressionEvaluator::prefetch` 对每个 chunk 的**每一列 × 每一行**做
  `input.getValue(c, r)` 再 `scan`，只为找出裸顶点/运行时标签；c6 的 Filter 输入
  11,538 行 × 11 列，其中大部分是 INT64/STRING/EDGE 标量列。GDB 采样热点落在
  `prefetch → _Hashtable::_M_emplace_uniq`，PROFILE 的 Filter self CPU 约 25 ms。
- 改动：按列类型跳过不可能包含实体的标量列（BOOL/INT64/DOUBLE/STRING/时间/NULL/
  topology ref）；VERTEX/EDGE/LIST/MAP/PATH 的 FLAT 列直接读 typed buffer；只有
  CONSTANT/DICTIONARY/ANY 走 Value 泛化路径。`note_labels` 只收集当前 `label_defs`
  快照里不存在的标签（DDL 中途新建的），避免每行构造标签 set。
- 同机交错 A/B（15 轮 + 3 warmup，min；正常/对调 data copy 各一轮）：

```text
query        base min   opt min   opt/base     (对调 data copy)
complex-6      85.16      69.11     0.812      89.70 → 66.59（0.742）
complex-10     52.19      51.43     0.985
complex-14     90.69      89.60     0.988
complex-9     126.39     128.53     1.017      120.11 → 120.73（1.005）
complex-5     192.66     192.77     1.001
complex-3      96.88     100.70     1.039       95.79 →  96.76（1.010）
```

  c6 稳定 **−19~26%**（85→69 ms），其 Filter self CPU 从 ~25 ms 降到 2.2 ms；
  其余查询在噪声内。结果逐行一致。
- 验证：`query_executor_tests` **608/608**、`ctest --preset debug` **1436/1436**
  （含 TCK）、`verify_ldbc_results.py` **21/21**。

### 3.1 通用方向

1. **边邻接局部性**：低 degree 展开的主要成本仍是全局 `edge_index` 的 per-source 定位，
   可考虑按 `(edge_label, direction, src/dst)` 建邻接表，或让同 source 的连续展开复用 cursor。
2. **相关子计划下推**：complex-14 已完成 PC anchor 的 `LabelScan` + `CorrelatedVertexProbe`；
   剩余是让端点 `id` 走运行时索引 seek、或对 PC 去相关。该方向对非 LDBC 查询同样适用。
3. **属性批量/预取**：表达式求值已不在现场做 IO；裸顶点/`startNode` 等由算子边界
   `prefetch` 批量加载。path 元素继续按批预取，避免逐元素往返。
4. **dispatch 批量化**：complex-12 的 VLE 已把「逐邻居标签查询 + 逐 child 邻接扫描」
   合并为 batch（47,787→192 dispatch）；后续可在更大 frontier 上继续复用此模式。
5. **内存**：shortestPath/allShortestPaths 的 BFS 路径枚举有 100k 条上限，超过抛明确错误；
   sf0.1 未触发 OOM，4G cgroup 下无失控。

## 4. 本轮验证

- **eugraph Release**：`build/release`，`CMAKE_BUILD_TYPE=Release`，`-O3 -DNDEBUG`；
  8 compute / 4 storage IO / 1GB WT cache / `--wt-txn-sync none`（读查询）。
- **neo4j**：本地独立实例 5.26.32，Bolt `127.0.0.1:8687`，1GB heap / 512MB pagecache；
  2026-11-01 用同一份 CsvComposite 重新导入干净库（`neo4j-admin database import full`，
  `--id-type=INTEGER`），没有历史 update 测试边。
- **数据等价**：eugraph 与 neo4j 均为 327,588 顶点 / 1,477,965 边；14 个标签、15 种边
  的计数逐项相等（Person 1,528、Message 286,744、City 1,343、Country 111、
  Company 1,575、University 6,380、KNOWS 14,073、HAS_TAG 290,118、HAS_CREATOR 286,744、
  REPLY_OF 151,043 等）。两边只建主键索引 + `Message(id)`，不建
  `Message.creationDate` 或额外的 name 索引。
- **结果验证**：
  `python3 scripts/verify_ldbc_results.py --neo-uri bolt://127.0.0.1:8687 ...`
  跑全量 21 条，**21/21 语义一致，差异/错误 0 条**（退出码 0）。c1/c12 的 collect 列表
  按集合归一；c14 在 `ORDER BY pathWeight desc` 的权重 17.0 并列时顺序不同，按同多重集
  接受。两引擎的 `id` 都是 INTEGER；`verify_ldbc_results.py` 的
  `--neo4j-id-type` 默认 `int`。
- **性能**：同轮 15 轮 + 3 轮 warmup，两引擎交替执行、各取 min；结果见 §2。
- **代码验证**（提交 `10f4aab1`）：`query_executor_tests` **603/603**、
  `index_e2e_tests` 56/56、`rpc_integration_tests` 28/28（4 个网络 IO worker）、
  `io_scheduler_tests` 6/6、`query_stream_hop_tests` 4/4；分组跑完 `ctest` **1421/1421**。
- **全量 TCK**：**3959 scenarios / 3906 passed / 52 undefined / 1 known failure**（`Temporal2 [6]` tzdata）。
- 本轮不是同代码 base/opt A/B：主要变化是 loader 行级标签重导入 + compute/IO 分离后的
  真实跨线程 dispatch。同轮比值与 §2 的 min 是本轮判定口径。
