# Optimizer / Access Path 后续规划

> 状态：2026-11-28。
> - P0 / P1 已完成并合入；
> - P1.3 / P1.4 已完成；IndexProbe（R1/R2/R3 + lowering）已落地；
> - A1–A5 访问路径物理决策已删除/迁移；**D1**（correlated 端点过滤折叠）与 **E2**（规划分析）
>   已归位到 optimizer（2026-11-28）；
> - **P2**（2026-11-28）：correlated 端点由 binder 直接生成 bound dst 形态（不再造 Filter 再
>   折叠）；probe 只是 optimizer 的 `AnnotateCorrelatedEndpointProbe` 标注，形状/标注分离；
> - **B1**（join 形状兜底）已删除：CBO 的 join_conditions + tag 决定算法；
> - **B2**（HashJoin build 侧/预算）已归位：build 侧是候选属性，预算由成本模型排除；
> - D2 部分完成：post-CBO 重写策略收口到 `optimizer::applyPostCboRewrites`（触发点仍在
>   materialization 边界，完整前移与 C1/P2 绑定）；
> - 剩余：C1/C2（PE 物化边界）、E1（join-key descriptor）——见 `physical-planner-residue-audit.md`。
> - 无标签目标变量的索引覆盖判定仍缺元数据（见 P4）。
> - 本文把 optimizer 债务与既有执行层性能待办放在同一优先级队列里。

## 0. 规则归位计划（2026-11-28）

> 原则：**会改变计划/访问路径/join 形状/物化边界的优化决策 → `optimizer/rules/logical`
> 候选（CBO 用成本选）；physical 的 Impl 规则只产 `PhysicalOpTag`/属性，lowering 只实例化；
> normalization 只保留不改变选择的语义规范化/标注（binder 生成物的 canonicalization）。**

| 阶段 | 内容 | 目标模块 | 状态 |
|---|---|---|---|
| P1 | 删除 pre-memo `applyIndexAccessPaths` 调用；applier 移出 `analysis/`，只由 `GenerateIndexScanRule` 使用 | `rules/logical/generate_index_scan` | **完成** |
| P2 | D1 端点折叠从 normalization 移到 **binder**（binder 直接生成 bound dst 形态，不再造 Filter 再折叠）；probe 标志作为策略标注/后续规则 | `planner/binder` | **完成**（probe 由 optimizer 标注；Impl 候选待 P3 统计） |
| P3 | `ShortestPathBatch` 策略标志改为 Impl 候选 + 成本（B2 模式）；`JoinCondition` 提取保留为语义规范化（不选择，只把等值谓词挂到 join_conditions）并写入契约 | `rules/physical` + `normalization` | 待做 |
| P4 | 停止扩充 `post_cbo` 并按计划移除：anchor share 要么成为 logical rule（需 PC Apply/Fork 进 memo：LogProp、Impl、copyIn/copyOut、requirements），要么删除；B3 probe 随之重新设计 | `rules/logical`（或删除） | 待做 |
| P5 | C1/C2（PE 物化边界 → PhysProp/enforcer）、E1（join-key descriptor）；B3 依赖 P4 | optimizer | 待做 |

## 1. 目标边界

```text
Binder
  → 用户 SlotId / SlotSemantics 定稿

Optimizer
  → 选择 access path / join 算法 / 物化边界
  → ChosenPlan 携带完整 descriptor

Physical planner
  → 只实例化 PhysicalOperator
  → schema / slot_layout / column_index / 表达式编译
  → internal PE slot 分配
```

`physical planner` 不应再：

- 选择 forward/reverse index；
- 选择 HashJoin/IndexJoin tag；
- 分配用户 SlotId；
- 重新推断 property 语义；
- 决定 PE / LateMaterialize 边界。

## 2. 优先级队列

### P0 — #331：IndexJoin descriptor + 删除 tryPlan*（已完成）

当前缺口：

- 已有 `ImplIndexJoinRule` / `PhysicalOpTag::IndexJoin` 入口；
- `tryPlanFilterDestinationChain` / `tryPlanFilterDynamicDestinationChain` 仍在
  physical planner 中做形状识别和 fallback；
- c9 的 property alias、c6 的动态 key 仍依赖 physical helper。

完成条件：

- `GenerateIndexJoinRule` 生成 `BoundIndexJoinOp` descriptor；
- `ImplIndexJoinRule` 只负责 tag；
- physical lowering 只消费 descriptor；
- 删除 `tryPlan*`；
- c6/c9/c2 结果一致，SF1 不回归。

### P1 — 动态 IndexJoin 的 shared-source 语义（已完成）

动态 key 路径已改为 `DynamicIndexJoinPhysicalOp`：

- source 是 owning child，只执行一次；
- 同一次扫描同时构建 source hash table 和 distinct index keys；
- 删除 `ReexecutePhysicalOp`；
- EXPLAIN 直接显示 `DynamicIndexJoin` 和两个 child；
- 当前按 `max_build_rows` 限制内存，超限明确报错。

详细设计与后续 spill 路线见
[Dynamic IndexJoin 单次 Source 消费](dynamic-index-join-shared-source.md)。

**后续 P1.2**：`SpillableRowStore` → 按 join key 分区 → 磁盘 spill；
预算按字节、成本模型计入 spill；source 仍只执行一次。

### P1.1 — Slot ownership 收尾（待开始）

- optimizer 规则生成 Project/alias 时同步创建/继承 SlotId；
- 删除 physical planner 的 `allocateAllSlots` backfill；
- 明确 user slot / internal column 的分配者；
- 增加“optimizer 后不存在未登记用户 slot”的校验。

### P1.3 — IndexCondition extraction resolver（已完成）

