# EXPLAIN 计划树设计

Status: 2026-11-23.

## 目标

`EXPLAIN` 输出面向用户，必须能回答：

- 这是什么算子；
- 它访问哪个标签/关系/属性/索引；
- 它的谓词、join key、排序键、聚合表达式是什么；
- 它的父子关系是什么（多个孩子不能被 flatten 成一条线）；
- 计划来自 CBO 还是 RBO。

内部列名（`__pe_*`）、裸 label/property ID 不应出现在默认输出中。

## 分层

```text
PhysicalOperator::explain(ExplainNode&, ExplainContext)
        ↓
ExplainBuilder：递归构造树，补 output schema
        ↓
ExplainFormatter：树形文本
```

`toString()` 保持短名，供 trace / `[PLAN]` 日志使用；用户 EXPLAIN 走 `explain()`。

## ExplainNode

```cpp
struct ExplainNode {
    std::string operator_name;
    std::vector<std::string> details;
    std::vector<std::string> identifiers;
    std::optional<double> estimated_rows;
    std::optional<double> cost;
    std::string plan_source;
    std::vector<std::unique_ptr<ExplainNode>> children;
};
```

`children` 保留树结构；formatter 输出：

```text
Limit(10)
└─ Sort(keys=[postCount DESC, tagName ASC])
   └─ Aggregate(groupBy=[tagName], aggs=[count(post) AS postCount])
      └─ Expand(src=post, dst=tag, labels=[HAS_TAG], direction=OUT)
         └─ NodeByLabelScan(variable=post, labels=Post)
```

## 名称解析

`ExplainContext` 携带 `GraphSchema`：

- `LabelId -> label name`
- `EdgeLabelId -> edge label name`
- `(LabelId, prop_id) -> property name`

找不到时使用显式 `<label#N>` / `<prop#N>`，不静默使用裸数字。

## 当前覆盖

默认实现只输出 `toString()`；以下 operator 提供语义 details：

- `NodeByLabelScan` / `AllNodeScan` / `IndexScan` / `EdgeIndexScan`
- `Expand` / `VarLenExpand`
- `Filter`
- `Project` / `Aggregate` / `Sort` / `Limit` / `Skip` / `Unwind`
- `CrossProduct` / `HashJoin` / `SemiJoin` / `LeftJoin` / `Apply`
- `PathBuild`
- `ProjectionExtract` / `LateMaterialize`

未覆盖 operator 仍有合理 fallback。

## 已知缺口

- 部分 lowered expression 已变成内部 `BoundColumnRef`，无法恢复原 property 表达式（例如 `message.id` 在 Project mapping 中显示为列名）；后续需要保留 explain-only provenance。
- `IndexScan` 的 index-id 构造路径在某些计划中没有 label name；formatter 会解析 schema index 表，解析不到时显示明确 placeholder。
- `estimated_rows` / `cost` 字段已预留，但物理算子还没有稳定的 carrier；当前只在根节点显示 `Plan source`。
- `output` 列只在 verbose 模式显示，默认聚焦语义 details。

## 测试

- `ExplainRendersSemanticTree`：树连接符、Plan source、标签名、Expand 关系/方向；
- `ExplainShowsFilterAndProjectExpressions`：Filter predicate、Project mapping；
- 已有 PropertyExtractPlanTest 更新为新 operators 名称/`materialize=` details；
- `EXPLAIN` 不执行查询，普通结果/TCK 不受影响。


## 5. Bolt plan metadata

`EXPLAIN` 在最终 `PULL` 的 SUCCESS metadata 中额外发送：

```json
{
  "plan": {
    "operatorType": "Projection",
    "args": { "Details": "..." },
    "identifiers": ["..."],
    "estimatedRows": 0,
    "children": [...]
  }
}
```

- `EXPLAIN` 同时仍返回 `Plan` 文本列，cypher-shell 文本输出不变；
- 普通查询不发送 `plan`；
- `PROFILE` 继续使用 `profile` 字段；
- 常见 eugraph 算子名映射为 Neo4j 风格：
  `Project -> Projection`, `Aggregate -> Aggregation`,
  `Expand -> Expand(All)`, `VarLenExpand -> VarLengthExpand(All)`,
  `LateMaterialize/ProjectionExtract -> Projection`, `LeftJoin -> Optional`。

Web Console 通过 `result.summary.plan` 渲染 Plan tab，提供 Graph / Tree / Raw
三种视图；Graph 采用居中盒子和对称分叉连线，Raw 显示完整 plan JSON，不需要解析
Plan 文本。

## 6. 客户端兼容性

| 客户端 | 数据来源 | 当前状态 |
|---|---|---|
| cypher-shell | `Plan` 文本列 | 文本树 |
| Web Console | Bolt `summary.plan` | Plan tab：文本 + 图形 |
| Neo4j Browser | Bolt `summary.plan` | 计划树可消费；完整 Browser 还需要系统过程/元数据兼容，未保证 |

Neo4j Browser 的 Plan 页依赖 `summary.plan`，但 Browser 本身还会调用
`dbms.procedures`、`db.labels`、`SHOW DATABASES` 等；这些不是本设计的一部分。
