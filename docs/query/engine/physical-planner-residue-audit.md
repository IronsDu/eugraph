# Physical planner 遗留 access-path / PE 决策清单

> 状态：2026-11-27（职责审计）。目标：`query/physical_plan` 只做 lowering（见
> [模块职责与代码归属](module-responsibilities.md)）。本文件列出仍留在 physical planner
> 的“决策”，按**是否会改变计划形状/代价**分类，给出证据（函数）、目标归属和顺序。
>
> 参照实现：[IndexProbe operator and rule split](index-probe-operator.md)
> （R1 形状 / R2 只细化 probe group / R3 tag / lowering 只消费 descriptor）。

## 0. 判定标准

- 这个决定会**改变计划或代价** → optimizer（rule / descriptor / cost model）；
- 只是把已决定的计划**实例化** → physical lowering；
- 禁止按 query / label / 具体 ID 特化；
- 禁止 physical planner 用估算结果“改回”另一个计划（代价门禁、预算回退同属此类）。

## 1. 审计结论（2026-11-27）

仍存在 4 类越界，共 15 处（下表）。其中 **A 类（访问路径）和 B 类（连接算法/形状）
会直接改变执行计划**，是最该先迁的；C/D 类改变物化边界或做计划重写；E 类是语义反解
重复。F 类是正常的 lowering，不算越界。

### A. 直接改变访问路径（最严重）

| # | 函数 | 行为 | 目标归属 |
|---|---|---|---|
| A1 | `tryBoundEdgeIndexScan` | `Filter(Expand)` + 边属性谓词 → 选边索引，替换 Expand 为 EdgeIndexScan | `GenerateEdgeIndexScan` + descriptor + `ImplEdgeIndexScan` |
| A2 | `tryPlanCorrelatedIndexProbe` | `Filter(CrossProduct(Unwind, LabelScan))` → `Apply(IndexScanValues)`，并选索引 | rule + correlated-values descriptor |
| A3 | `tryPlanListIndexJoin` | `Filter(CrossProduct)` 的 `x.prop IN left.list` → ListIndexJoin，选索引并写 `ctx.expand_allowed_filter` | rule + descriptor |
| A4 | `trySetVarlenOrFilters` | `OR` 谓词 → VLE 的 `src_filter_index_id` / `dst_filter_index_id` allow-set | `optimizer/analysis` 计算 + VLE descriptor 字段 |
| A5 | `collectStaticPruneHints` / `collectStaticVertexPruneLabels` / `collectStaticEdgePruneLabels` | `IS NOT NULL` 谓词 → 限制扫描 label / 边 label（`prune_labels`、`static_prune_hints`）；用 `ctx.extraction_info` 反解 property→label | optimizer analysis（property demand / label pruning）+ scan descriptor |

补充：`GenerateIndexJoinRule`（现属 logical rules）内部仍有 catalog 兜底 `find_idx`
遍历所有 label 猜索引（J8 的一部分）；单步静态 probe 已迁出，dynamic-key / 多跳链仍用它。

### B. 改变连接算法 / 计划形状

| # | 函数 / 位置 | 行为 | 目标归属 |
|---|---|---|---|
| B1 | `tryPlanGenericHashJoin` | 物理层比较 `hash_cost < cross_cost`、选 build 侧、超预算回退 —— 直接推翻 CBO 选择 | 第一步已做（`planHonoredHashJoin` 执行 CBO tag）；第二步删除本函数（需要 join_conditions 语义化，T2） |
| B2 | `planHonoredHashJoin` | 执行 tag，但 build 侧按估算选、超预算回退 | build 侧可保留（实例化细节）；预算应由 optimizer 成本模型提前排除（J6） |
| B3 | BinaryJoin 分支的 `pc_anchor` 特判 | 形状匹配 `CrossProduct(CorrelatedSource, LabelScan)` → 替换为 `CorrelatedVertexProbePhysicalOp` | post-CBO rewrite 或专用 descriptor（optimizer） |
| B4 | `planIndexJoin` | 消费 descriptor，但内部仍有 dynamic-key 分支与 fallback | 保持 descriptor 化；bug 修复见 Issue #335 |

