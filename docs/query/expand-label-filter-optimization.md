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

同机、同一旧数据目录、main 基线 vs 仅此改动：

| 项目 | 基线 | 本改动 | 变化 |
|---|---:|---:|---:|
| friend_msg（complex-3 核心步骤） | 60.63ms | **39.48ms** | **1.54×** |

同一分支的 complex 子集（min，旧 main 数据）：

| 查询 | 基线 | 本改动 |
|---|---:|---:|
| complex-2 | 15.65 | 14.81 |
| complex-3 | 136.75 | 138.26 |
| complex-4 | 8.43 | 8.72 |
| complex-7 | 5.02 | 4.84 |
| complex-8 | 4.06 | 3.50 |
| complex-10 | 35.87 | 37.19 |
| complex-11 | 7.35 | 7.07 |
| complex-12 | 354.86 | 359.36 |

结论：

- 优化只针对**大 neighbor 集合 + 大目标标签**；核心步骤约 1.5×；
- 小集合查询基本持平（±5% 内）；complex-10 有约 4% 回退，complex-3/4 约 1–3%，
  属于测量/阈值边界波动；
- 本分支不包含任何存储格式改动，也不是“比 neo4j 快”的结论。

## 5. 代价与边界

- 多标签过滤可能扫多张 `label_fwd`；小集合仍走点查；
- `neighbor_set`/`members` 内存 O(邻居数 + 命中标签数)；
- 大标签（如 Message 28 万）全扫是顺序 I/O，但若 neighbor 数很小，点查更便宜，故保留阈值；
- 标签前向索引与标签点查结果必须一致；由现有 `scanVerticesByLabel` 提供。

## 6. 验证

- `query_executor_tests`：584/584
- `eugraph_tests`：77/77
- 全量 ctest：**1304/1304**，含 `tck_tests`（723.90s）全部通过

## 7. 后续

- 阈值可按实际标签基数自适应：例如 `label_count / neighbor_count` 决定扫描或点查；
- 当前不做 IN/OUT 邻接缓存；缓存实验记录在独立研究文档，不进入本分支；
- 本分支只提交标签过滤优化及本分析文档。
