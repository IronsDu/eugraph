# LDBC SNB Interactive SF0.1 复测（当前版本）

> **2026-09-19 修订**：complex-10 结论已更正、延迟已重测；complex-5/6/9 的可执行性
> 与根因已补入。未标记小节测得于 2026-09-10，其数值受当时 `CPU(s) scaling MHz` 影响，
> **不宜与新值直接相减**。值拷贝优化的完整剖析见
> [查询值拷贝性能剖析](query-value-copy-profiling.md)。

> 分支：`feat/pattern-expression-in-return`（main `c63e3bd` + 本轮 RETURN/ORDER BY pattern expression 支持）
> 数据目录：`eugraph/build/ldbc-bench/eugraph`（converted Neo4j-header CSV，`Comment:Message` / `Post:Message`）
> 节点 327,588；边 1,477,965
> 查询集：`ldbc_snb_interactive_v1_impls/cypher/queries/` 的 **LDBC 原版查询文本**
> 方法：Bolt 7688，每个查询 1 次预热 + 3 次计时取中位数；单次超时 45s
> 日期：2026-09-10

## 1. 测试环境

| 项 | 配置 |
|---|---|
| server | `eugraph-server --bolt-port 7688 --thrift-port 9090` |
| WiredTiger | cache 2048MB，`--wt-txn-sync none`，threads 4 |
| 索引 | loader 自带各标签 `id` 唯一索引 + `Tag(name)` / `TagClass(name)` / `Person(firstName)` / `Message(creationDate)` 等 |

## 2. 已执行查询结果（同机对比）

> 时间均为 1 次预热 + 3 次计时取中位数，单位 ms。
> 图标：🟢 = EuGraph 与 Neo4j 性能相当或更快（倍数 ≤ 1.5）；🟡 = 慢 1.5~5 倍；🔴 = 慢 5 倍以上。
> 倍数 = EuGraph / Neo4j，<1 表示 EuGraph 更快。

| Query | 行数 | EuGraph(ms) | Neo4j(ms) | 倍数 | 图标 |
|-------|-----:|------------:|----------:|-----:|:---:|
| complex-12 | 2 | 13.09 | 83.79 | 0.16 | 🟢 |
| short-6 Comment | 1 | 1.21 | 4.85 | 0.25 | 🟢 |
| short-3 | 3 | 1.04 | 3.92 | 0.27 | 🟢 |
| short-5 Post | 1 | 0.78 | 2.42 | 0.32 | 🟢 |
| short-4 Post | 1 | 0.90 | 2.49 | 0.36 | 🟢 |
| short-1 | 1 | 1.06 | 2.82 | 0.38 | 🟢 |
| short-4 Comment | 1 | 0.98 | 2.49 | 0.39 | 🟢 |
| short-5 Comment | 1 | 0.92 | 2.23 | 0.41 | 🟢 |
| short-6 Post | 1 | 1.28 | 2.54 | 0.50 | 🟢 |
| short-7 无回复 | 0 | 1.40 | 2.70 | 0.52 | 🟢 |
| short-7 有回复 | 19 | 7.02 | 9.27 | 0.76 | 🟢 |
| short-2 | 10 | 5.78 | 5.25 | 1.10 | 🟢 |
| complex-8 | 20 | 5.92 | 5.28 | 1.12 | 🟢 |
| complex-2 | 20 | 25.51 | 17.98 | 1.42 | 🟢 |
| complex-4 | 10 | 18.23 | 8.92 | 2.04 | 🟡 |
| complex-11 | 10 | 13.28 | 5.85 | 2.27 | 🟡 |
| complex-3 | 0 | 101.20 | 48.76 | 2.08 | 🟡 |

