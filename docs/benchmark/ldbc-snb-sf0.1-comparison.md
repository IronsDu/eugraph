# LDBC SNB Interactive SF0.1 对照（最新状态）

> 只保留当前结论：可运行性、最近一次有效耗时，以及剩余性能缺口的原因/思路。
> 历史轮次的过程数据已删除；需要复现时以本文的查询参数和测量口径为准。

## 1. 口径

- 数据：LDBC SNB Interactive **sf0.1**。本轮的计时与结果复验都使用 **CsvComposite 副本**
  （`eugraph-ldbc-composite2`，由 `social_network-sf0.1-CsvComposite-LongDateFormatter` 导入，
  schema 为 `docs/program/design/ldbc-sf01-composite.schema.json`）：eugraph 327,588 顶点 /
  1,477,965 边，Person 1,528，Message 286,744；`email` 为 `STRING[]`，Person 的 `language`
  列按上游 `cypher/scripts/headers.txt` 映射为 `speaks:STRING[]`（`language` 是 Post 的属性）。
  neo4j 另外存在历史测试标签/边，但标准 Person/Message/Post/Comment/Forum/Tag 等计数与 eugraph 一致。
- neo4j 的 `LIKES.creationDate`、`WORK_AT.workFrom`、Person 日期的字符串属性已归一化为整数；
  eugraph 原本是整数。
- 环境：**Release**（构建目录 `build-release`，`-O3 -DNDEBUG`）；complex 8 compute /
  4 storage IO / 1GB WT cache。update 未在本轮测量（无 update-stream fixture）。
- 客户端：官方 Bolt driver，每条查询独立连接。**id 类型**：eugraph 是 `INT64`，
  本地 neo4j 已就地转为 `INTEGER`（327,796 个节点的 `id` 属性，随后重建 id 索引），
  两边都与参考实现 `cypher/scripts/import-to-neo4j.sh` 的 `--id-type=INTEGER` 对齐；
  读/写参数统一传整数。驱动侧 `id_type=long|string` 仍保留（上游默认 string），
  但本地对照不再依赖字符串 id。
- 同机、同轮 eugraph / neo4j；全量 21 条读查询 2 轮 warmup + 3 轮计时取 min（两轮再取 min）。
  绝对值受 CPU 频率影响（本轮 2.6–4.3 GHz），跨轮次只看同轮比值。
- 参数：complex/short 的 `personId=933`、`messageId=3`；complex-13/14
  `person1Id=933, person2Id=1129`（官方默认 ID 不在本地 sf0.1）。complex-1 的 `firstName`
  保持文件默认 `Jose`。`bench_ldbc_ab.py` / `verify_ldbc_results.py` 默认使用这组参数，
  需要换成本地其他 ID 时传 `--override personId=...,person1Id=...,person2Id=...`。
- 结果复验跑全量 21 条；complex-1/c12 的 collect 列表顺序按集合归一后比较。
- **索引前提**：short-4..7 依赖 `Person/Message/Post/Comment/Forum(id)` 索引。
  eugraph 侧各标签唯一 id 索引与 `idx_message_id` 均为 PUBLIC，neo4j 对应索引均 ONLINE；
  **不建 `Message.creationDate` 索引**（neo4j 也没有，保持访问路径对等）。

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

> **最新全量复跑（2026-10-29，CsvComposite 副本，Release 8/4，id 两边均为 INTEGER）**：
> complex-5 **187.4 ms** vs neo4j 85.5 ms（2.2×）、complex-9 **119.0 ms** vs 27.9 ms（4.3×）、
> complex-14 **115.7 ms** vs 14.6 ms（7.9×）；21/21 行结果与 neo4j 一致
> （c1/c12 仅 collect 列表顺序不同，按集合归一）。
> 本轮改动（Filter 边界延迟物化 + 实体引用不物化）的同轮 base/opt delta、探针与剩余
> 瓶颈见 [query-performance-optimization-notes.md](query-performance-optimization-notes.md) §8.12。

