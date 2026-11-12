# Expand 目标标签过滤优化

## 1. 背景

Cypher 模式中的标签过滤有两类：

1. 扫描输入变量时直接按标签扫描（`LabelScan`）；
2. `Expand` 产生邻居后，要求邻居满足 `dst_label_ids_`。

第二类在 LDBC 查询里很常见，例如：

```cypher
MATCH (p:Person {id:933})-[:KNOWS*1..2]-(f:Person)
MATCH (f)<-[:HAS_CREATOR]-(m:Message)
RETURN count(*)
```

`Expand(f, m, HAS_CREATOR, IN)` 先得到 message 候选，再要求 `m` 有 `Message` 标签。

## 2. 原有实现

`ExpandPhysicalOp` 在边上过滤阶段对每个不同的 `dst_id` 调用
`getVertexLabelsBatch(neighbors)`：

```text
neighbors 去重
-> getVertexLabelsBatch(neighbors)
-> 建立 vid -> labels 映射
-> 逐边检查所有 dst_label_ids_
```

正确，但对大结果集是大量标签点查：每个邻居一次 B-tree seek。  
complex-3 的这一步约 5.8 万邻居，实测旧实现约 60.6ms。

## 3. 改动

`src/query/physical_plan/operator/expand_physical_op.cpp`：

- `edges.size() < 4096`：保留逐 neighbor 标签点查；
- `edges.size() >= 4096`：
  1. 收集本批 `neighbor_set`；
  2. 对每个需要的标签，顺序扫 `label_fwd_{label}`；
  3. 只保留同时属于 `neighbor_set` 的 vid，得到 `members`；
  4. 多标签时对 `members` 取交集；
  5. 按 membership 过滤边。

即用一次/几次顺序标签扫描代替数万次随机标签点查。

阈值 4096 是实测折中：小集合点查更便宜，无条件扫描会让 complex-2/8/10 这类
小集合查询变慢。

## 4. 性能

### 4.1 重新导入数据

使用 sf0.1 原始 CSV 重新导入，避免沿用旧数据目录：

```text
social_network-sf0.1-CsvBasic-LongDateFormatter
schema: /tmp/loader-schema-sf01-basic.json
loader: --batch-size 5000 --rpc-connections 4 --parallel-files 4
```

说明：本节标签过滤 A/B 使用首次 fresh import（未导入 Person `email`/`speaks`），
因为 friend_msg / complex-6 的读取路径不使用这两个属性；最终与 neo4j 对照使用补齐
`email`/`speaks` 的导入数据（见 benchmark §4）。

导入后核对：

- 顶点：327,588
- 边：1,477,965
- Person：1,528
- Message：286,744

把导入后的数据目录复制成两份，分别启动：

- `base`：`ad71a72^`（仅去掉标签过滤改动）
- `opt`：`ad71a72`（本分支）

同机、同数据、同参数、8 compute、cache 256MB，交错执行取 min。

### 4.2 核心步骤

| 项目 | base | opt | 变化 |
|---|---:|---:|---:|
| friend_msg（complex-3 核心步骤） | 56.47ms | **35.64ms** | **1.58×** |

### 4.3 全量 short + complex

| 查询 | base (ms) | opt (ms) | opt/base |
|---|---:|---:|---:|
| short-1 | 1.07 | 0.91 | 0.85 |
| short-2 | 3.66 | 3.58 | 0.98 |
| short-3 | 0.90 | 0.92 | 1.03 |
| short-4 | 1363.08 | 1356.44 | 1.00 |
| short-5 | 395.45 | 393.42 | 0.99 |
| short-6 | 397.34 | 395.57 | 1.00 |
| short-7 | 392.77 | 397.20 | 1.01 |
| complex-1 | 16.82 | 16.92 | 1.01 |
| complex-2 | 14.69 | 14.50 | 0.99 |
| complex-3 | 130.90 | 131.63 | 1.01 |
| complex-4 | 7.84 | 8.10 | 1.03 |
| complex-5 | 504.36 | 470.32 | **0.93** |
| complex-6 | 124.40 | 89.88 | **0.72** |
| complex-7 | 4.40 | 4.36 | 0.99 |
| complex-8 | 3.43 | 3.44 | 1.00 |
| complex-9 | 407.48 | 393.71 | 0.97 |
| complex-10 | 34.55 | 34.84 | 1.01 |
| complex-11 | 6.89 | 6.52 | 0.95 |
| complex-12 | 353.43 | 350.52 | 0.99 |
| complex-13 | 1.12 | 1.07 | 0.96 |
| complex-14 | 80651.68 | 75619.08 | **0.94** |