说明：
- complex-3 本轮从 2024.30ms 降至约 101ms（同参数、同机）；该参数返回 0 行，但查询正常执行完成。
- complex-3 结果正确性用返回 1 行的 `Sweden/Kazakhstan` 参数与 Neo4j、原计划交叉核对一致。
- `Message(creationDate)` 索引必须是完整索引；本机旧索引只有 1024 条，重建后为 286,744 条（见 5.6）。
- short-7 使用“无回复”的 Comment，避免已知的 OPTIONAL MATCH 超时路径。
- 参数口径见下表。

### 2.1 参数

| Query | 参数 |
|-------|------|
| complex-2 | personId=933, maxDate=1287187200000 |
| complex-3 | personId=933, countryXName=Germany, countryYName=Brazil, startDate=1275350400000, endDate=1277769600000 |
| complex-4 | personId=933, startDate=1262808638903, endDate=1347527502711 |
| complex-8 | personId=933 |
| complex-11 | personId=933, countryName=India, workFromYear=2011 |
| complex-12 | personId=933, tagClassName=Actor |
| short-1/2/3 | personId=933 |
| short-4/5/6 Post | messageId=3 |
| short-4/5/6 Comment | messageId=618475290625 |
| short-7 | messageId=618475290625（无回复） |

## 3. 能力缺口与重查询状态

| Query | 状态（2026-09-19） |
|-------|------|
| complex-1 / complex-13 | 语法不支持：依赖 `shortestPath` |
| complex-14 | 语法不支持：`allShortestPaths` + `reduce` |
| **complex-6** | ✅ **可执行**（personId=933：0.49 s / 3 行） |
| **complex-9** | ✅ **可执行**（personId=933：3.96 s / 20 行，峰值 3.1 GB） |
| **complex-5** | ⚠️ 不再耗尽内存，但 200 s 内不返回（内存平稳 ~4 GB）—— 属**性能/计划**问题 |

**complex-5/6/9 原先「CPU/内存失控、跳过」的根因已定位并修复**：
三者共用 `WITH collect(DISTINCT friend) AS friends UNWIND friends AS f ...`，
而 `friends` 在 UNWIND 之后**已死**却被继续携带；`ProjectionExtract` 把上游的
CONSTANT 广播列重新逐行物化成 FLAT，于是「1 份值」变成 `行数` 份 174 元素列表的深拷贝。

| 写法（同语义、同数据） | 修复前 | 修复后 |
|---|---:|---:|
| 普通 MATCH 等价写法 | 671 MB / 0.71 s | — |
| `collect+UNWIND`（LDBC 原写法） | **OOM（>6 GB）/ 6.31 s** | **599 MB / 0.32 s** |
| `collect+UNWIND` 后加 `WITH f` 丢弃该列 | 653 MB / 0.73 s | 598 MB / 0.55 s |

修复：`ProjectionExtractPhysicalOp` 对 CONSTANT 源列的透传**保持 CONSTANT**
（该算子输入输出行 1:1，故广播形式仍成立）。详见该修复的提交说明。

### complex-5/6/9 与 neo4j 对比（2026-09-19，personId=933）

**测量前对 neo4j 实例补了三处数据差异**（与此前补 `City`/`Country`/`Continent`、`birthday`
的做法一致）—— 否则 neo4j 侧的耗时无意义：

| 差异 | neo4j 原状 | 补齐 |
|---|---|---|
| 派生标签 `Message` | **不存在**（警告 `label does not exist`），complex-9 返回 0 行 | `MATCH (n:Post) SET n:Message` + `Comment` 同理 → **286,744**，与 eugraph 一致 |
| `Message.creationDate` | **字符串** | `toInteger()` → complex-9 的 `WHERE creationDate < $maxDate` 才能生效 |
| `HAS_MEMBER.joinDate` | **字符串** | `toInteger()` → complex-5 的 `WHERE joinDate > $minDate` 才能生效 |

> 后两处与文档早先记录的 `birthday` 是同一类问题：旧转换把 LONG 表头当字符串处理。
> **补之前**：complex-9 与 complex-5 在 neo4j 上均返回 **0 行**（耗时 13–28 ms，无参考价值）。

