# 邻接缓存与 vertex_data 后续研究

> 状态：研究记录，**不改变当前实现**。当前实现见 [kv-encoding.md](kv-encoding.md) 与
> [ldbc-snb-sf0.1-comparison.md](../benchmark/ldbc-snb-sf0.1-comparison.md) §8。

## 1. 当前结论

### 已落地的实际优化

1. **大结果集目标标签过滤**：`Expand` 的 `dst_label_ids_` 在 `edges.size() >= 4096` 时不再逐
   neighbor 调 `getVertexLabelsBatch`，改为顺序扫 `label_fwd_{id}` + 本批 neighbor membership 过滤；
   小集合保留点查。
   - friend_msg（complex-3 核心步骤）：**78.97ms → 33.3ms**；
   - 旧 main 同一步 60.63ms。
2. **hybrid 存储**：
   - `vertex_data`：标签 + 可选 OUT 邻接缓存；
   - `vprop_{id}`：每标签属性表；
   - `label_fwd` / `edge_index` / `vertex_existence` 不变。

### 不是性能优化

- `vertex_data` 的标签部分逻辑上等价于旧 `label_reverse`，只是多 kind 字节并与邻接共表；
  它**不是**标签过滤提升的来源。
- OUT 邻接 cache N=8 vs 0 在 sf0.1 LDBC complex-2..12 上无收益（部分略慢），默认关闭。
- 纯 X 属性合并没有稳定收益；complex-3 在属性密集路径上回退，因此保留 per-label vprop。

## 2. 受控 A/B：pure X vs hybrid

同数据、同 edge_index，仅属性布局不同（旧数据 + 标签迁移到 X）：

| 查询 | pure X | hybrid | hybrid/pureX |
|---|---:|---:|---:|
| complex-2 | 15.86 | 15.36 | 0.97 |
| complex-3 | 178.01 | 142.20 | **0.80** |
| complex-4 | 8.53 | 8.02 | 0.94 |
| complex-7 | 5.04 | 5.30 | 1.05 |
| complex-8 | 4.08 | 4.06 | 0.99 |
| complex-10 | 36.95 | 36.33 | 0.98 |
| complex-11 | 7.23 | 7.54 | 1.04 |
| complex-12 | 378.31 | 373.02 | 0.99 |

读法：

- B-tree 从 28MB 变 69MB 并不会让普通点查天然慢 25%；多数查询差 1–6%。
- complex-3 的 25% 差异说明属性密集、批量物化的路径对 page 局部性敏感，不能只用
  “B-tree 深度”解释。
- 因此 hybrid 是**针对 complex-3 类属性密集负载**的选择，不是普遍断言“per-label 一定更快”。

## 3. IN 邻接缓存实验（待实施）

### 3.1 动机

complex-3 的 `(f:Person)<-[:HAS_CREATOR]-(m:Message)` 是 IN 展开；当前 OUT-only cache 无效。
Person 的平均 HAS_CREATOR 入度约 `286,744 / 1,528 ≈ 187`。

如果要覆盖平均情况，需要 N≈200；当前 N=8 会立即 OVERFLOW。

### 3.2 候选设计

- 新增 IN 邻接缓存，键 `{v}{kind=IN_ADJACENCY}`，blob 格式与 OUT 相同：
  - state + 固定 32B entry；
  - count 由 value length 推导；
  - N 可配置。
- 写入：
  - `insertEdge(src,dst)` 维护 `src` 的 OUT 和 `dst` 的 IN；
  - `deleteEdge` 两边同步删除并压缩；
  - N+1 才置 OVERFLOW。
- 查询：
  - `Direction::IN` 且 cache COMPLETE 时直接过滤 blob；
  - OVERFLOW / BOTH / 无 cache 回退 edge_index；
  - 只在查询计划明确需要时传入 `const InEdgeCache*`，不做通用隐式读。

### 3.3 成本估算

- N=200：每顶点每条方向约 `1 + 200*32 = 6.4KB`；
  - 只对实际有边的顶点分配；但 Person 平均入度 187，大量顶点会接近满缓存；
  - 总缓存数据量需按顶点/边数实测，不能只用平均度估算。
- 一次性展开 58k 条边时：
  - 直接 edge_index 约 58k 条记录（~1–2MB 顺序/局部扫描）；
  - 缓存路径可能为每个 source 读取 200 条（6.4KB）blob；
  - 如果 source 数 × 6.4KB > edge_index 扫描量，cache 反而更慢。
- 高 degree 顶点仍会 OVERFLOW，IN cache 不能覆盖所有 LDBC 形状。
- 写路径：每条边维护两个 blob；并发插入同一顶点会产生 WT 写冲突（此前 4 连接 + cache 开启
  已实测 commit 失败）。

### 3.4 实验矩阵

| 配置 | OUT | IN | N | 目的 |
|---|---|---:|---:|---|
| A | off | off | 0 | 基线 |
| B | on | off | 8 | 已有 OUT-only 结论 |
| C | on | on | 8 | 小缓存双向 |
| D | on | on | 200 | 覆盖 Person 平均入度 |
| E | off | on | 200 | 隔离 IN 收益 |

固定 sf0.1、同数据、同 edge_index、8 compute，分别测：

- `friend<-HAS_CREATOR-message`（IN，58k 边）；
- complex-3 全查询；
- complex-6/9（标签过滤重的查询，确认 cache 不干扰）；
- 高 degree 展开（确保 OVERFLOW 回退正确且不严重退化）。

同时记录：

- 每个查询的 cache 命中/OVERFLOW 率；
- 加载 cache 的 blob 字节数；
- edge_index 扫描条数；
- 查询延迟、装载时间、数据目录大小、WT 写冲突/重试次数。

### 3.5 验收标准

IN cache 只有在以下条件同时满足时才值得保留：

1. 至少一个真实 LDBC 查询稳定提升 ≥10%；
2. 其他查询回退 ≤3%；
3. 装载时间/磁盘增长可接受；
4. 并发写入不出现不可恢复的 commit 失败（或实现重试）；
5. 高 degree/OVERFLOW 路径行为正确。

否则继续默认 N=0，仅保留研究记录。

## 4. 其他待研究点

- 是否把标签从 X 拆回独立的 compact `label_reverse`：
  - 当前大集合过滤已绕过 X 标签点查，收益主要来自 `label_fwd`；
  - 小集合点查和通用 `getVertexLabelsBatch` 仍可能从 compact 表受益。
- complex-3 属性路径的进一步优化：
  - per-label vprop 已避免 X 大表；
  - 是否需要对 City/Country/Message 的属性做批量预取/缓存，而不是每行点查。
- complex-14 的 PC 端点下推仍独立于本存储改动，未解决。

## 5. 当前默认

- `--adj-cache-capacity 0`
- 不自动读邻接 blob
- 标签过滤阈值 4096
- hybrid：X 标签/邻接，`vprop_{id}` 属性