读法：

- complex-6 从 **124.4ms 降到 89.9ms（-28%）**，其计划中包含 `(post:Post)` 大集合目标标签过滤；
- complex-5、complex-14 也分别约 -7%、-6%，但未像 friend_msg 一样逐算子拆解，不能全部归因于同一路径；
- complex-3 整体仅 1% 波动：其核心 friend→message 步骤已 1.58×，但 City/Country/属性部分占主导；
- short-4..7 是更新/事务型查询（约 0.4–1.3s），标签过滤不参与，base/opt 基本一致；
- 其余 complex 在 ±3% 内，complex-4 约 +3% 属于小集合点查路径的边界波动。

## 5. 代价与边界

- 多标签过滤可能扫多张 `label_fwd`；小集合仍走点查；
- `neighbor_set`/`members` 内存 O(邻居数 + 命中标签数)；
- 大标签（如 Message 28 万）全扫是顺序 I/O，但若 neighbor 数很小，点查更便宜，故保留阈值；
- 标签前向索引与标签点查结果必须一致；由现有 `scanVerticesByLabel` 提供。

## 6. 验证

- `query_executor_tests`：584/584
- `eugraph_tests`：77/77
- 全量 ctest：**1304/1304**，含 `tck_tests`（723.90s）全部通过
- 上述 fresh import A/B 使用 baseline/opt 两个二进制，对同一份导入数据的两份副本交错测试

## 7. 后续

- 阈值可按实际标签基数自适应：例如 `label_count / neighbor_count` 决定扫描或点查；
- IN/OUT 邻接缓存已在 `feat/storage-vertex-data` 中实现，但默认关闭（N=0，`edge_index` 全量）；编码与实测见 [kv-encoding.md](../storage/kv-encoding.md)。本文的分析范围仍只限标签过滤；
- 本分支只提交标签过滤优化及本分析文档。

## 5. 无类型 Expand 的单次邻接扫描与计划显示

无类型的 `-[]->` 在 binder 阶段仍会把所有 edge label 放入 `BoundExpandOp.edge_label_ids`，
但执行层不应按每个 label 各做一次 `search_near`：

- `ExpandPhysicalOp::executeChunk()` 对含多个 label 的过滤集合折叠成一次无类型前缀扫描
  （`scan_filters = {nullopt}`），再在内存里按允许的 label 集合过滤返回的邻接项；
- 单个 label 仍走带 label 的前缀扫描，避免为少量边做全邻接扫描；
- **目标顶点已绑定时不折叠**：`scanEdgesExact` 的 `{src}{dir}{label}{neighbor}` 精确 seek 是
  同变量端点（`(a)-[r]-(a)`）过滤非自环边的唯一路径。折叠成无 label 扫描会绕过它，导致
  `(a)-[r]-(a)` 保留来自其它顶点的入边，并让无向自环出现两次。回归见 TCK Match7 [24] /
  Match8 [2] 和 `BoundSameVariableUndirectedSelfLoopKeepsOnlySelfLoop`。

计划文本同步反映这一语义：

- label 集合覆盖全部 edge label，或 operator 没有 label filter：显示 `labels=<any>`；
- 显式指定少量 label：按 id 排序后显示 `labels=[...]`，避免 catalog 遍历顺序带来的不稳定 diff；
- 显式空集合（不存在的类型）：显示 `labels=[]`。

回归测试 `QueryExecutorTest.ExplainUntypedExpandAndCountWithoutProperties` 覆盖：

- 无类型 Expand 显示 `labels=<any>`，不再输出 15 个 catalog id；
- `count(m)` 不生成只用于属性/标签物化的 `ProjectionExtract`；
- `count(m.name)` 才生成属性加载的 `ProjectionExtract`。

## 5. 按标签规模+目的端数估价的成本模型（2026-11-11）

`ffd1ffdd` 把固定阈值从 4096 提到 65,536（按**边数**），仍是单点折中：
每邻居 fetch 的成本按**去重后的目的端数**增长，标签扫描的成本按**该标签顶点数**
增长，用边数比较会把「多边指向少目的端」和「窄标签」两种情况估错。