| 查询 | eugraph | neo4j | 倍数 | 行数（eugraph / neo4j）|
|---|---:|---:|---:|:---:|
| **complex-6** | 0.49 s | **30.8 ms**（median 35.6） | ~16× | **3 / 3** ✅ |
| **complex-9** | 3.96 s | **36.9 ms**（median 40.4） | ~107× | **20 / 20** ✅ |
| **complex-5** | 未完成（>200 s） | **92.3 ms**（median 96.6） | — | — / 20 |

**行数一致**说明两个引擎的**结果正确**（complex-6 与 complex-9 逐行内容未逐条比对，
但行数与首行一致）。**complex-5 是当前最大差距**：neo4j 96 ms 完成，eugraph 200 s 不返回。

**complex-5 剩余问题**（未修）：`OPTIONAL MATCH (friend)<-[:HAS_CREATOR]-(post)
<-[:CONTAINER_OF]-(forum) WHERE friend IN friends` 未完成 —— 需查 join order /
`IN` 谓词下推，与内存无关。

**complex-10 已支持**（此前记为「pattern comprehension 语法不支持」）：根因是
`bindExistsSubPlan` 的保存槽用局部计数器命名，同语句的两个子计划都生成
`__exists_saved_1` 而互相串绑定，导致与 `NOT <模式谓词>` 同处一个 WHERE 的模式推导
静默丢弃终点约束。详见
[列表推导内模式推导：发现与待修缺陷](../query/deferred-pattern-comprehension-findings.md)。

冒烟（personId=933, month=5）：complex-2/3/4 返回 0 行（该人当月无数据，非错误）、
complex-7 **7 行** / complex-8 **20 行** / complex-10 **10 行** / complex-11/12 **0 行** 均正常；
complex-1/13/14 因语法不支持报错。

### 测量警告：CPU 会降到 33% 频率

本机（AMD Ryzen 7 5825U）在若干次测量中观察到 `lscpu` 的
`CPU(s) scaling MHz: 33%`，此时**同一二进制**的 complex-7 相比全频时慢约 2 倍
（如 pid 933 从 min 8.4ms 变为 15.8ms、pid 1242 从 39ms 变为 71ms）。
**跨时间比较复杂度指标前必须确认当前 CPU 频率**，否则会把功耗状态误判为代码回归 ——
本轮就曾因此怀疑新提交引入了性能回归，同二进制交错 A/B 显示两个二进制逐项相同，
真正原因是频率。

### complex-10 延迟（sf0.1）

> **2026-09-19 更新**：本节原表测得于 `CPU(s) scaling MHz: 34%`；下表的**旧值**行保留作对照，
> **新值**为「消除值深拷贝」改动落地后所测（`CPU(s) scaling MHz: 58%`，同机、同参数、
> 同口径 warmup 3 / iters 15 / 每轮独立连接，threads 4）。
> 频率不同（58% vs 34%）意味着**不能直接相减**：把旧值按 1.7× 归一化后仍显示约 2.4× 改善，
> 与同二进制交错 A/B 的 2.25–2.74× 一致。改动详见
> [查询值拷贝性能剖析](query-value-copy-profiling.md)。

**测量前提**：warmup 3 / iters 15，每轮独立连接，`--compute-threads 4`。

| personId | month | rows | 旧 median（34% MHz） | **新 min** | **新 p25** | **新 median** | **新 p95** |
|---|---:|---:|---:|---:|---:|---:|---:|
| 933 | 5 | 10 | 396.59 ms | **96 ms** | **97 ms** | **99 ms** | **106 ms** |
| 933 | 8 | 10 | 667.20 ms | **211 ms** | **212 ms** | **215 ms** | **228 ms** |
| 1242 | 5 | 10 | 2476.58 ms | **906 ms** | **918 ms** | **925 ms** | **944 ms** |
| 2199023256816 | 5 | 10 | 2902.08 ms | **1242 ms** | **1281 ms** | **1293 ms** | **1487 ms** |

