# LDBC SNB Interactive SF0.1 对照（最新状态）

> 只保留当前结论：可运行性、最近一次有效耗时，以及剩余性能缺口的原因/思路。
> 历史轮次的过程数据已删除；需要复现时以本文的查询参数和测量口径为准。

## 1. 口径

- 数据：LDBC SNB Interactive **sf0.1**，CsvComposite 副本。2026-10-31 用当前 loader 重新导入
  （loader 行级标签写入行为变更后，旧数据目录不再可用）：数据目录
  `eugraph-data-sf01-composite-20261031`，CSV 目录
  `social_network-sf0.1-CsvComposite-LongDateFormatter`，schema 为
  `docs/program/design/ldbc-sf01-composite.schema.json`。导入结果
  **327,588 顶点 / 1,477,965 边（49.9 s）**；Person 1,528、Message 286,744、Post 135,701、
  Comment 151,043、Forum 13,750、Tag 16,080。`email` 为 `STRING[]`，Person 的 `language`
  列按上游 `cypher/scripts/headers.txt` 映射为 `speaks:STRING[]`（`language` 是 Post 的属性）。
- **neo4j 侧不是完全同数据**：本地 neo4j 仍带历史 update 测试标签/边。标准
  Person/Message/Post/Comment/Forum/Tag 计数与 eugraph 一致，但 KNOWS 为 **14,085 vs
  eugraph/source 14,073**（多 12 条），Person 933 的 KNOWS 度为 **15 vs 3**。因此
  complex-1 / complex-7 / complex-12 / complex-14 / short-3 的**行数不同**，
  这几条的耗时不能当作严格同数据对照。
- neo4j 的 `LIKES.creationDate`、`WORK_AT.workFrom`、Person 日期的字符串属性已归一化为整数；
  eugraph 原本就是整数。
- 环境：**Release**（构建目录 `build/release`，`-O3 -DNDEBUG`）；complex 8 compute /
  4 storage IO / 1GB WT cache / `--wt-txn-sync none`（读查询）。update 未在本轮测量
  （无 update-stream fixture）。
- 客户端：官方 Bolt driver，每条查询独立连接。**id 类型**：eugraph 是 `INT64`，
  本地 neo4j 已就地转为 `INTEGER`（327,796 个节点的 `id` 属性，随后重建 id 索引），
  两边都与参考实现 `cypher/scripts/import-to-neo4j.sh` 的 `--id-type=INTEGER` 对齐；
  读/写参数统一传整数。`scripts/bench_ldbc_ab.py` 的 `--neo4j-id-type` 默认 `int`
  （`string` 仅用于仍是字符串 id 的旧 neo4j 实例）；驱动侧 `id_type=long|string` 仍保留。
- 同机、同轮 eugraph / neo4j；**15 轮计时 + 3 轮 warmup，每轮两引擎各执行一次，取 min**。
  绝对值受 CPU 频率/负载影响（本轮峰值约 4.2 GHz），跨轮次只看同轮比值。
- 参数：complex/short 的 `personId=933`、`messageId=3`；complex-13/14
  `person1Id=933, person2Id=1129`（官方默认 ID 不在本地 sf0.1）。complex-1 的 `firstName`
  保持文件默认 `Jose`。`bench_ldbc_ab.py` / `verify_ldbc_results.py` 默认使用这组参数，
  需要换成本地其他 ID 时传 `--override personId=...,person1Id=...,person2Id=...`。
- 本轮 A/B 跑全量 21 条：**16/21 行数一致**；上述 5 条行数不同（c1 15/16、c7 7/19、
  c12 3/8、c14 4/1、short-3 3/15），因此没有对这 5 条做严格结果/耗时对照。
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

> **最新全量复跑（2026-10-31，重新导入的 CsvComposite，Release 8/4，id 两边均为 INTEGER）**：
> complex-5 **183.7 ms** vs neo4j 107.2 ms（1.7×）、complex-9 **133.5 ms** vs 37.6 ms（3.6×）、
> complex-12 **714.1 ms** vs 93.0 ms（7.7×，3/8 行）、complex-14 **148.6 ms** vs 2.85 ms
> （52.1×，4/1 行，不可比）。**16/21 行数一致**；c1/c7/c12/c14/short-3 因 neo4j 历史
> 测试边不同（见 §1），这几条的耗时不能当作严格同数据对照。

