# LDBC SNB Interactive SF0.1 对照（最新状态）

> 最新测量：2026-11-08，eugraph `d4d54f36`（c5 身份需求 + c6 tag reorder + D1 回退 + PC 批量化），Release。
> 只保留当前结论：可运行性、最近一次有效耗时，以及剩余性能缺口的原因/思路。
> 历史轮次的过程数据已删除；需要复现时以本文的查询参数和测量口径为准。

## 1. 口径

- 数据：LDBC SNB Interactive **sf0.1**，CsvComposite 副本。eugraph 于 2026-11-03 用当前
  loader 重新导入（含 `vertex_summary` 表）：数据目录 `eugraph-data-summary2-sf01`；
  schema 为 `docs/program/design/ldbc-sf01-composite.schema.json`。旧目录
  `eugraph-data-sf01-composite-20261031` 仍可打开，但缺摘要行、读路径全部回退（摘要表的
  编码与失效规则见 [kv-encoding.md](../storage/kv-encoding.md#顶点摘要vertex_summary)）。neo4j 于 2026-11-01 用同一份 CSV
  **重新导入干净库**。两边完整计数一致：
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
- 环境：eugraph 为 **Release**（`build/release`，`-O3 -DNDEBUG`，2026-11-04 重新构建）；
  8 compute / 4 storage IO / 1GB WT cache / `--wt-txn-sync none`（读查询）。neo4j：
  1GB heap / 512MB pagecache。update 未在本轮测量（无 update-stream fixture）。
  第二轮增量优化后的同轮环境 CPU 约 **3.4 GHz**（第一轮稳态约 4.3 GHz）；两引擎绝对值
  都同向变慢，跨轮不可直接比绝对值，只看同轮比值与同二进制 A/B。
- 客户端：官方 Bolt driver，每条查询独立连接。**id 类型**：eugraph 是 `INT64`，
  neo4j 导入为 `INTEGER`；`scripts/bench_ldbc_ab.py` 与
  `scripts/verify_ldbc_results.py` 的 `--neo4j-id-type` 默认都是 `int`，
  `string` 仅用于仍是字符串 id 的旧实例。
- 同机、同轮 eugraph / neo4j；**15 轮计时 + 3 轮 warmup，每轮两引擎各执行一次，取 min**。
  本轮先做 5 次全量预热（覆盖 neo4j 的 page cache / JIT 与 CPU 升频），正式表取第 6 次
  全量；另保留“启动后首轮”作为与上一轮同协议的对照（§2.4）。绝对值受 CPU 频率/负载
  影响（本轮稳态占用 3.5–4.3 GHz，启动时曾低至 1.9 GHz），跨轮次只看同轮比值。
- 参数：complex/short 的 `personId=933`、`messageId=3`；complex-13/14
  `person1Id=933, person2Id=1129`（官方默认 ID 不在本地 sf0.1）。complex-1 的 `firstName`
  保持文件默认 `Jose`。`bench_ldbc_ab.py` / `verify_ldbc_results.py` 默认使用这组参数。
- **结果一致性**：`verify_ldbc_results.py` 于 2026-11-04 重跑全量 21 条，**21/21 语义一致**
  （c1/c12 的 collect 列表按集合归一；c14 的 `ORDER BY pathWeight desc` 在 17.0 并列时
  两引擎输出顺序不同，属同多重集）。没有数据/结果差异，因此性能表可以按同数据对照解读。
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

> **最新全量复跑（2026-11-08，干净同源数据，Release 8/4）**：21 条读查询、同机交错
> **15 轮 + 5 warmup** 取 min；neo4j 为 `neo4j-local` 干净实例（Bolt 8687，327,588
> 顶点 / 14,073 条 KNOWS），与 eugraph 逐项计数一致。c14 仅 `ORDER BY pathWeight desc`
> 在权重 17.0 并列时输出顺序不同（同多重集，见 §4）。
> **口径**：主表为全量预热后的稳态；第一轮同协议对照见 §2.4，增量优化同二进制 A/B 见 §2.5。
> 绝对值受 CPU 频率影响，跨轮次只看同轮比值。

### 2.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| short-4 | 0.71 | 0.62 | 🟡 慢 1.2× |
| short-5 | 0.73 | 0.53 | 🟡 慢 1.4× |
| short-3 | 1.00 | 0.62 | 🟡 慢 1.6× |
| short-1 | 0.98 | 0.60 | 🟡 慢 1.6× |
| short-6 | 1.07 | 0.62 | 🟡 慢 1.7× |
| short-7 | 1.19 | 0.59 | 🟡 慢 2.0×（0 行） |
| short-2 | 2.61 | 1.22 | 🟡 慢 2.1× |

> 本轮 short 合计 **8.3 ms vs 4.8 ms（1.72×）**。短查询仍在亚毫秒到 2 ms：eugraph 每查约
> **0.3–0.5 ms 固定开销**（Bolt/执行器启动/线程切换）；neo4j 全量预热后下限约 0.5–0.7 ms。

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-13 | 0.99 | 0.90 | 🟡 慢 1.1×（shortestPath） |
| complex-6 | 33.02 | 25.34 | 🟡 慢 1.3×（tag reorder 后） |
| complex-12 | 119.57 | 88.43 | 🟡 慢 1.4×（VLE） |
| complex-2 | 4.30 | 2.44 | 🟡 慢 1.8× |
| complex-10 | 18.30 | 10.31 | 🟡 慢 1.8×（PC 批量化后） |
| complex-11 | 4.88 | 2.65 | 🟡 慢 1.8× |
| complex-8 | 3.19 | 1.57 | 🟡 慢 2.0× |
| complex-5 | **200.35** | 85.87 | 🟡 慢 2.3×（89k posts × 2 expands） |
| complex-4 | 5.60 | 2.17 | 🟡 慢 2.6×（0 行） |
| complex-3 | **69.42** | 26.30 | 🟡 慢 2.6×（0 行） |
| complex-9 | **73.20** | 27.12 | 🟡 慢 2.7×（53k coalesce） |
| complex-7 | 5.52 | 1.77 | 🟠 慢 3.1× |
| complex-1 | 11.24 | 3.57 | 🟠 慢 3.1× |
| complex-14 | **78.64** | 13.80 | 🔴 慢 5.7×（PC 公共前缀，ORDER BY 并列同多重集） |

> 本轮为全量预热后的稳态：**15 轮计时 + 5 轮 warmup**，每轮两引擎各一次，取 min。
> complex 合计 **628.2 ms vs 292.2 ms（2.15×）**；加 short 后 **636.5 ms vs 297.1 ms（2.14×）**。
> c3/c4、short-7 两边都是 0 行，耗时只代表扫描/过滤成本。c14 仅 `ORDER BY pathWeight desc`
> 在权重 17.0 并列时输出顺序不同（同多重集）。
>
> 与上一轮（2026-11-04）相比，主要变化是 c6 tag-anchored reorder（63.2→33.0 ms）、
> PC 批量化（c10 40.4→18.3 ms）和 D1 回退（c14 76.6→78.6 ms，噪声内；c10 不再受
> labels-only PE 额外读取影响）。c14/c5/c9/c3 仍是剩余主缺口；c9/c3 在无索引口径下
> 接近存储地板。

### 2.3 Updates

> 2026-11-04 轮次只跑 21 条读查询，**update 未重跑**（无 update-stream fixture，且写操作会
> 污染读基准库）；下表为上一次有效测量，保留作参考。update 的 begin/commit 已和读路径一样
> 经 Storage IO pool 异步执行。

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

### 2.4 首轮同协议对照与稳态口径

同一份二进制、同一份数据，按“启动后首轮 + 3 warmup”再取一组（与上一轮 2026-11-03 的
协议一致），与 §2.1/§2.2 的稳态表对比：

| 口径 | complex eugraph | complex neo4j | 比值 | short eugraph | short neo4j | 比值 |
|---|---:|---:|---:|---:|---:|---:|
| 2026-11-03 文档（上一轮） | 702.2 | 314.8 | 2.23× | 12.16 | 10.37 | 1.17× |
| 2026-11-04 首轮（启动后） | 633.5 | 299.9 | 2.11× | 10.36 | 9.62 | 1.08× |
| 2026-11-04 第一轮稳态（增量前） | 637.0 | 269.8 | 2.36× | 9.01 | 4.65 | 1.94× |
| 2026-11-04 第二轮稳态（主表） | 696.1 | 304.1 | 2.29× | 10.04 | 5.33 | 1.88× |

- eugraph 的 complex 总量在第一轮首轮/稳态间只差 **0.6%**（633.5 vs 637.0），说明
  增量前的代码绝对性能稳定；相对 2026-11-03 的 702.2 ms 下降约 **9.3%**。
- 第二轮（审计项落地）在 3.4 GHz 下测得 696.1 ms，neo4j 也升到 304.1 ms；两者同向变慢
  约 9–13%，**同轮比值 2.29× 反而略好于第一轮的 2.36×**。绝对值的升高不能当成本轮回归；
  增量代码的真实影响由 §2.5 的同二进制 A/B 判定。
- 短查询的“谁更快”依赖于 neo4j 的预热状态：首轮 eugraph 在 short-1/3/4/5 上领先或持平，
  稳态下 neo4j 反超。后续报告短查询应固定使用“全量预热后的稳态”口径，并注明 neo4j 是否
  完全预热。

### 2.5 增量优化同二进制 A/B（2026-11-04 第二轮）

为区分“代码变化”和“CPU 频率/数据副本差异”，用同一份数据复制出 `eugraph-data-ab`，
分别启动**增量前**与**增量后**两棵二进制（8/4、1GB WT cache），同机交错、每轮交换顺序，
15 轮取 min；重点查询再做 40 轮复核。下表为 `opt/base`（<1 表示增量后更快）：

| 查询 | base min (ms) | opt min (ms) | min 比值 | p50 比值 |
|---|---:|---:|---:|---:|
| complex-5 | 190.02 | 181.85 | **0.957** | 0.964 |
| complex-10 | 40.16 | 38.61 | **0.961** | 0.962 |
| complex-7 | 4.97 | 4.80 | **0.966** | 0.983 |
| complex-14 | 74.32 | 73.83 | 0.993 | 0.990 |
| complex-1 | 14.73 | 15.13 | 1.027 | 1.000 |
| complex-12 | 128.56 | 129.97 | 1.011 | 1.034 |

- **c5/c10/c7 稳定 −3~−4%**；c14/c1 基本持平；c12 的 +1~3% 与同机噪声同量级，未发现
  可复现的回归。
- 其余查询在 15 轮 A/B 中落在 ±8% 噪声内（c6 −2%、c9 +5%、c11 −3%、c8 −2% 等），
  不能据此宣称单查询收益。
- 增量代码的主要收益来自 **相关列注入 + join/apply typed copy + typed EQ/AND**；
  c14 的 Filter 大头是端点属性 prefetch，不是 EQ，所以 typed kernel 对 c14 只有中性影响。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距（稳态口径） | 已定位原因（2026-11-04 PROFILE） | 下一步思路 |
|---|---|---|---|---|
| P1 | 🔴 complex-10 | 40.35 ms vs 5.90 ms（6.8×） | 946 次按帖 `PatternComprehensionApply`（self 3.5 ms/946 次）；`CorrelatedSource` self 1.8 ms/946 行；HAS_TAG Expand 12.2 ms；PC 内 Aggregate 3.5 ms | 去相关为按组聚合/半连接；相关列注入已落地，下一步是 Apply 按 chunk 驱动 |
| P1 | 🔴 complex-14 | 76.64 ms vs 13.95 ms（5.5×） | 两个 PC 分支的 Filter self 6.6–7.3 ms（合计 ~13.8 ms），其中 120/15 行的端点 Filter 伴随 30k 次 prefetch；3,788 行的 Filter self 仅 0.66 ms | typed EQ/edge-unique 已落地但对 c14 中性；下一步是 PC 公共前缀/末级 `dst_bound` 绑定；见 §5.9 |
| P2 | 🟠 complex-1 | 15.53 ms vs 3.44 ms（4.5×） | `shortestPath` self 1.75 ms/674 次 `scanNear`；后处理 LeftJoin/Aggregate 各 ~3 ms incl | 绝对值 ~15 ms；profile 后处理 |
| P2 | 🟠 complex-4 | 8.90 ms vs 2.45 ms（3.6×，0 行） | 扫描/过滤成本；无结果也要全扫 | 低 ROI |
| P2 | 🟠 complex-7 | 5.86 ms vs 1.80 ms（3.3×） | 大结果集 LIKES/HAS_CREATOR 展开 + `head(collect(...))` 聚合 | 排序/聚合前裁剪；增量 A/B −3.4% |
| P2 | 🟡 short-2 | 4.55 ms vs 1.66 ms（2.7×） | 2 跳 + 固定开销；eugraph 每查约 0.3–0.5 ms 起跳 | 绝对值 ~5 ms；与短查询固定开销一并看 |
| P2 | 🟡 complex-9 | 79.21 ms vs 27.14 ms（2.9×） | PE coalesce self 20.6 ms / 53,192 次 `wtSearchNear`；剩余是 7.7k 行物化 | 无索引时接近存储地板；见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P2 | 🟡 complex-3 | 71.33 ms vs 25.71 ms（2.8×） | 52k message 的 label+`creationDate` 读取；0 行也全扫 | 同 c9：存储地板；日期索引需同 schema 评估 |
| P2 | 🟡 complex-11 | 5.35 ms vs 2.17 ms（2.5×） | 多跳展开/属性读取，具体热点未拆 | profile 该路径 |
| P2 | 🟡 complex-6 | 63.22 ms vs 26.99 ms（2.3×） | Expand self 5.5+4.4 ms（scan 188,893）；PE 3.1 ms；`:Post` 过滤后 17,739→11,538 行 | 延迟 label 过滤扩展到 `Expand→Expand` |
| P2 | 🟡 complex-5 | 184.90 ms vs 89.90 ms（2.1×） | 89k 边 Expand self 21.5+4.3 ms；Filter 15.1 ms；Aggregate 13.5 ms；Sort/LeftJoin ~7.7 ms | typed GROUP BY/join key；friends 下推/半连接；增量 A/B −4.3% |
| P3 | 🟡 complex-12 | 136.21 ms vs 99.52 ms（1.4×） | VLE self 20.4 ms（65,380 行）+ PE 12.6 ms + Filter 7.2 ms；dispatch 已批量化 | 减少 VLE 结果到下游的中间行，见 §3.2 |
| P3 | 🟡 complex-13 | 0.94 ms vs 0.77 ms（1.2×） | shortestPath，绝对值 <1 ms | 不投入 |
| P3 | 🟡 short-3/1/6/7/4/5 | 0.67–1.09 ms vs 0.54–0.68 ms（1.2–2.0×） | 亚毫秒固定开销（Bolt/执行器启动/线程切换），不随频率线性下降 | 单独立项，不属于 Value/evaluator 优化族 |

优先级按“差距 + 下一步可执行性”综合：complex-10/14 仍是绝对差距最大的两条；短查询的
差距已经从“值不值”变成“固定开销”，需要单独定位，不应混入 evaluator 优化。

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

### 3.8 shortestPath/allShortestPaths 的 BFS frontier 批量扫描（2026-11-02）

- 根因：`ShortestPathPhysicalOp::scan(vid)` 对 BFS frontier 的**每个顶点**发一次
  `scanEdges` dispatch。c1 的 PROFILE 有 **674 次 ioDispatches**（22 ms 里约 6–7 ms
  是 dispatch 开销）；c14 的 `allShortestPaths` 352 次。邻接缓存只消除重复访问，
  不改变“每个顶点一次跨线程 dispatch”的形态。
- 改动：新增 `scanBatch(frontier, dir)`，用一次 `scanEdgesBatch` 扫描整个 frontier
  （每个路径 label 一次），按源顶点分组回填为每条 frontier 顶点的邻居表；邻接缓存、
  每源边顺序、BFS 访问顺序与 path 构造顺序均保持不变。`shortestBidirectional` 的
  前向/后向 frontier 与 `allShortestPaths` 都改走批量路径。
- 同机交错 A/B（15 轮 + 3 warmup，min；正常/对调 data copy 各一轮）：

```text
query        base min   opt min   opt/base     (对调 data copy)
complex-1      22.45      15.31     0.682      21.99 → 14.53（0.661）
complex-14     90.90      84.23     0.927      94.50 → 89.52（0.947）
complex-13      0.97       0.93     0.959       0.98 →  0.87（0.891）
complex-10     53.17      51.40     0.967      54.34 → 52.76（0.971）
complex-12    162.54     150.53     0.926
short-2         4.14       3.93     0.950
```

  c1 稳定 **−32~34%**（22→15 ms），c14 再降 ~7%（allShortestPaths），c13/c10 小幅；
  结果逐行一致；§2.1/§2.2 的全量表已包含本改动（c1 13.65 ms、c14 85.10 ms）。
- 验证：`query_executor_tests` **608/608**、`ctest --preset debug` 全量（含 TCK）、
  `verify_ldbc_results.py` **21/21**。

### 3.9 c9 的 Message 标签过滤延迟到物化（2026-11-02）

- 根因：`(friend)<-[:HAS_CREATOR]-(message:Message)` 在 53,192 条边上触发
  “扫整张 `label_fwd_Message`（286,744 行）再过滤”的启发式，实测 ~26 ms。
- 改动：按 [deferred-label-filter-design.md](../query/engine/deferred-label-filter-design.md)
  把目标标签判定延迟到直接父节点的 `ProjectionExtract`：一次 `{vid}` 前缀扫描同时读标签与
  coalesce 候选属性，PE 按标签压缩输出行。仅对只读语句、宽标签（统计顶点数 ≥64K）启用；
  标签计数在图 open 时统计一次（sf0.1 74 ms）。
- 结构判据：c9 的 Expand `scanRows` **339,936 → 53,192**。
- A/B（15 轮，min；正常/对调数据副本）：c9 **130.2→110.2（−15.4%）**，对调
  134.7→110.4（−18.0%）；c5/c14 差异在同二进制 self-A/B 的 ~10% 噪声内；结果一致。
- 验证：新增 2 个测试；`query_executor_tests` 609/609；全量 ctest（含 TCK）；verify 21/21。

### 3.10 同族优化落地与剩余点（2026-11-04）

本轮分支已经把 evaluator scratch、typed 列访问/拷贝、aggregate typed 参数和 scan callback
`FunctionRef` 落地；第二轮又补了 typed 比较/逻辑、edge-unique、相关列注入、join/apply
结果列、DISTINCT digest 和属性 typed 写入。下表按“LDBC 收益 × 风险”列出**已落地**与
**仍未覆盖**的项；收益来自 §2.5 的同二进制 A/B 与 PROFILE（§3 表）。

| 状态 | 候选 | 落地 / 证据 | 结果 |
|---|---|---|---|
| ✅ | **typed EQ/NEQ + BOOL AND/OR** | `typedBoolLogicEval`/`typedBoolEquality`/`typedStringEquality` + INT64/DOUBLE `EqOp`/`NeqOp`；移除 AND/OR/XOR 的逐行 Value | 组合 A/B 中 c5/c10/c7 −3~4%（未逐项隔离）；c14 中性（其 Filter 大头是端点 prefetch） |
| ✅ | **`__edge_unique` 原生 kernel** | captureless `edgeUniqueBatch`，EDGE/EDGE_KEY 列直接读，复用 `unordered_set`，去掉每行 `vector<Value>`+set 分配 | c14 中性，说明它不是当前 PC 的主成本；保留为结构修复 |
| ✅ | **相关列 typed 注入** | `CorrelatedSource::setRowFromChunk` + 复用 `view_values_`；Apply/PCApply/LeftJoin/SemiJoin 不再每行建 `vector<Value>` | 组合 A/B 中 c10 −3.9%（未逐项隔离）；`CorrelatedSource` self 1.8 ms/946 行是直接证据 |
| ✅ | **join/apply 结果列平移** | CrossProduct/Apply/HashJoin/CorrelatedVertexProbe/LeftJoin 改用 `copyValueFrom` 优先 | 与 c5 −4.3% 合并计量 |
| ✅ | **DISTINCT typed digest** | `cellDigestColumn` 直接读 BOOL/INT64/DOUBLE/STRING/顶点列，避免逐列 Value | c5 `WITH DISTINCT` 路径；未单独 A/B |
| ✅ | **属性 typed 写入** | `setColumnFromPropertyValue` 用于 PE 的 LoadVertexProp/coalesce/LoadEdgeProp，标量/字符串不再中转 Value | c9/c3 存储受限，A/B 未见显著变化 |
| ⏳ | **Sort/Materialize/HashJoin 的 Row 物化** | HashJoin 输出列已 typed copy；Sort 全量 Row、Materialize Row 缓存、HashJoin key/右行仍是 `vector<Value>` | 需要独立的列式缓冲设计，未在本轮硬改 |
| ⏳ | **typed GROUP BY key** | Aggregate 仍每行构造 `Row` key | 中风险，依赖 group map 重构；未做 |
| ⏳ | **短查询固定开销** | Bolt/执行器启动/线程切换约 0.3–0.5 ms/查 | 不属于 Value/evaluator 族，单独立项 |

> 结论：本轮把**低风险、可复用**的同族项都落地了；同二进制 A/B 的稳定收益集中在
> c5/c10/c7（−3~4%），c14/c1 中性，c12 在噪声内。剩余三项（Sort/Materialize/HashJoin 的
> Row 模型、typed GROUP BY、短查询固定开销）都需要架构级改动/单独测量，不应为了“覆盖”
> 硬塞进本轮。

### 3.11 通用方向

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

## 4. 本轮验证（2026-11-04）

- **构建**：`cmake --build build/release --target eugraph-server -j12`，Release
  `-O3 -DNDEBUG`，commit `9dd10b24`；生成 `build/release/eugraph-server`
  （2026-11-04 20:35）。全量编译通过；17 条编译告警，无 error。
- **eugraph**：`--bolt-port 7688 --compute-threads 8 --storage-io-threads 4
  --wt-cache-size-mb 1024 --wt-txn-sync none`，数据目录 `eugraph-data-summary2-sf01`
  （2026-11-03 导入，含 `vertex_summary`）；327,588 顶点 / 1,477,965 边，Message
  286,744、KNOWS 14,073，与 neo4j 逐项一致。
- **neo4j**：`neo4j-local` 5.26.32，Bolt `127.0.0.1:8687`，1GB heap / 512MB pagecache，
  auth disabled；系统 7687 实例未用于本轮对照。
- **结果验证**：
  `python3 scripts/verify_ldbc_results.py --eg-uri bolt://127.0.0.1:7688 --neo-uri bolt://127.0.0.1:8687 --neo4j-id-type int ...`
  跑全量 21 条，**21/21 语义一致，差异/错误 0 条**（退出码 0）。c1/c12 的 collect 列表
  按集合归一；c14 在 `ORDER BY pathWeight desc` 的权重 17.0 并列时顺序不同，按同多重集
  接受。两引擎的 `id` 都是 INTEGER。
- **性能**：第一轮先做 5 次全量预热，第 6 次全量按 **15 轮 + 3 warmup** 取 min（§2.4）；
  第二轮审计项落地后，在两棵二进制、两份同源 data copy 上做同机交错 A/B（40 轮复核，
  §2.5），再用 eugraph vs neo4j 取 3 次全量、取最后一次稳态（主表，§2）。
- **测试状态**：第二轮改动后 `query_executor_tests` **614/614**、
  `evaluator_typed_tests` **34/34**（新增 4 个 typed EQ/string/BOOL/edge-unique 用例）、
  `data_chunk_tests` **20/20**；21/21 结果校验重跑通过。TCK 仍为分支记录
  **3930/3983**（唯一失败为既有 `Temporal2 [6]`）。**全量 TCK 本轮未重跑**。
- **测量口径**：第一轮不是同二进制 A/B；第二轮有同二进制 A/B（§2.5）。跨轮次绝对值和
  短查询胜负受 CPU 频率/neo4j 预热影响，只在同轮比值与 A/B 内解释。

## 5. 2026-11-03 / 11-04 改动与归因

> 本节只记录“为什么变快 / 下一步从哪来”的机制，详细编码规则见
> [kv-encoding.md](../storage/kv-encoding.md#顶点摘要vertex_summary)；完整 21 条耗时见 §2。

### 5.1 顶点摘要（storage）

`vertex_data` 里一个顶点的属性是**每属性一行**，LDBC message 平均约 6.6 行；c3/c9 的
coalesce 各要探测 5.2 万/5.3 万个 message。新增 `table:vertex_summary` 把 labels + 编码
≤32B 的属性合成一顶点一行，大属性只记 present-but-omitted：

- c3：98.3 → **70.5 ms**（52,475 次探测）；
- c9：110.2 → **77.0 ms**（53,192 次探测）。

（均为 8/4、15+3 全量口径；4/4 单查询 A/B 下约 62/67 ms。两个口径不混比绝对值。）
摘要是可丢弃缓存：`insertVertex` 写，任何属性/标签变更、`DROP LABEL`、`deleteVertex`
删；缺失/不完整/解码失败一律回退 canonical 行。回归测试见
`tests/test_graph_store_wt.cpp` 的 `Summary*` 用例。

### 5.2 `dst_bound` Expand 的精确邻接探测

相关模式 `(friend)-[:KNOWS]-(person)`（EXISTS / anti-join）里终点 `person` 已绑定，
原计划仍“扫完 friend 的全部邻接再过滤”。新增
`scanEdgesExact({src}{dir}{label}{neighbor})`：`edge_index` 前缀本身支持精确邻居，
一次 seek 替代一次邻接扫描。

- 仅当 `dst_bound_`、无 `edge_bound_` 且标签过滤是具体值（非 `nullopt`）时启用；
- c10 的 `NOT (friend)-[:KNOWS]-(person)`：7,532 条边扫描 → 每源一次精确 seek。

### 5.3 无向 Expand 的批量扫描

`direction=ANY` 的 Expand 原先被排除在批量路径外，因为 BOTH 方向混在一起会丢失物理方向。
现在批量路径显式跑 OUT / IN 两趟 `scanEdgesBatch`，再用 `stable_sort(src_row)` 还原
“同一源先 OUT 后 IN”的行序；自环在 IN 趟跳过（OUT 趟已产出）。

- c10 anti-join：364 次 dispatch（182 行 × 2 方向）→ 2 次；
- 缓存路径对 `ANY` 仍禁用（缓存不保存物理方向）。

### 5.4 SemiJoin / AntiSemiJoin 的批量相关执行

`SemiJoinPhysicalOp` 原先逐左行驱动右子计划；c10 的 anti-join 因此执行 182 次。现在与
`LeftJoin` 一样按 ≤512 行分批：`CorrelatedSource::setRows` 一次注入整批，右子计划只跑一次，
输出行的**关联列**按槽位取回，与左行的关联值做多重集匹配。

- 仅当关联列在右侧 schema 中存在且全部是顶点类型时启用；否则回退逐行路径
  （非顶点关联、嵌套列表等语义不变）；
- `EUGRAPH_SJ_BATCH=0` 可关闭，便于同二进制 A/B。

### 5.5 摘要标签 + 跨执行顶点缓存

- `getVertexLabelsBatch` 先点查摘要 labels；命中一次读完成，未命中回退 `{vid}{0x00}`。
  受益者：Expand 的 `dst_label_ids_` 过滤、VLE 邻居标签、c14 的 `REPLY_OF` 目标标签判定。
- `ProjectionExtract` 的 ConstructVertex 共享原先只跨“一个 chunk 的行”；相关 Apply 每个
  左行执行一次子计划，常量顶点（c10 的 `person`、c14 的路径端点）被反复构造。只读语句现在
  把 `vid -> VertexValuePtr` 缓存提升到算子实例（上限 8192），跨 `executeChunk()` 复用；
  写语句仍只用 per-chunk 缓存。
- c14：85.1 → **75.6 ms**；c10 的 per-post Apply（见 §5.7）仍是主体。

### 5.6 TCK 覆盖

`tests/tck/features-eugraph/vertex-summary.feature`（12 scenarios / 84 steps）覆盖：
CREATE 后属性读、`coalesce` 命中/缺失为 null、>32B 属性回退、`SET`/`REMOVE`/
`SET label`/`REMOVE label`/`DETACH DELETE` 失效、`MERGE` 转换、同一顶点多行时的逐行对齐、
`SET n.p = null`、大列表属性回退。

```bash
python3 tests/tck/run_tck.py --server-bin build/release/eugraph-server \
  --tck-bin build/release/tests/tck/tck_tests \
  --features tests/tck/features-eugraph --data-dir <磁盘目录>
# Total: 74 | Passed: 74 | Failed: 0
```

存储级还有 `SummaryIsInvalidatedByPropertyUpdate`、`SummaryIsInvalidatedByDropLabel`、
`SummaryTreatsLaterDeclaredPropertyAsNull` 三个直接覆盖失效不变量的用例。

### 5.7 仍待做

- **c10（6.8×，第二轮稳态）**：946 次“每帖一次”的 `PatternComprehensionApply`（self 3.5 ms/946 次）
  仍是主体；`CorrelatedSource` self 1.8 ms/946 行，说明每行 `vector<Value>` 注入已可测量。
  下一步：`size([x IN list WHERE <pattern>])` 解相关成按组聚合/半连接、相关列 typed 注入，
  或让 Apply 按 chunk 批量驱动右子计划。
- **c14（5.5×，第二轮稳态）**：两个 PC 分支的 Filter self 合计 ~13.8 ms，集中在 120/15 行的
  端点 Filter（伴随 30k 次 prefetch）；3,788 行的 Filter self 仅 0.66 ms。typed EQ 与
  `__edge_unique` 已落地但 A/B 中性，说明剩余大头是端点 prefetch/PC 计划；Expand 合计 ~8 ms。两个 PC 共享
  `a←HAS_CREATOR←Comment` 前缀；最大单点仍是“把 `b` 绑定到 `endNode(r)`”：
  `b.id = endNode(r).id` 在 `Person.id` 唯一索引成立时唯一确定 `b`，可让末级 Expand 走
  `dst_bound` 精确 seek，并把 3,788 行的端点 Filter 移到 120 行之后（未实施，风险在
  binder 的模式重排）。
- **c5（2.1×）/ c12（1.4×）**：c5 的 Expand（89k 边）与 Aggregate（89k 行分组）是主体；
  c12 的 VLE 20.4 ms + PE 12.6 ms 是主体。两者都不在 evaluator 的 Value 拷贝族里，
  需要先决定是否投入 typed GROUP BY / VLE 中间行裁剪。

### 5.8 2026-11-04 分支落地与复测

`perf/evaluator-scratch-reuse` @ `9dd10b24` 在 main #288 之上补齐了：

- `ExpressionEvaluator` 按求值深度分帧的 scratch、临时列池、单行 chunk 复用；
  list literal 从 O(R×N) 降为 O(N)；
- `Column` 的 typed 访问/拷贝（`getInt64`/`getDouble`/`getBool`、`copyValueFrom`）与
  `cell_access.hpp`（`columnCellEquals`/`copyColumnCell`/`columnVertexId`）；
- Aggregate 参数改为 chunk 级 typed 列 + `AggregateInput` 行视图，回调从 `std::function`
  改为 captureless 函数指针；去掉 `vector<vector<vector<Value>>>`；
- 同步扫描回调从 `std::function` 改为 `folly::FunctionRef`；Expand/SemiJoin 的 dst 过滤与
  右侧关联 key 改用 `columnVertexId`/扁平数组。

**效果**（跨轮次，非同一二进制 A/B）：eugraph complex 合计 **702.2 → 633.5/637.0 ms**
（约 −9.3%）；c10 −10.2%、c14 −8.4%、c5 −9.8%、c12 −14.2%、c9 −6.9%、c3 −5.0%、
c1 −12.8%、short-2 −24.2%（取稳态值；首轮因频率/预热差异略小）。c6 基本持平（−1.4%）、
c8 绝对差仅 0.35 ms，符合“存储/扫描受限，evaluator 不是主成本”的判断。短查询的 −22~−35% 主要来自 CPU 升频，不能全部归因于本分支。

**仍未做**：per-group batch aggregate update、typed GROUP BY key、Sort/Materialize/HashJoin
的整行 `Row` 物化、短查询固定开销；完整候选与证据见 §3.10 与
[value-copy-audit.md](../query/engine/value-copy-audit.md)。

### 5.9 2026-11-04 第二轮：审计项落地与 A/B

在 §5.8 之上补了 6 组同族改动：

- `evalBinaryOp`：INT64/DOUBLE `EQ/NEQ` typed kernel；BOOL `AND/OR/XOR` 三值逻辑
  typed kernel；STRING `EQ/NEQ` 的 FLAT/CONSTANT 快路径；修正 `typedBinaryEval`
  常量/向量分支对 BOOL 列的错误指针；
- `__edge_unique`：从 `scalarToBatch`（每行 `vector<Value>` + 新 `unordered_set`）改成
  captureless 列级函数，复用 set；
- `CorrelatedSource`：新增 chunk-row view，Apply/PCApply/LeftJoin/SemiJoin 不再为每个左行
  构造 `vector<Value>`；view 复用 `view_values_`，保持按实际 Value 推断列类型；
- CrossProduct/Apply/HashJoin/CorrelatedVertexProbe/LeftJoin 的结果列优先
  `copyValueFrom`；
- `DistinctPhysicalOp` 的 `cellDigestColumn` 直接读 typed 列（BOOL/INT64/DOUBLE/STRING/
  顶点 id），避免逐列 Value；
- `ProjectionExtract` 的 LoadVertexProp/coalesce/LoadEdgeProp 使用
  `setColumnFromPropertyValue`，标量/字符串属性不再中转 Value。

同二进制 A/B（40 轮，min/p50）：c5 **0.957/0.964**、c10 **0.961/0.962**、
c7 **0.966/0.983**、c14 0.993/0.990、c1 1.027/1.000、c12 1.011/1.034。其余查询在
±8% 噪声内。**结论：稳定收益集中在 c5/c10/c7 的 3–4%，c14 的剩余大头是端点
prefetch/PC 计划而非 EQ kernel；c12 的轻微正差在噪声内。**