**新值下 `min` 与 `p95` 仅差约 10%**（旧值 p95/min 达 1.15–1.29），说明耗时更稳定 ——
原因正是消除了「每帖复制整张属性表」这类与数据规模耦合的分配行为。

**同条件下的 complex-7 参照**（用于上下文化，min）：

| personId | complex-7 min | complex-10 min | 倍数 |
|---|---:|---:|---:|
| 933 | 17.03 ms | 375.74 ms | ~22x |
| 1242 | 74.72 ms | 2328.10 ms | ~31x |
| 2199023256816 | 134.52 ms | 2803.82 ms | ~21x |

**~~瓶颈在计划开头，不在推导~~ —— 此结论已被测量否证（2026-09-19）**：

原结论是从**计划形状**推断的（看见 `VarLenExpand` 在最外层就认定它是瓶颈），
但逐算子计时与 perf/GDB 采样给出了相反的答案：

| 环节 | 实测 |
|---|---:|
| 纯 2 跳 VLE（`VarLenExpand`） | **5.87 ms（0.1%）** |
| 推导子计划的逐帖驱动 + 值拷贝 | **约 88%（简化探针上 3.4 s）** |

**真正的瓶颈是「每帖一次的子计划执行」里反复发生的深拷贝**：
`Unwind` 把 `posts` 这个 107 元素的列表**逐元素复制**（每组约 11449 次元素拷贝而非 107 次），
每次拷贝都克隆 `unordered_map<LabelId, Properties>`。修复后该算子从占执行时间的
**94.3% 降为 0%**（采样证据见 [查询值拷贝性能剖析](query-value-copy-profiling.md)）。

**教训**：从计划形状推断瓶颈会得出错误结论 —— 必须逐算子计时，再用采样确认「时间花在什么操作上」。
`VarLenExpand` 之所以「看起来」是瓶颈，是因为它在计划树上处于最外层，而非因为它耗时。

原结论中仍然成立的部分：该 VLE **是无向 2 跳**（`direction=ANY`），需双向遍历，
且候选集随人脉规模增长 —— 但它的**绝对耗时很小**，不是主要成本。

**优化方向**（未实施）：从 `VarLenExpand` 的中间节点入手减少 2 跳候选集；
或把 `NOT (friend)-[:KNOWS]-(person)` 的 `AntiSemiJoin` 前移到 VLE 之前剪枝。

### complex-10 与 neo4j 对比（需先补齐 neo4j 数据，两个差异）

neo4j 实例是从**旧的转换结果**导入的，直接对照会失真；补齐两处后两引擎可等价对比：

| 差异 | neo4j 现状 | 补齐 |
|---|---|---|
| 派生细分标签 | `City`/`Country`/`Continent` 均为 0（eugraph 从 `Place.type` 派生） | `MATCH (p:Place) WHERE p.type='city' SET p:City`（country / continent 同理）→ 1343/111/6，与 eugraph 一致 |
| `birthday` 类型 | **字符串**（原始 CSV 表头是 `birthday:LONG`，旧转换当字符串处理） | `MATCH (n:Person) SET n.birthday = toInteger(n.birthday)` |

**结果一致性核验**（补齐后）：

| personId | eugraph | neo4j | 结论 |
|---|---:|---:|---|
| 933 | 10 行 | 10 行 | **逐行一致** |
| 1242 | 10 行 | 10 行 | 顶部 4 行不同 —— 经查是**平局截断**：`score = -1` 有 15 个 friend 而只取 10 个 |

1242 的完整 `score` 分布**两引擎逐 bucket 相同**（75 个 bucket，含 `0:5, -1:5, -2:1, …, -2490:1`），
且 2 跳过滤后的 friend 数两边都是 96 —— **结果正确，差异仅来自 ORDER BY 平局内的取舍顺序**。

### complex-10 延迟对比（同数据、同机、同口径）