### 2.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| short-4 | 0.60 | 0.60 | 🟡 约 1.0× |
| short-5 | 0.60 | 0.60 | 🟡 约 1.0× |
| short-1 | 0.80 | 0.70 | 🟡 慢 1.1× |
| short-6 | 0.90 | 0.70 | 🟡 慢 1.3× |
| short-3 | 0.80 | 0.60 | 🟡 慢 1.3× |
| short-7 | 1.10 | 0.50 | 🟡 慢 2.2×（0 行） |
| short-2 | 3.60 | 1.50 | 🟡 慢 2.4× |

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-13 | 0.90 | 1.00 | 🟢 快 1.1×（shortestPath） |
| complex-3 | 45.4 | 26.7 | 🟡 慢 1.7×（0 行） |
| complex-8 | 2.60 | 1.30 | 🟡 慢 2.0× |
| complex-6 | 56.8 | 26.7 | 🟡 慢 2.1× |
| complex-5 | **187.4** | 85.5 | 🟡 慢 2.2× |
| complex-2 | 6.40 | 2.40 | 🟡 慢 2.7× |
| complex-12 | **261.9** | 96.9 | 🟡 慢 2.7×（仅 tag 列表顺序不同） |
| complex-4 | 6.10 | 2.10 | 🟡 慢 2.9×（0 行） |
| complex-7 | 3.90 | 1.20 | 🟠 慢 3.3× |
| complex-11 | 5.70 | 1.70 | 🟠 慢 3.4× |
| complex-9 | **119.0** | 27.9 | 🟠 慢 4.3× |
| complex-1 | 15.1 | 3.40 | 🟠 慢 4.4×（15/15，列表顺序归一后一致） |
| complex-10 | 31.0 | 6.70 | 🟠 慢 4.6× |
| complex-14 | **115.7** | 14.6 | 🔴 慢 7.9× |

> eugraph/neo4j 按 E→N→N→E 交错，各 2 轮 warmup 2 + 3 轮取 min；两边 `id` 都是 INTEGER，
> 参数都是整数。21 条结果逐条一致（c1/c12 仅列表顺序）。c3/c4、short-7 两边都是 0 行，
> 耗时只代表扫描/过滤成本；绝对值受 CPU 频率影响。

### 2.3 Updates

update 查询文件没有 `:param` 块（参数来自 LDBC update stream）。本轮按 LDBC 语义从图里
合成合法参数：新增类语句查重后分配新 id；like/membership/friendship 先选尚无该关系的
person/target；每条语句执行 3 次取 min，eugraph/neo4j 按 E→N→N→E 交错，Release 8/4。
两边 `id` 都是 INTEGER，update-7 的原查询可直接执行。复现脚本：
[scripts/bench_ldbc_updates.py](../../scripts/bench_ldbc_updates.py)（单引擎，交错跑两次）。

| 语句 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| update-5（add membership） | 6.93 | 8.21 | 🟢 快 1.2× |
| update-3（like comment） | 7.16 | 8.46 | 🟢 快 1.2× |
| update-8（add friendship） | 6.88 | 7.96 | 🟢 快 1.2× |
| update-2（like post） | 7.15 | 7.78 | 🟢 快 1.1× |
| update-4（add forum） | 22.91 | 17.37 | 🟡 慢 1.3× |
| update-6（add post） | 23.92 | 18.02 | 🟡 慢 1.3× |
| update-1（add person） | 45.69 | 31.70 | 🟡 慢 1.4× |
| update-7（add comment） | **332.52** | 17.75 | 🔴🔴 慢 18.7× |

