# EXPLAIN 语义元信息

> 状态：2026-11-25。EXPLAIN 读取 Binder SlotSemantics、PEPlan 和 Catalog，
> 把逻辑属性、PE 派生列和索引都渲染成用户可读名称；不改变计划与执行。

## 1. 数据流

```text
Binder
  SlotSemantics (user slot → variable/property)

Physical planner
  PEPlan (internal slot → property/object/labels/type/coalesce)
  buildExplainSymbols → ExplainSymbolMap

ExplainContext
  slot_semantics
  symbols (user + PE internal)
  schema / catalog

formatExplainExpression / operator::explain
  → text tree / Bolt plan
```

`ExplainSymbolMap` 的 key 统一为 `SlotId`：

- user slot：来自 Binder；
- internal PE slot：来自 `PEPlan.prop_slot_ids` / `object_slot_id` /
  `labels_slot_id` / `type_slot_id` / `coalesce_vertices`。

找不到符号时回退旧格式（名称/`#slot`/`<prop#N>`），不抛异常。

## 2. 渲染规则

```text
BoundColumnRef(user variable)     → variable
BoundColumnRef(property)           → owner.property
BoundColumnRef(internal PE prop)   → owner.property
BoundColumnRef(whole object)       → owner
IndexScan properties               → catalog property name
IndexScanValues                    → label + index + property name
ProjectionExtract load/materialize → owner.property
```

示例：

```text
# before
IndexScan(variable=message, label=Message, properties=[<prop#2>], mode=range)
Filter(predicate=(message <= 1287230400000))
Project(mapping=[message AS postOrCommentId, coalesce(message, message) ...])

# after
IndexScan(variable=message, label=Message, properties=[creationDate], mode=range)
Filter(predicate=(message.creationDate <= 1287230400000))
Project(mapping=[message.id AS postOrCommentId, coalesce(message.content, message.imageFile) ...])
```

## 3. 覆盖范围

- c2：`message.creationDate`、`coalesce(message.content, message.imageFile)`；
- c5：`membership.joinDate`、`forum.title`；
- c6：`IndexScanValues(label=Tag, index=idx_Tag_id)`；
- c9：`properties=[creationDate]`、`message.creationDate`。

全量 LDBC EXPLAIN 快照对比：

- `<prop#N>` 占位符：3 → **0**；
- `index_id=N` 未解析索引：6 → **0**；
- 29 条 interactive 语句（含 update）全部成功生成计划。

仍保留的物理标识：

- `__anon_*`：binder 生成的匿名变量；
- `__index_join_src`：IndexJoin 内部 join key 别名；
- `ReexecuteSource`：动态 key 的 shared-source 债务（见
  [optimizer-next-steps](optimizer-next-steps.md) P1）。

## 4. 扩展方式

- 新用户语义：在 Binder `SlotSemantics` 增加字段，`formatExplainExpression` 消费；
- 新 PE 列：在 `buildExplainSymbols` 注册 internal slot；
- 新索引/属性来源：扩展 `explainPropertyName` / `explainEdgePropertyName`；
- operator 只在 `explain()` 中追加语义 detail，不解析 slot。

## 5. 验证

```text
query_executor_tests  687/687 passed
ExplainUsesSlotAndPeSymbols
```

新增测试断言 `n.name` 出现且不存在 `<prop#`。