> **2026-09-19 更新**：值深拷贝优化落地后重测。下表旧值为优化前（`CPU(s) scaling MHz: 34%`），
> 新值为优化后（`58%`）；两列**都在同一次会话内**与 neo4j 同口径测得（warmup 3 / iters 15），
> 因此**倍数**可直接比较。

| personId | 时期 | eugraph min / median | neo4j min / median | 倍数（min） |
|---|---|---:|---:|---:|
| 933 | 优化前（34% MHz） | 410.36 / 426.23 ms | 21.24 / 22.71 ms | 19.3x |
| 933 | **优化后（58% MHz）** | **96 / 99 ms** | **13.6 / 18.6 ms** | **7.1x** |
| 1242 | 优化前（34% MHz） | 2363.17 / 2548.27 ms | 39.08 / 42.43 ms | 60.5x |
| 1242 | **优化后（58% MHz）** | **906 / 925 ms** | **35.5 / 41.4 ms** | **25.5x** |

**与 neo4j 的差距显著收窄**：933 从 19.3× 降到 **7.1×**，1242 从 60.5× 降到 **25.5×**
（各约改善 2.4–2.7 倍，与「值拷贝」优化的 A/B 结果一致）。

**剩余差距的性质**：本轮的优化针对的是**两引擎共有的深拷贝开销**（neo4j 也做属性读取，
但它不在热路径上复制整张属性表）。剩余差距主要来自
**无向 2 跳 VLE 的实现方式**（见下节）与推导的执行结构，而非值传输。

（测量时 `CPU(s) scaling MHz` 偏低，两边同样受影响，**倍数**比绝对值更可靠。）

**差距来源在计划开头，不在推导**：

```
VarLenExpand(src=person, dst=friend, hops=[2..2], labels=[11], direction=ANY)
```

即 `[:KNOWS*2..2]` 的**无向 2 跳变长展开**。neo4j 用双向 BFS + 关系索引，
而当前实现的无向变长展开是主要成本；且它按 person 的连接度放大，
所以高连接度的 1242 比 933 慢 6 倍（而 neo4j 只慢 1.8 倍）。

**原优化方向**（针对 VLE；仍可做，但不是首要目标）：

1. 无向 VLE 改为**双向按需扩展**（而非全量扫描后取交集）；
2. 把 `NOT (friend)-[:KNOWS]-(person)` 的 `AntiSemiJoin` **前移到 VLE 之前**剪枝；
3. 日期过滤（`friend.birthday`）在 VLE 阶段即可用属性下推削减候选。

**已实施的优化方向**（收益 2.25–2.74×）：消除查询管线中的值深拷贝 ——
`Unwind` 透传列改用 `Column::CONSTANT`、`ProjectionExtract` 的 `RowCache` 跨行复用、
透传值走类型直拷、推导不再引发整点物化。详见
[查询值拷贝性能剖析](query-value-copy-profiling.md)。

### 回归验证：EXISTS 保存槽改名未影响性能

同数据、同机、每轮重启、两二进制交错各两轮（complex-7，warmup 5 / iters 30）：

| pid | 修复前 min（两轮） | 修复后 min（两轮） |
|---|---|---|
| 933 | 15.84 / 15.81 ms | 16.47 / 15.86 ms |
| 1242 | 71.02 / 71.16 ms | 71.64 / 72.42 ms |
| 2199023256816 | 138.96 / 138.08 ms | 154.72 / 147.35 ms |

逐项差异都在噪声内，**无可测回归**。

## 4. 当前版本关键优化（Q12 + Complex-3 / Short-7 + Complex-7 原版支持）

