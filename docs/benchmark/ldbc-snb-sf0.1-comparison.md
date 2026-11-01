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
| complex-12 | **695.5** | 87.9 | 🔴 慢 7.9× |
| complex-14 | **144.1** | 15.95 | 🔴 慢 9.0×（ORDER BY 并列，同多重集） |

> 每轮两引擎各一次，15 轮取 min、3 轮 warmup；绝对值受 CPU 频率影响，跨轮次只看同轮比值。
> c3/c4、short-7 两边都是 0 行，耗时只代表扫描/过滤成本。complex-12 的 48,577 次
> `io_dispatches` 归因见 §3；c14 的顺序差异只是 `pathWeight=17.0` 并列，不是结果差异。

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
| P1 | 🔴 complex-14 | 144.1 ms vs 15.95 ms（约 9.0×） | allShortestPaths 后两路 PC 的实际 Comment/Post 展开；同数据 4 条路径，结果一致 | PC 去相关/展开批量化；见 [pattern-comprehension-anchor-label-scan.md](../query/pattern-comprehension-anchor-label-scan.md) |
| P1 | 🔴 complex-12 | 695.5 ms vs 87.9 ms（约 7.9×） | **48,577 次 `io_dispatches`**：VLE `HAS_TYPE\|IS_SUBCLASS_OF*0..` 的 65,380 行 / 64 批逐跳 scan+游标流；按 ~10 µs/次估算约 0.49 s，占 0.71–0.79 s 的大部分 | 批量化/合并 VLE 逐跳扫描（减少 dispatch 次数），而不是继续调线程数 |
| P1 | 🔴 complex-10 | 52.6 ms vs 9.49 ms（约 5.5×） | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P1 | 🟠 complex-9 | 128.7 ms vs 27.6 ms（约 4.7×） | `message.id` 过滤前预取已由 Filter 边界消除；剩余是 53k 条 `HAS_CREATOR` 的 per-edge 成本 + creationDate coalesce | 展开侧共享 cursor / 目标属性过滤下推；见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P1 | 🟠 complex-1 | 23.2 ms vs 6.49 ms（约 3.6×） | shortestPath 后还有 OPTIONAL/collect 属性展开 | profile 后处理；绝对值 ~23 ms，收益有限 |
| P1 | 🟡 complex-5 | 180.9 ms vs 84.5 ms（约 2.1×） | correlated `LeftJoin` 已按 512 行批处理；剩余是 89k posts/creator 的两次 Expand（per-edge 成本） | `dst IN list` 下推进 Expand，或从 174 个 friend 反向展开后与 forum 半连接 |
| P2 | 🟠 complex-6 | 83.1 ms vs 27.0 ms（约 3.1×） | 已通过标签前向扫描从 ~9× 降到 ~3.1×；剩余是 post/tag 展开与属性读取 | 继续 profile 该路径 |
| P2 | 🟡 complex-11 | 7.67 ms vs 3.95 ms（约 1.9×） | 多跳展开/属性读取，具体热点未拆 | profile 该路径 |
| P2 | 🟡 complex-7 | 5.81 ms vs 3.05 ms（约 1.9×） | 大结果集的 LIKES/HAS_CREATOR 展开与 `head(collect(...))` 聚合 | 排序/聚合前裁剪；绝对值 ~6 ms |

优先级按“差距 + 下一步可执行性”综合。complex-12 与 complex-14 的绝对差距最大，且都能
落到“减少逐跳/逐路径的存储访问”这一条主线上。

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
- **全量 TCK**：**3907/3959 scenarios，0 failed**，769.2 s（旧基线约 1470–1500 s）。
- 本轮不是同代码 base/opt A/B：主要变化是 loader 行级标签重导入 + compute/IO 分离后的
  真实跨线程 dispatch。同轮比值与 §2 的 min 是本轮判定口径。
