# IndexProbe operator and rule split

> 状态：2026-11-27。R1/R2/R3 + physical lowering 已落地；未标注目标变量的索引覆盖
> 判定仍缺 catalog 元数据（见 §6）。

## Purpose

`Filter(Expand)` access paths need an index probe on the Expand destination.
The probe and the anchor subtree are separate concerns:

- the probe answers `IndexScan(target) -> Expand(target -> source)`;
- the anchor is the complete existing left pipeline (VLE, location filters,
  DISTINCT, ...).

A single rule must not re-select that anchor, and a shape rule must not choose
an index.

## Operators

```text
BoundIndexProbeOp        leaf probe descriptor (index range + one edge step)
BoundIndexProbeJoinOp    two child groups:
                           child_groups[0] = anchor (complete left subtree)
                           child_groups[1] = probe group (BoundIndexProbeOp leaf)
```

`BoundIndexProbeJoinOp` 的 inline `anchor` / `probe` 字段只是 `copyIn`/`copyOut`
的树传输（memo 内以 child group 为准）；`probe` 同时是 copyOut 回填描述符的
fallback。clone 保留 probe 描述符，anchor 由 child group 恢复。

## Rules

```text
rules/logical/generate_index_probe   R1: Filter(range on Expand.dst.prop) over Expand
                                        -> new probe group + ProbeJoin candidate
rules/logical/resolve_index_probe    R2: IndexProbe -> IndexProbe with catalog index
                                        (probe group only)
rules/physical/impl_rules            R3: ImplIndexProbe / ImplIndexProbeJoin tags
```

- **R1 只做形状**：不读 catalog（不选索引）、不算 cost、不选 winner；anchor 直接引用
  Expand 的 child group，不重新推导扫描。谓词的每个顶层 AND conjunct 必须落在同一个
  目标变量/属性上，否则不产生候选（不允许丢谓词）。
- **R2 只细化 probe group**：命中 `IndexProbe` 时按 catalog 解析 `index_id` /
  `index_prop` / label，新的探测描述符仍留在同一个 group。它看不到也不修改 anchor。
- **R3 只产 tag**：`ImplIndexProbeJoin` 手动构造 `required_input_mat`：anchor 输入继承
  group 需求，probe 输入为空（range 谓词已被描述符消费，不能让 Enricher 包住描述符叶子）。

## Physical lowering

`PhysicalPlanner::planIndexProbeJoin` 只消费已选定的 descriptor：

1. 用 `planBoundOperator` 原样 plan anchor child group；
2. 把 probe 描述符合成为普通算子树
   `Expand(src=target, dst=<unique alias>, direction=reverse(descriptor.direction),
   child=LabelScan(index_scan, index_id, range))`，复用既有 Expand/LabelScan 的
   label filter、方向、layout 与 PE dispatch；
3. 在该 source 变量上做 `HashJoin`：anchor 为 build 侧，index stream 为 probe 侧；
   probe 侧 join 列使用唯一 alias，避免与 anchor 的同名 SlotId 冲突。

`materializeChosen` 对 ProbeJoin 只展开 anchor 子树，probe 描述符从 probe group 的
chosen plan 取回。

## Invariants

- probe 描述符不含 anchor；
- ProbeJoin anchor 来自 existing Group，规则不重新推导扫描；
- rules 不算 total cost、不选 winner；
- physical planner 只 lowering 已选的 ProbeJoin/IndexProbe，不做 access-path 选择；
- 任何规则不得按 query id / 具体 ID / label 名特化。

## 可观测性

EXPLAIN 直接显示合成后的计划，例如 c3：

```text
HashJoin | build=left, keys=[friend = index_probe_source]
  <anchor: VLE -> city -> NOT city IN cities -> DISTINCT>
  Expand(src=message, dst=index_probe_source, labels=[HAS_CREATOR], direction=OUT)
    IndexScan(variable=message, label=Message, mode=range,
              range=[start..end], index=idx_Message_creationDate)
```

`index_probe_source` 列即 probe join 的标记。

## 6. 已知边界：无标签目标变量的覆盖判定

R2 目前只解析「目标变量有显式 label」的 probe（`(message:Message)`）。原因：

- c3 原文 `(friend)<-[:HAS_CREATOR]-(message)` 的目标变量无标签，
  `SlotSemantics.vertex_labels=[]`，`Expand.dst_label_ids=[]`；
- 属性 `creationDate` 的 property candidates 是 `Comment/Forum/Person/Post`，
  不含 `Message`；catalog 里 `Message#23` 与 `Post#24` 两个 creationDate 索引都是
  weak accessor；
- 选 `Message` 索引依赖「Post/Comment 顶点同时带 Message 标签」这个导入期
  `derived: ["Message"]` 约定，而 `GraphSchema`/`EdgeLabelDef` 没有持久化
  派生标签或边端点标签。

因此无标签目标保持 unresolved，不会猜 label（旧实现按 `label_name_to_id("Message")`
硬编码的路径已删除）。补齐所需的元数据：

1. loader 持久化派生标签（Post/Comment ⊂ Message）；
2. loader 持久化边端点标签（HAS_CREATOR src = Comment/Post）；
3. Binder 用边端点收窄无标签端点变量的候选 label，R2 用派生标签判断覆盖。

在此之前，c3 保持 forward `Filter(Expand)` 计划（结果正确，1 行）。

## 7. 回退开关

`EUGRAPH_DISABLE_PROBE_RULES=1` 关闭 R1/R2/R3 的注册，把搜索空间恢复到
ProbeJoin 之前（同一二进制做计划 A/B，或线上回退用）。默认开启。

## 8. 实测（SF1，c3 参数 personId=933 / Angola / Colombia）

同一二进制、同一数据、warm 4 轮：

| 形态 | 计划 | 结果 | min |
|---|---|---|---|
| `(message)`（原查询） | forward Expand + 逐行 load creationDate | 1 行（friendId=4607） | **1489.8 ms** |
| `(message:Message)`（诊断，语义等价） | ProbeJoin：range IndexScan + HashJoin | 1 行，内容一致 | **145.6 ms** |

forward 计划的 op-trace 显示 `Expand(friend->message)` 914,399 行、creationDate
PE ~1225 ms self_cpu；probe 形态把该段换成日期窗口 46,680 条索引命中。
带标签版本只是诊断，不是提交的查询特化；无标签版本要拿到同样收益需先完成 §6 元数据。