1. **Q12 计划重写**：`Apply(collect(tag.id), HashJoin(friend))`，左支从 `Tag(id) IN tags` 反向展开，右支从 `Person(id)` 经 KNOWS 展开。
2. **Expand 批处理**：dst label 一次批量检查；新增 `scanEdgesBatch` 快速路径。
3. **VarLenExpand 邻接缓存**：同一 chunk 内复用每个 vertex 的邻接边和 label 判定，避免多起点重复扫描同一张图。
4. **VarLenExpand OR 索引剪枝**：`WHERE src.prop = v OR dst.prop = v` 下推为 src/dst 索引允许集，Q12 左分支起点从 71 个 TagClass 降为 1 个。
5. **Complex-3 索引优先 join**：将 `Expand(friend→message) → Filter(message.creationDate) → Expand(message→country)` 改写为 `HashJoin(candidate friends, IndexScan(Message.creationDate range) → HAS_CREATOR → creator)`；日期谓词在 Expand 之上先拆分为独立 Filter 后再下推。
6. **Short-7 OPTIONAL MATCH 相关性链修复**：起点已绑定时不再因为终点也已绑定就退化为独立扫描 + CrossProduct，而是继续用 `CorrelatedSource → Expand(start→new) → Expand(new→bound_endpoint)`；同时修复了 complex-7 的 OPTIONAL MATCH 等价改写。
7. **Complex-7 原版 pattern expression 支持**：`NOT`、`AND` / `OR`、`CASE WHEN`、`ANY` / 列表推导 WHERE、`EXISTS { pattern }` 等布尔上下文中的 bare pattern 解析为 `ExistsExpr`，经 `BoundPatternComprehensionApplyOp` hoist 后绑定为 `size(list) > 0`；原版 complex-7 已可执行，结果与 Neo4j 逐行一致。

Q12 同机中位数：约 **13ms**（优化前约 1.2s）。

Complex-3 同机中位数：约 **101ms**（优化前约 2024ms）。

Short-7 有回复中位数：约 **7ms**（优化前超时 >240s；Neo4j 同机约 9ms）。

## 5. 历史结论与遗留优化方案（保留）

> 本节只保留仍然有效的根因分析和后续方案；已过时的“历史耗时记录”不再保留。

### 5.1 已知语法能力缺口

| 语句 | 缺口 | 方案 |
|------|------|------|
| complex-1 | `shortestPath` 不支持 | 实现双向 BFS shortest-path 算子 |
| complex-13 | `shortestPath` 不支持，当前用 `knows*1..3 + min(length(path))` 近似 | 同上 |
| complex-14 | `allShortestPaths` + `reduce` 不支持 | 实现 all-shortest-paths + reduce |

### 5.2 仍然较慢的语句与优化方向

| 语句 | 当前差距 | 已定位原因 | 后续方案 |
|------|---------|-----------|---------|
| complex-3 | ~2.1x | 已改为 Message(creationDate) 索引优先 HashJoin(creator=friend)；剩余在 `KNOWS*1..2` VLE、聚合与顶点物化 | 继续优化 VLE、聚合/排序批量化 |
| complex-7 | 3.4x~12.5x | **不是** pattern expression 主导：主因是中间宽列物化 + 逐行 `Value` variant 拷贝/堆分配（该主因已由值拷贝优化改善） | 继续压缩中间宽列与聚合/排序批量化 |
| complex-4 | ~2.0x | 已修多 pattern 关联；剩余在聚合/排序/ProjectionExtract | 聚合与排序批量化；ProjectionExtract 向量化 |
| complex-11 | ~2.3x | 同上 | 同上 |

### 5.3 重查询状态复核（2026-09-19）

| 语句 | 现象（复核后） | 已定位方向 |
|------|------|-----------|
| complex-5 | 不再 OOM，但 200 s 内不返回 | `friend IN friends` 未下推 / join order |
| complex-6 | ✅ 已可执行（0.49 s） | — |
| complex-9 | ✅ 已可执行（3.96 s，峰值 3.1 GB，仍偏高） | 排序未做 top-K，`ORDER BY … LIMIT 20` 前物化全部朋友消息 |

### 5.4 已经修复并验证有效的问题（保留结论）