现在的选择（`ExpandPhysicalOp::filterDstLabels`）：

```text
distinct = 本批 list 里不同的 dst_id 数
scan_rows = Σ ctx.label_vertex_counts[dst_label]        // planner 传入
fetch     ≈ distinct × 1.3 us
scan      ≈ scan_rows × 0.11 us
prefer_fetch = scan_rows > 0 ? distinct × 12 <= scan_rows
                             : distinct < 65536         // 计数未知时回退旧阈值
```

要点：

- 条件用 **distinct** 而不是边数：同一目的端被多条边命中时不会重复计价；
- 用标签规模定价后，窄标签（City/Country 只有 1k 级顶点）即使 10 万邻居也走扫描，
  不会因为放大固定阈值而出现 100ms 级回归；
- SF1 实测：80,607 条 `HAS_CREATOR` 边（80,607 个不同消息，过滤出 21,892 个 Post）
  原来走标签扫描，Expand 自身约 121ms；fetch 路径约 109ms（扣除 9ms 邻接扫描后
  约 100 vs 112ms）。complex-10 同会话采样由 360ms 降到 **345ms**；complex-14 在
  同一时段由 755ms 降到 **713ms**。本机频率漂移下绝对值有 ±5% 波动。

回归：`query_executor_tests` 635/635、`optimizer_tests` 124/124、
`verify_ldbc_results.py` 21/21 语义一致。

## 6. 扫描路径按算子 memo（2026-11-12）

`filterDstLabels` 每处理一个输入 chunk 调用一次，而扫描路径每次都会把**整张标签**
从头扫一遍。c11/SF1 的 `(friend)-[workAt:WORK_AT]->(company:Company)` 有 2,089 个
friend、18 个 chunk：4,580 条 WORK_AT 结果对应的却是 **17 × 1,575 = 26,775 行**
Company 扫描；`(company)-[:IS_LOCATED_IN]->(:Country)` 又叠加 41 × 111 行。

同一语句内一个标签的顶点集合不会变，因此改为按 operator 实例 memo：

- 扫描路径首次遇到某标签时，把该标签的完整 vid 集合存进
  `label_scan_memo_`（`unordered_map<LabelId, unordered_set<VertexId>>`），
  之后的 chunk 只做内存成员判断；
- memo 只缓存总量 ≤ `kMaxMemoisedLabelVids`（默认 65,536 vid，可用
  `EUGRAPH_LABEL_MEMO_MAX_VIDS` 覆盖）的标签；超预算的标签保持旧行为——
  只把自己 chunk 的邻居与扫描结果求交，不把大标签（如 100 万顶点的 Post）
  常驻内存；
- 判定逻辑不变（多标签仍要求全部命中），只是把「整标签扫描 + 求交」换成
  「整标签扫描一次 + 逐 chunk 成员判断」。
- 可观测性：`EXPAND` 的 `labelScanCount()` 只统计真实扫描次数，
  新增 `labelScanMemoHitCount()` 统计由 memo 命中的 chunk 数；
  两者与 `labelFetchCount()` 一起由 `resetLabelFilterCounts()` 清零。
  回归 `QueryExecutorTest.DestinationLabelScanIsMemoisedAcrossChunks` 强制
  `EUGRAPH_EXPAND_INPUT_ROWS=1`（10 个 chunk）断言扫描 1 次、memo 命中 > 0，
  并用 `EUGRAPH_LABEL_MEMO_MAX_VIDS=1` 断言超预算分支回到 10 次扫描、结果不变。

SF1 实测（同机交错 A/B，20 轮取 min，bench-sf1 数据）：

| 查询 | 改前 min | 改后 min | 变化 |
|---|---:|---:|---:|
| complex-11 | 39.20 | 36.92 | −5.8%（重复测 −4.2% ~ −8.0%） |
| complex-4 / complex-7 / complex-1 / complex-2 | — | — | ±1% 以内 |

complex-11 的 PROFILE：`totalScanRows` 37,744 → 约 9,000（Company 26,775→1,575，
Country 4,551→111），wall 35.8 → 29.9ms。

注意：c11 剩余时间仍在 2,572 行 `friend` 的属性/顶点点读（9.3ms，实测换成 3 个扁平
属性同样价）与 WORK_AT/IS_LOCATED_IN 两跳的逐源点查上；那部分属于「单次点读成本 +
单 IO 线程串行」问题，不在本 memo 的覆盖范围。