> update-7 的 `MATCH (message:Message {id: $replyToPostId + $replyToCommentId + 1})`
> 右值是**参数表达式**，eugraph 的索引选择只认常量/参数本身，退化为 286k Message 全扫描；
> neo4j 先算表达式再走 `Message(id)` 索引。这是当前 update 侧最大的单点差距，可在 planner
> 做参数常量折叠后走 `IndexScan`。
>
> 早前本地 neo4j 是字符串 id 时，该表达式会做字符串拼接并 MATCH 0 行（no-op），
> 现已把本地库转成 INTEGER，表里的 neo4j 时间是真实插入。脚本对每条语句都做了效果校验
> （新节点/边必须存在），no-op 会直接报错。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P1 | 🔴 complex-14 | 115.7 ms vs 14.6 ms（约 7.9×） | PC 锚点已从 `AllNodeScan` 改为 `LabelScan` + `CorrelatedVertexProbe`；剩余是两路 PC 各约 57 ms 的实际 Comment/Post 展开 | 展开侧批量/邻接缓存，或 PC 去相关；见 [pattern-comprehension-anchor-label-scan.md](../query/pattern-comprehension-anchor-label-scan.md) |
| P1 | 🟠 complex-10 | 31.0 ms vs 6.7 ms（约 4.6×） | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P1 | 🟠 complex-1 | 15.1 ms vs 3.4 ms（约 4.4×，15/15） | shortestPath 后还有 OPTIONAL/collect 属性展开 | profile 后处理；绝对值 ~15 ms，收益有限 |
| P1 | 🟠 complex-9 | 119.0 ms vs 27.9 ms（约 4.3×） | `message.id` 的过滤前预取已由 Filter 边界消除；剩余是 53k 条 `HAS_CREATOR` 的 per-edge 成本 + creationDate coalesce | 展开侧共享 cursor / 目标属性过滤下推；见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P1 | 🟡 complex-5 | 187.4 ms vs 85.5 ms（约 2.2×） | correlated `LeftJoin` 已按 512 行批处理；剩余是 89k posts/creator 的两次 Expand（per-edge 成本） | `dst IN list` 下推进 Expand，或从 174 个 friend 反向展开后与 forum 半连接；目标 ≤150 ms |
| P2 | 🟠 complex-11 | 5.7 ms vs 1.7 ms（约 3.4×） | 多跳展开/属性读取，具体热点未拆 | profile 该路径 |
| P2 | 🟠 complex-7 | 3.9 ms vs 1.2 ms（约 3.3×） | 大结果集的 LIKES/HAS_CREATOR 展开与 `head(collect(...))` 聚合 | 排序/聚合前裁剪；绝对值 ~4 ms |
| P2 | 🟡 complex-12 | 261.9 ms vs 96.9 ms（约 2.7×，仅列表顺序差异） | `TagClass → Tag → Post` 多跳展开与中间结果规模 | 优化展开顺序/中间结果裁剪 |
| P2 | 🟡 complex-6 | 56.8 ms vs 26.7 ms（约 2.1×） | 已通过标签前向扫描从 ~9× 降到 ~2.1×；剩余是 post/tag 展开与属性读取 | 继续 profile 该路径 |

优先级按“差距 + 下一步可执行性”综合：c5/c9/c10/c14 的剩余优化都落到存储/访问路径或
更大结构调整，均列 P1（c5 已完成算子层批处理）。

### 3.1 通用方向

1. **边邻接局部性**：低 degree 展开的主要成本仍是全局 `edge_index` 的 per-source 定位，
   可考虑按 `(edge_label, direction, src/dst)` 建邻接表，或让同 source 的连续展开复用 cursor。
2. **相关子计划下推**：complex-14 已完成 PC anchor 的 `LabelScan` + `CorrelatedVertexProbe`；
   剩余是让端点 `id` 走运行时索引 seek、或对 PC 去相关。该方向对非 LDBC 查询同样适用。
3. **属性批量/惰性加载**：path 顶点按需 load labels/properties；大量路径元素时仍应批量预取，
   避免逐元素往返。
4. **内存**：shortestPath/allShortestPaths 的 BFS 路径枚举有 100k 条上限，超过抛明确错误；
   sf0.1 未触发 OOM，4G cgroup 下无失控。

## 4. 本轮验证

- **Release**：`build-release`，`CMAKE_BUILD_TYPE=Release`，`-O3 -DNDEBUG`；
  8 compute / 4 storage IO / 1GB WT cache。
- 数据：CsvComposite 副本（`eugraph-ldbc-composite2`）。sf0.1 名义 327,588 顶点 /
  1,477,965 边（本副本含少量历史测试节点），Person 1,528 / Message 286,744；索引见 §1。
- **neo4j id 类型**：本地 `neo4j` 库已就地转为 INTEGER（327,796 个 `id` 属性），
  重建 `Comment/Forum/Message/Person/Post(id)` 索引；与参考 `--id-type=INTEGER` 对齐。
  转换/清理后 c14 的 4 条 shortestPath 恢复，21 条读查询逐条与 eugraph 一致。
- 全量 21 条读查询：eugraph 与 neo4j **21/21 行结果一致**（c1/c12 仅 collect 列表顺序，
  按集合归一后相同）；c5/c9/c14 逐行一致。
- 与分支起点 HEAD 在同一 CsvComposite 副本上的 21 条结果哈希逐条一致
  （Filter 边界、实体引用折叠、`__edge_unique` 改读 EdgeKey 均未改变结果）。
- 本地 `query_executor_tests` 594/594、`data_chunk_tests` 17/17；
  `ctest`（不含 `tck_tests`）通过。按开发者要求**未跑 TCK**。