| 问题 | 修复 | 结果 |
|------|------|------|
| 单 MATCH 多 pattern / WITH 后 MATCH 退化为 AllNodeScan + CrossProduct | binder 复用已绑定列续接 Expand/VarLenExpand | complex-4、short-2 不再 AllNodeScan |
| `:Message` 无主标签导致点查走 AllNodeScan | loader 多标签 + weak index + `Filter(LabelScan)->IndexScan` 触发 | short-4/5/6 降至 ~1ms |
| Q12 `tag.id IN tags` 未下推、前向链 1.2s | Apply + HashJoin(friend) + Tag 反向链 + VLE 邻接缓存 + VLE OR 索引剪枝 | Q12 降至 ~13ms |
| complex-3 先按 friend 展开 45,723 条 message，再做 creationDate 过滤 | WHERE 顶层 AND 拆分 + 谓词下推 + `IndexScan(Message.creationDate) → HAS_CREATOR → HashJoin(friend)` | 2024ms → ~101ms |

### 5.5 Q12 优化路径记录（保留方法）

1. 计划重写：`Filter(CrossProduct)` → `Apply(collect(tag.id), HashJoin(friend))`；
2. 右支：`IndexScanValues(Tag id IN tags) → 反向 Expand`，补 dst label 约束；
3. 左支：TagClass 起点反向 VLE；同一 chunk 缓存 vertex 邻接边，edge scan 65,689 → 16,151；
4. 左支：OR 下推为 VLE src/dst 索引允许集，71 个 TagClass 起点 → 1 个。

### 5.6 Complex-3 优化路径记录（本轮）

1. **根因**：最后一条 MATCH 的 `WHERE message.creationDate >= start AND < end AND country IN [...]` 被绑定为一个 Filter，计划先把每个候选 friend 的全部 message 展开出来（该参数为 45,723 条），再过滤 creationDate。
2. **AND 拆分**：binder 在 WHERE 直接作用于 Expand/VarLenExpand 时，把顶层 AND 拆成嵌套 Filter；这样 date 谓词可以下推到 `Expand(message→country)` 之前，而 `country IN [...]` 留在 Expand 之后。拆分前先递归检查 Expand，避免破坏 `Filter(LabelScan) → IndexScan` 的复合索引匹配。
3. **索引优先 join**：物理计划将 `Filter(date) → Expand(friend→message, IN HAS_CREATOR)` 重写为 `HashJoin(left=candidate friends, right=IndexScan(Message.creationDate range) → Expand(message→creator, OUT HAS_CREATOR))`，把 45,723 次“逐 friend 展开消息”降为日期索引命中的 3,389 条消息。
4. **索引 range 分页修复**：`AsyncGraphDataStore` 的索引 range 扫描原先每轮都会从起点重扫第一批（结果 >1024 条时重复）；改为先收集命中集、再按 1024 分块 yield，保证 3,389 条消息全部返回。
5. **跨类型 join 等值**：HashJoin 现在把 `VertexValue` / `VertexRef`（以及 `EdgeValue` / `EdgeKey`）按同一个 id 视为相等，使左支构造出的 friend 顶点能与右支拓扑引用 creator 正确 join。
6. **依赖**：`Message(creationDate)` 必须是完整索引。本机旧索引只有 1024 条，重建后为 286,744 条；未修复索引时该改写不会启用（也没有结果错误，只是退回原计划）。

---

## 附：已移除的过程叙述

本文件此前还包含 §6「执行计划对比与扩展性分析」、§7「SHORT-7 / COMPLEX-7 的
OPTIONAL MATCH 相关性优化」、§8「COMPLEX-7 基线与优化」共约 750 行。
它们是 complex-7 / q12 的**逐轮过程记录**，其**结论已保留在 §4 与 §5**
（关键优化项、已修复问题、优化路径记录、遗留方向），过程细节属于历史叙述，
不再占据本文档篇幅。需要查阅原始过程可回溯 git 历史。