### 2.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| short-3 | 1.03 | 1.05 | 🟢 快 1.0×（3/15 行，数据差异） |
| short-5 | 0.72 | 0.65 | 🟡 慢 1.1× |
| short-4 | 0.70 | 0.59 | 🟡 慢 1.2× |
| short-1 | 1.03 | 0.76 | 🟡 慢 1.4× |
| short-6 | 1.04 | 0.65 | 🟡 慢 1.6× |
| short-7 | 1.09 | 0.56 | 🟡 慢 1.9×（0 行） |
| short-2 | 4.64 | 1.87 | 🟡 慢 2.5× |

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-13 | 1.10 | 1.09 | 🟡 约 1.0×（shortestPath） |
| complex-5 | **183.7** | 107.2 | 🟡 慢 1.7× |
| complex-8 | 3.10 | 1.74 | 🟡 慢 1.8× |
| complex-2 | 6.95 | 3.44 | 🟡 慢 2.0× |
| complex-7 | 6.41 | 2.83 | 🟡 慢 2.3×（7/19 行，数据差异） |
| complex-6 | 85.8 | 34.9 | 🟡 慢 2.5× |
| complex-4 | 8.43 | 3.17 | 🟡 慢 2.7×（0 行） |
| complex-11 | 7.65 | 2.61 | 🟡 慢 2.9× |
| complex-3 | 109.3 | 35.6 | 🟠 慢 3.1×（0 行） |
| complex-9 | **133.5** | 37.6 | 🟠 慢 3.6× |
| complex-10 | 50.0 | 12.7 | 🟠 慢 3.9× |
| complex-1 | 21.6 | 3.94 | 🔴 慢 5.5×（15/16 行，数据差异） |
| complex-12 | **714.1** | 93.0 | 🔴 慢 7.7×（3/8 行，数据差异；48,577 dispatches） |
| complex-14 | **148.6** | 2.85 | 🔴🔴 慢 52.1×（4/1 行，数据差异，不可比） |

> 每轮两引擎各一次，15 轮取 min、3 轮 warmup；绝对值受 CPU 频率影响，跨轮次只看同轮比值。
> 16/21 行数一致；5 条行数不同见 §1。c3/c4、short-7 两边都是 0 行，耗时只代表扫描/过滤
> 成本。complex-12 的 48,577 次 `io_dispatches` 归因见 §3。

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

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P1 | 🔴🔴 complex-14 | 148.6 ms vs 2.85 ms（约 52.1×；4/1 行，数据不可比） | PC 锚点已从 `AllNodeScan` 改为 `LabelScan` + `CorrelatedVertexProbe`；剩余是两路 PC 的实际 Comment/Post 展开。当前行数 4/1，绝对差距被数据差异放大 | 先对齐/排除 neo4j 历史测试边，再做 PC 去相关；见 [pattern-comprehension-anchor-label-scan.md](../query/pattern-comprehension-anchor-label-scan.md) |
| P1 | 🔴 complex-12 | 714.1 ms vs 93.0 ms（约 7.7×；3/8 行，数据不可比） | **48,577 次 `io_dispatches`**：VLE `HAS_TYPE\|IS_SUBCLASS_OF*0..` 的 65,380 行 / 64 批逐跳 scan+游标流；按 ~10 µs/次估算约 0.49 s，占 0.71–0.79 s 的大部分 | 批量化/合并 VLE 逐跳扫描（减少 dispatch 次数），而不是继续调线程数；同时对齐数据后再比较 |
| P1 | 🔴 complex-1 | 21.6 ms vs 3.94 ms（约 5.5×；15/16 行） | shortestPath 后还有 OPTIONAL/collect 属性展开；行数差异来自 neo4j 额外 KNOWS 边 | profile 后处理；先排除数据差异，收益有限 |
| P1 | 🟠 complex-10 | 50.0 ms vs 12.7 ms（约 3.9×） | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P1 | 🟠 complex-9 | 133.5 ms vs 37.6 ms（约 3.6×） | `message.id` 过滤前预取已由 Filter 边界消除；剩余是 53k 条 `HAS_CREATOR` 的 per-edge 成本 + creationDate coalesce | 展开侧共享 cursor / 目标属性过滤下推；见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P1 | 🟡 complex-5 | 183.7 ms vs 107.2 ms（约 1.7×） | correlated `LeftJoin` 已按 512 行批处理；剩余是 89k posts/creator 的两次 Expand（per-edge 成本） | `dst IN list` 下推进 Expand，或从 174 个 friend 反向展开后与 forum 半连接 |
| P2 | 🟡 complex-11 | 7.65 ms vs 2.61 ms（约 2.9×） | 多跳展开/属性读取，具体热点未拆 | profile 该路径 |
| P2 | 🟡 complex-7 | 6.41 ms vs 2.83 ms（约 2.3×；7/19 行，数据差异） | 大结果集的 LIKES/HAS_CREATOR 展开与 `head(collect(...))` 聚合；行数差异来自额外测试边 | 先对齐数据；排序/聚合前裁剪 |
| P2 | 🟡 complex-6 | 85.8 ms vs 34.9 ms（约 2.5×） | 已通过标签前向扫描从 ~9× 降到 ~2.5×；剩余是 post/tag 展开与属性读取 | 继续 profile 该路径 |