`optimizer/analysis/index_condition` 已提供 `IndexableCondition` /
`ResolvedProperty` / `ColumnPropertyResolver` / normalized extraction；
physical planner 删除重复实现，只保留 `PlanContext.extraction_info` 到 resolver
的 adapter。

**问题**：`tryExtractBoundCondition` / `resolveColumnProperty` 仍留在 physical planner，
因为把 `BoundPropertyRef` / `BoundColumnRef` 还原为 `{label, prop_id, property_name,
strong}` 需要 `PlanContext.extraction_info`（PEPlan 的 `prop_slot_ids` / `prop_order` /
coalesce）。

这使 range 合并/inclusive 已进入 `optimizer/analysis/index_condition`，但条件“是谁的
property”这一步仍依赖 physical 状态。

**目标**：

- 提供不依赖 `PlanContext` 的 `IndexConditionResolver` 接口；
- `BinderSlotSemanticResolver` 消费 SlotSemanticMap；
- `PEPlanResolver` 消费 PEPlan（physical 适配器）；
- extraction + merge + inclusive 统一进 optimizer/analysis；
- physical planner 只把描述符实例化为 IndexScan。

**验收**：c3、string/int inclusive range、c2/c6/c9 及 689 tests 通过；physical planner
不再包含 property 反解实现。

### P1.4 — 普通 IndexScan access path 迁入 optimizer（已完成）

**问题**：`Filter(LabelScan) → IndexScan` 仍在 physical planner 的
`tryBoundIndexScan` 中完成。CBO 选择的是 `Filter(LabelScan)`，physical planner
之后读 `PlanContext.extraction_info`、catalog，单方面改成 IndexScan；CBO
没有比较过 `LabelScan+Filter` vs `IndexScan` 的成本，也没有看到最终 access path。

**目标**：

- 新增 `GenerateIndexScanRule` / `BoundIndexScanOp` descriptor：
  label、index_id、accessors、eq/lower/upper、inclusive、residual；
- CBO 规则使用 `IndexCondition` 和统计/选择性，比较 LabelScan 与 IndexScan；
- `ImplIndexScanRule` 输出 `PhysicalOpTag::IndexScan`；
- physical planner 只实例化 descriptor；
- 删除 `tryBoundIndexScan` 及 `PlanContext.extraction_info` 条件反解；
- RBO fallback 也要走同一个 optimizer access-path pass，而不是 physical 决策。

**验收**：c3、inclusive int/string、c2/c6/c9、689 tests；physical planner 不再查
catalog 选索引。

**结果**：`applyIndexAccessPaths` + `GenerateIndexScanRule` + `ImplIndexScanRule` 已落地；
`tryBoundIndexScan` 已删除；IndexScan cost 读取 descriptor。

### P2 — PE / LateMaterialize enforcer 化（待开始）

- PE 边界由 ChosenPlan/PhysProp 描述；
- physical planner 只实例化 enforcer；
- LateMaterialize 同样从计划描述驱动；
- 解决当前 physical planner 自行决定 PE 边界的问题。

### P3 — degree / selectivity 统计（待开始；先设计评审）

- `(edge_label, src_label, dst_label)` 边数；
- in/out degree；
- index selectivity；
- StatsDelta / snapshot / journal / Catalog 全链路；
- 先定粒度、写时维护 vs ANALYZE、兼容和回退，再实现。

### P4 — c3：无显式 label 的父 label 索引（待开始，IndexProbe 的最后一块）

IndexProbe 的 R1/R2/R3 + physical lowering 已落地（见
[IndexProbe operator and rule split](index-probe-operator.md)）。目标变量带显式 label 时
已能选中并下推 probe（SF1 c3 加 `:Message` 的诊断：1489.8 → 145.6 ms，1 行不变）。

剩余阻塞：无标签目标变量的索引覆盖判定。实测 catalog 证据：

- c3 的 `message` 变量 `vertex_labels=[]`、`Expand.dst_label_ids=[]`；
- `creationDate` 的 property candidates = `Comment/Forum/Person/Post`（不含 Message）；
- `Message#23` 与 `Post#24` 的 creationDate 索引都是 weak accessor；
- `GraphSchema`/`EdgeLabelDef` 未持久化 loader 的 `derived: ["Message"]`，也没有
  边端点标签。

因此需要（按顺序）：

1. loader 持久化派生标签（Post/Comment ⊂ Message）；
2. loader 持久化边端点标签（HAS_CREATOR src = Comment/Post）；
3. Binder 用边端点收窄无标签端点变量的候选 label；
4. R2 用派生标签判断单索引覆盖，覆盖成立才产生 probe 候选。

在此之前，无标签 c3 保持 forward 计划（正确、1 行、~1.49 s），不猜 label。

### P5 — EXPLAIN 全面使用 SlotSemantics（部分完成）

- `IndexScan properties=[<prop#N>]` → 属性名；
- `Filter(message < ...)` → `message.creationDate < ...`；
- Projection load/materialize 显示完整属性；
- Bolt plan / Web Graph 同步受益。

### P6 — 既有执行层性能待办（待开始）

延续 [LDBC 执行层优化计划](../../benchmark/ldbc-storage-unchanged-optimization-plan.md)：

- c5：批量边属性读、friends 下推、semi-join；
- c14：PC 公共前缀/共享邻居缓存；
- c10：去相关/展开顺序；
- c12：VLE 闭包早剪枝；
- c9/c3：在索引/统计接入后重新评估；
- PE/Expand 分配与 WT search 批量化。

## 3. 验收原则

- 优化器规则产生候选，不调用手工过程；
- physical planner 不做 access path 决策；
- 同二进制 A/B、结果逐行一致；
- 每条规则有 explain 标记/可观测性；
- 最后统一跑全量测试与 SF1 benchmark，不逐项跑 CI。
