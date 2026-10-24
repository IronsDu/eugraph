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
- 当前不做 IN/OUT 邻接缓存；缓存实验记录在独立研究文档，不进入本分支；
- 本分支只提交标签过滤优化及本分析文档。
