# PatternComprehension / EXISTS 锚点标签扫描

## 问题

complex-14 的 pattern comprehension：

```cypher
[(a:Person)<-[:HAS_CREATOR]-(:Comment)-[:REPLY_OF]->(:Post)-[:HAS_CREATOR]->(b:Person) ...]
```

eugraph 的计划从 `AllNodeScan(variable=a)` 开始，而不是 `LabelScan(a:Person)`。
结果是每个 pattern comprehension / 每个 path relationship 都要扫全图 327k 顶点，
sf0.1 complex-14 约 **80.5s**；Neo4j 同查询约 **20.9ms**。

## 根因

`bindExistsSubPlan` 为非相关锚点直接构造了：

```cpp
BoundScanOp scan;
scan.variable = start_var_name;
source_op = std::move(scan);
```

随后 `bindMatch` 看到 start 变量已经绑定，只把 `a:Person` 作为 **post-scan label filter**，
于是物理计划是 `AllNodeScan + Filter`，而不是 `LabelScan`。

这不是存储问题，也不是 `LabelScan` 本身的问题，而是 EXISTS/PatternComprehension 的
anchor source 构造路径绕过了 `bindNodePattern` 的标签解析。

## 改动

在构造 anchor source 时解析 start node 的标签：

```text
first_node.labels -> catalog_.labelNameToId
-> BoundLabelScanOp(label_ids, label_names)
```

没有标签时才回退 `BoundScanOp`；相关锚点仍走原有的 CorrelatedSource 路径。

代码：`src/query/planner/binder/bind_match.cpp`。

## 结果

complex-14（person1Id=933, person2Id=1129，sf0.1，同一数据/机器）：

| 版本 | 时间 | 行数 |
|---|---:|---:|
| 改前 AllNodeScan | 80.5s | 4 |
| 改后 LabelScan(Person) | **56.5s** | 4 |

`EXPLAIN` 已从 `AllNodeScan(a)` 变为 `LabelScan(variable=a, labels=Person)`。

## 仍未解决

LabelScan 把 anchor 从 327k 降到 1,528，但每个 path relationship 仍要展开该 anchor 的全部
Comment/Post 邻域；56.5s 仍远慢于 Neo4j 20.9ms。

下一步需要：

1. 把 `a.id = startNode(r).id OR a.id = endNode(r).id` 转成运行时 `Person(id)` index seek；
2. 或先以 VertexRef 过滤 anchor，再只对存活行做完整顶点物化；
3. 或做 PC 去相关（一次物化 Comment/Post 端点对，再按端点在路径上 join）。

仅靠 anchor LabelScan 只能到 56.5s，不满足 complex-14 目标。

## 验证

- `query_executor_tests`：584/584
- 全量 ctest：1304/1304（含 TCK）