优先级按“差距 + 下一步可执行性”综合。complex-12 与 complex-14 的绝对差距包含数据差异，
真正可执行的优化分别是 **VLE dispatch 批量化** 与 **PC 去相关/展开批量化**。

### 3.1 通用方向

1. **边邻接局部性**：低 degree 展开的主要成本仍是全局 `edge_index` 的 per-source 定位，
   可考虑按 `(edge_label, direction, src/dst)` 建邻接表，或让同 source 的连续展开复用 cursor。
2. **相关子计划下推**：complex-14 已完成 PC anchor 的 `LabelScan` + `CorrelatedVertexProbe`；
   剩余是让端点 `id` 走运行时索引 seek、或对 PC 去相关。该方向对非 LDBC 查询同样适用。
3. **属性批量/预取**：表达式求值已不在现场做 IO；裸顶点/`startNode` 等由算子边界
   `prefetch` 批量加载。path 元素继续按批预取，避免逐元素往返。
4. **dispatch 批量化**：complex-12 的 VLE 仍有 48,577 次逐跳 dispatch；需要合并
   游标扫描/邻接读取，减少跨线程次数，而不是继续增加线程。
5. **内存**：shortestPath/allShortestPaths 的 BFS 路径枚举有 100k 条上限，超过抛明确错误；
   sf0.1 未触发 OOM，4G cgroup 下无失控。

## 4. 本轮验证

- **Release**：`build/release`，`CMAKE_BUILD_TYPE=Release`，`-O3 -DNDEBUG`；
  8 compute / 4 storage IO / 1GB WT cache / `--wt-txn-sync none`（读查询）。
- 数据：2026-10-31 重新导入的 CsvComposite（`eugraph-data-sf01-composite-20261031`），
  327,588 顶点 / 1,477,965 边；Person 1,528 / Message 286,744 / Post 135,701 /
  Comment 151,043 / Forum 13,750 / Tag 16,080。loader 行级标签行为变更后旧数据目录不可用，
  本轮从 CSV 重新导入（49.9 s，0 skipped）。
- **neo4j id 类型**：本地 `neo4j` 库已就地转为 INTEGER（327,796 个 `id` 属性），
  重建 `Comment/Forum/Message/Person/Post(id)` 索引；`scripts/bench_ldbc_ab.py`
  默认 `--neo4j-id-type int`，与 eugraph 的 `INT64` 对齐。
- **行数一致性**：21 条读查询中 **16 条与 neo4j 逐条一致**；c1/c7/c12/c14/short-3
  行数不同（15/16、7/19、3/8、4/1、3/15），原因是 neo4j 仍带历史 update 测试边
  （KNOWS 14,085 vs 14,073；Person 933 度 15 vs 3），不是引擎语义差异。
- **本轮代码验证**（提交 `10f4aab1`）：`query_executor_tests` **603/603**、
  `index_e2e_tests` 56/56、`rpc_integration_tests` 28/28（4 个网络 IO worker）、
  `io_scheduler_tests` 6/6、`query_stream_hop_tests` 4/4；分组跑完 `ctest` **1421/1421**。
- **全量 TCK**：**3907/3959 scenarios，0 failed**，769.2 s（旧基线约 1470–1500 s）。
- 本轮不是同代码 base/opt A/B：主要变化是 loader 行级标签重导入 + compute/IO 分离后的
  真实跨线程 dispatch。旧表数值不可直接横比；同轮比值与 §2 的 min 才是本轮的判定口径。