### C. 物化 / PE 边界（J7）

| # | 函数 | 行为 | 目标归属 |
|---|---|---|---|
| C1 | `computeFilterDeferredSlots` / `computeDeferredSlots` / `buildDeferredPlans` / `materializeDeferred` / `materializeDeferredAfterLimit` | 决定 PE 列在哪里 defer / 在 Limit 之后物化（filter boundary） | ChosenPlan / PhysProp enforcer（P2） |
| C2 | `findTopologyOnlyCollectedEntities` | 决定 `collect()` 的实体可停留在 topology 表示（抑制 PE） | optimizer analysis（representation/PE 决策） |
| C3 | `dispatchProjectionExtract` | 实例化 PE（读取物理层 deferral 状态） | 本身是 lowering，但依赖 C1 的决策 |

### D. Post-CBO 计划重写（在 physical 路径里做）

| # | 函数 / 位置 | 行为 | 目标归属 |
|---|---|---|---|
| D1 | `rewriteDstBoundExpandFilters` | 把 `id(dst)=id(saved)` 折进 Expand / 索引过滤；`planBound` 与 `planChosen` 都调用 | optimizer normalization（CBO 前） |
| D2 | `planChosen` 里调用 `optimizer::rewritePatternComprehensionAnchorShare` | post-CBO 计划重写由 physical 路径触发 | optimizer（提取 ChosenPlan 前） |

### E. 语义反解重复（P1.3 遗留）

| # | 函数 | 行为 | 目标归属 |
|---|---|---|---|
| E1 | `resolveColumnProperty` / `tryExtractBoundCondition` / `tryExtractNormalizedBoundCondition` | 依赖 `PlanContext.extraction_info` 把 BoundColumnRef 反解成 label/prop（optimizer/analysis 已有实现） | optimizer/analysis（P1.3 收尾） |
| E2 | `collectPlanRefsInto` / `countPlanFieldRefs` / `subtreeProvidesCorrelatedVariable` | 全树引用计数等规划分析放在 physical | optimizer analysis |

### F. 正常 lowering（不算越界）

- schema / layout / column_index、表达式编译、算子实例化；
- enricher/PropertyExtract 实例化（`applyEnrichInPlace` 合并 prop 需求是机械应用）；
- descriptor → 算子（IndexProbe / IndexJoin / 已选 tag 的 join）；
- `hashJoinMaxBuildRows()` 读环境变量（策略配置，保留）。

## 2. 已完成

- P1.4 普通 IndexScan：`GenerateIndexScan` + `ImplIndexScan`，physical 只实例化 descriptor；
- IndexProbe：R1/R2/R3 + `planIndexProbeJoin`（两 child group memo 契约、hash/eq、LogProp、requirements、copyIn/copyOut/clone）；
- `GenerateIndexJoin` 从 `rules/physical` 归入 `rules/logical`，删除死 helper；
- **B1 第一步**：BinaryJoin 分支按 CBO 的 chosen tag 实例化 HashJoin（`planHonoredHashJoin`）；
- access-path 候选的 RBO 边界：`Memo::copyOut` 跳过 ProbeJoin 候选；R1 与 join commute 共用 `commuteSafe` 边界；
- **单步静态 destination probe 归一**：`GenerateIndexJoinRule` 只保留 dynamic-key / 多跳链；单步静态 probe 由 IndexProbe 族负责（同时修掉 #335 的 c9 0 行）；
- **单边界 range 不产生 probe 候选**（无统计不猜）；`EUGRAPH_CBO_TRACE=1` 提供候选级成本可观测性；
- **J8 第一步（PlanProp / 按候选基数）**：`Winner` 记录每个候选自己的输出基数
  （`outputCardinality`），`LogPropDeriver::deriveCandidateCardinality` 用已 costed 的
  子 winner 基数推导父候选基数，O_INPUTS 的 local cost 与父估算都改用它。这样
  `Filter(LabelScan)→IndexScan` 的点查选择性（label × 0.01/0.1）能传给上层，不再把
  `id = 933` 估成整个 label。
- **IndexJoin 规则范围收敛为 dynamic-key**：静态反向路径（单步/多步）在没有索引分布
  统计时无法与 forward 计划做可信比较，不再产生候选（单步静态由 IndexProbe 族负责）；
  `source_card > 1000` 这个规则内代价门禁随之删除。dynamic 路径按"每 key 一次
  IndexScanValues seek"计价（`kDynamicProbeSeekCost`，命名并注明需要 P3 统计替换）。

## 3. 建议顺序

1. ~~J8 第一步：PlanProp（按候选基数）+ IndexJoin 规则范围/成本~~（已完成，见 §2）。
   剩余 J8：索引分布统计（P3）后重新启用静态反向路径、去掉 `kDynamicProbeSeekCost`
   占位；join 选择性/NDV 统计。
2. **A1–A5 逐个 descriptor 化**，每个都按 R1/R2/R3 拆分并配规则单测（下一步）。
3. **B1 第二步**（join_conditions 语义化、删除物理层 Filter-join 融合）—— Cypher
   语义面（T2），需单独评审。
4. **D1/D2 前移**到 optimizer（normalization / post-CBO 提取前）。
5. **C1/C2**（P2 PE enforcer 化）与 **E1/E2**（P1.3 收尾）。

## 4. 实测与教训（SF1，warm 3–4 轮 min）

| 查询 | 结果 | 耗时 | 说明 |
|---|---|---|---|
| c2 | 20 行 | 32.1 ms | 旧 IndexJoin 853 ms；静态反向路径不再产生候选 → forward |
| c6 | 10 行 | 10.8 ms | DynamicIndexJoin（多跳 + 动态 key）保留 |
| c12 | 4 行 | 369 ms | |
| c9 | 20 行 | 25.1 s | 旧 IndexJoin 0 行 / 5.6 s（正确性优先；真实工作量即慢） |
| c3 原文 | 1 行（4607/x=1/y=1/xy=2） | 1.50 s | 无标签，不猜 label |
| c3 诊断（`:Message`） | 1 行（内容一致） | 133.6 ms | 双边界 → IndexProbe |

**LogProp 实验（已回退）**：P1.4 把 Filter 折进 `BoundLabelScanOp.index_scan` 后，
`deriveLabelScan` 仍返回整个 label 的基数，点查选择性不会传给父节点（c2 的锚点被估成
9892 而不是 ~99）。只在 `deriveLabelScan` 里乘索引选择性，结果 c2 从 forward 38 ms
翻到多跳 IndexJoin 5.6 s —— 说明必须把「按候选的基数（PlanProp）」与 IndexJoin 成本
一起改（J8），单改 LogProp 只会让别的坏候选变便宜。

**可观测性**：`EUGRAPH_CBO_TRACE=1` 打印每个候选的 `group/expr/tag/out_card/local/
total/best`；上面的成本分析都依赖它（此前 CBO 决策不可观测）。

## 5. 相关缺陷

- [#335](https://github.com/IronsDu/eugraph/issues/335)：`collect(distinct 顶点) + UNWIND`
  之后接 `Expand` 返回 0 行（LDBC c9；旧 `planIndexJoin` 计划）。单步静态形状已移出该
  路径；`planIndexJoin` 本体 bug 未修（dynamic-key / 多跳链仍走它）。
- `HashJoin build side exceeded memory row budget`：build 侧预算仍是物理层估算（B2/J6）。

## 6. 相关文档

- [模块职责与代码归属](module-responsibilities.md)
- [IndexProbe operator and rule split](index-probe-operator.md)
- [Optimizer / Access Path 后续规划](optimizer-next-steps.md)
