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
| A1 | ~~`tryBoundEdgeIndexScan`~~ | `Filter(Expand)` + 边属性谓词 → 选边索引，替换 Expand 为 EdgeIndexScan。**已删除**：它无视 Expand 的源约束，`MATCH (a:Person{id:933})-[r:KNOWS]->(b) WHERE r.creationDate>0` 在边索引 ONLINE 后返回 180623 条（正确 5 条）。若要做，必须是规则产出的 descriptor，并显式处理 endpoint 约束 | 删除（本可做：仅无约束源时用规则生成 descriptor） |
| A2 | ~~`tryPlanCorrelatedIndexProbe`~~ | `Filter(CrossProduct(Unwind, LabelScan))` → `Apply(IndexScanValues)`。**已删除**（physical planner 不再选该 access path）；per-key 探针的收益等 list/correlated-driven probe 的规则 + descriptor 落地后恢复（测试已改为语义契约） | 待规则化（list/correlated-driven probe descriptor） |
| A3 | ~~`tryPlanListIndexJoin`~~ | `Filter(CrossProduct)` 的 `x.prop IN left.list` → ListIndexJoin + Q12 形状硬编码重写（选索引、注入 expand allow-set）。**已删除**（约 400 行物理层计划重写，含 `ctx.expand_allowed_filter` 隐式通道与 `ListIndexJoinPhysicalOp`）；收益等 list-driven probe 的规则 + descriptor 落地后恢复 | 待规则化（list-driven probe descriptor） |
| A4 | ~~`trySetVarlenOrFilters`~~ | `OR` 谓词 → VLE 的 src/dst 索引 allow-set。**已迁出**：physical planner 不再识别 OR/选索引；单职责规则 `PruneVarlenOrFilters` 消费 Filter 并把两侧 allow-set 写进 VLE descriptor（两侧都必须可解析）。迁移过程发现并修掉框架级缺陷：VLE 的 allow-set 字段缺失于 content hash/eq，导致候选与未剪枝 VLE 去重 → `mergeGroups` 把父 group 并进子 group → 自引用 group → `copyOut` 爆栈 | 规则 + VLE descriptor（已完成） |
| A5 | ~~`collectStaticPruneHints` / `collectStaticVertexPruneLabels` / `collectStaticEdgePruneLabels`~~ | `IS NOT NULL` 谓词 → 限制扫描 label / 边 label。**已迁出**：顶点侧删除（弱空间下按 schema 裁剪不安全）；边侧改为逻辑规则 `PruneExpandEdgeLabels`（边是纯强模式，语义等价） | ~~optimizer analysis~~ 已解决 |

补充：`GenerateIndexJoinRule`（现属 logical rules）内部仍有 catalog 兜底 `find_idx`
遍历所有 label 猜索引（J8 的一部分）；单步静态 probe 已迁出，dynamic-key / 多跳链仍用它。

### B. 改变连接算法 / 计划形状

| # | 函数 / 位置 | 行为 | 目标归属 |
|---|---|---|---|
| B1 | ~~`tryPlanGenericHashJoin`~~ | 物理层比较 `hash_cost < cross_cost`、选 build 侧、超预算回退，直接推翻 CBO 选择。**已删除**：CBO 通过 `JoinConditionNormalization` 的 `join_conditions` + chosen tag 决定 join 算法；RBO/无 tag 时回退 CrossProduct+Filter（正确、可能慢）。`collectAndConjuncts`/`estimateRowsUpperBound` 的代价门禁随之删除（`estimateRowsUpperBound` 保留给 B2 的 build 侧实例化） | 已完成（删除） |
| B2 | ~~`planHonoredHashJoin` 的 build 侧/预算选择~~ | 执行 tag，但 build 侧按估算选、超预算回退。**已完成**：build 侧进入 `PhysicalExpr`/`ChosenPlan` 候选（`ImplHashJoinRule` 产出 build-left/right 两个候选），`findLocalCost(HashJoin)` 用 build 侧定价（build 行数带内存溢价）并做预算可行性（超 `EUGRAPH_HASH_JOIN_MAX_BUILD_ROWS` 或估计为 0 → `Cost::infinity()`）；physical 只转发 build 侧，运行时预算仍由算子强约束 | 已完成（optimizer 候选 + 成本） |
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
| D1 | ~~`rewriteDstBoundExpandFilters`~~ | 把 `id(dst)=id(saved)` 折进 Expand / 索引过滤。**已迁出**：CBO 前的 normalization pass `FoldExpandEndpointFilter`（`optimizer/normalization`），CBO 看到最终形状；因 CBO 的 FilterPushdown 可能在端点等式与 Expand 之间插入 edge-uniqueness 过滤，pass 会穿透"不引用 dst"的 Filter 链。physical 只 lowering 已折叠的 Expand | 已完成（normalization） |
| D2 | `planChosen`/`planBound` 调用 `optimizer::rewritePatternComprehensionAnchorShare` | post-CBO 计划重写由 physical 路径触发。**部分完成**：重写策略收口到 `optimizer::applyPostCboRewrites`（`optimizer/post_cbo/finalize`），physical 只调用这一个固定入口、不再选择/排列重写；完整前移到 CBO 前需要 `PatternComprehensionApply` 成为 memo/CBO 算子，或把 chosen plan 物化 + slot 分配迁入 optimizer（与 P2/C1 耦合） | optimizer（materialization 边界之后继续前移） |

### E. 语义反解重复（P1.3 遗留）

| # | 函数 | 行为 | 目标归属 |
|---|---|---|---|
| E1 | `resolveColumnProperty` / `tryExtractBoundCondition` / `tryExtractNormalizedBoundCondition` | 依赖 `PlanContext.extraction_info` 把 BoundColumnRef 反解成 label/prop。B2 后唯一用途是 `joinKeyColumn` 把 `join_conditions` 表达式映射成 lowering 时的列（列布局在 CBO 后才有）；需要 join-key descriptor（key 表达式 + 期望语义，列索引由 lowering 解析）才能删 | 待 join-key descriptor |
| E2 | ~~`collectPlanRefsInto` / `countPlanFieldRefs` / `subtreeProvidesCorrelatedVariable`~~ | 全树引用计数等规划分析。**已迁出**：`optimizer/analysis/plan_refs`（PlanRefIndex / countPlanFieldRefs / subtreeProvidesCorrelatedVariable / vertexRefIdArgument / expressionReferences / forEachPlanNode）；physical 的 topology-only 判断改为消费该 analysis（C2 本身仍待迁） | 已完成（analysis） |

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
- **A5 静态 label 裁剪归位**：physical planner 的 `static_prune_hints` 机制整体删除
  （`PlanContext` 字段、4 个 helper、Filter/AllNodeScan 两处应用）：
  - 顶点侧不再按 "property 声明 label" 裁剪扫描 —— 弱空间允许非声明 label 上存在值，
    按 schema 裁剪会丢行；
  - 边侧仍可裁剪且语义等价（边是纯强模式，无弱空间）：新增单职责逻辑规则
    `PruneExpandEdgeLabels`——`Filter(r.p IS NOT NULL)` 覆在 Expand 上时，把 Expand 的
    `edge_label_ids` 收窄到声明 p 的边 label（整条 Filter 被消费；混合谓词留给其他规则）。
- **A1 边索引捷径删除**：`tryBoundEdgeIndexScan`（physical planner 里唯一选择边索引的地方）
  在边索引 ONLINE 后返回**错误结果**（`MATCH (a:Person{id:933})-[r:KNOWS]->(b) WHERE
  r.creationDate>0`：180623 vs 正确 5）——它把 `Filter(Expand)` 整体替换为全局边索引扫描，
  没有保留 Expand 的源端点约束。已删除该函数与调用点，并加回归测试
  `EdgePropertyFilterKeepsBoundSource`（bound 源 1 条 / 无约束源 2 条）。边属性过滤回到
  通用 Expand+Filter 计划，结果正确。
- **D2 post-CBO 重写策略收口（部分完成）**：新增
  `optimizer/post_cbo/post_cbo_finalize`（`applyPostCboRewrites`），physical 的
  `planBound`/`planChosen` 只调用这个固定入口，不再直接决定 anchor-share 重写；
  `PatternComprehensionApply` 目前没有 CBO impl 规则，重写仍发生在 materialization
  边界（slot 分配在 physical）——完整前移与 P2/C1 绑定，已记录。
- **B2 HashJoin build 侧/预算归位（完成）**：`PhysicalExpr`/`ChosenPlan` 增加 `build_left`；
  `ImplHashJoinRule` 产出 build-left/right 两个候选；`findLocalCost(HashJoin)` 按 build 侧
  定价（build 行数 × 1.1 内存溢价 + probe 行数），并把预算作为候选可行性（build 超过
  `EUGRAPH_HASH_JOIN_MAX_BUILD_ROWS` 或估计为 0 → `Cost::infinity()`，由 CrossProduct 兜底）。
  `planHonoredHashJoin` 只转发 CBO 的 build 侧；physical 的 `estimateRowsUpperBound` /
  `hashJoinMaxBuildRows` 删除（预算统一为 `optimizer::joinBuildRowBudget()`，算子仍做运行时
  强约束）。测试：build-left/right 候选、build 侧成本、预算不可行、未知估计不可行；
  `GenericHashJoinCanBuildSmallerLeftSide` 断言 EXPLAIN `build=left`。
- **B1 join 形状兜底删除（完成）**：删除 physical 的 `tryPlanGenericHashJoin`（物理层
  hash/cross 成本比较、build 侧选择、预算回退）与 `collectAndConjuncts`/`estimateRowsUpperBound`
  代价门禁；join 算法完全由 CBO 的 join_conditions + chosen tag 决定，RBO/无 tag 时
  CrossProduct+Filter。`GenericHashJoinFallsBackWithResidualPredicate` 改为
  `GenericHashJoinKeepsResidualPredicate`（结果 + 残差 Filter，不再断言物理层回退）。
  `estimateRowsUpperBound` 保留给 B2 的 build 侧实例化；B2（build 侧/预算仍由
  `planHonoredHashJoin` 决定）与 E1（`resolveColumnProperty` 仍被 `joinKeyColumn` 使用）留待下一步。
- **D1 端点过滤折叠归位（完成）**：删除 physical 的 `rewriteDstBoundExpandFilters` 与
  `collectPlanRefsInto`/`countPlanFieldRefs`/`subtreeProvidesCorrelatedVariable`/
  `vertexRefIdArgument`/`bareRefName`；改为 CBO 前 normalization pass
  `FoldExpandEndpointFilter`（`optimizer/normalization`）+ 复用 `optimizer/analysis/plan_refs`。
  CBO 的 FilterPushdown 会在端点等式与 Expand 之间插入 edge-uniqueness 过滤，pass 会
  穿透"不引用 dst"的 Filter 链（等价性：中间 Filter 保持原样，仍作用在折叠后的 Expand 上）。
  EXPLAIN 对照：折叠前 `Filter(id(exists_dst)=id(exists_saved)) → Expand(dst=exists_dst)`，
  折叠后无该 Filter、`Expand(dst=exists_saved, probe=set)`；EXISTS 与 pattern-predicate
  两类形状都命中。LDBC 21 条行数不变。
- **E2 规划分析迁出（完成）**：`PlanRefIndex`/`countPlanFieldRefs`/
  `subtreeProvidesCorrelatedVariable`/`vertexRefIdArgument`/`expressionReferences`/
  `forEachPlanNode` 进 `optimizer/analysis/plan_refs`；physical 只消费。
  E1（`resolveColumnProperty` 等语义反解）仍被 B1 的 join key 解析使用，等 B1 删除。
- **A2 / A3 access path 删除（完成）**：physical planner 不再识别
  `Filter(CrossProduct(Unwind, LabelScan))` 等值探针（A2）与 `x.prop IN list` 的
  ListIndexJoin / Q12 硬编码重写（A3）；同步删除 `expand_allowed_filter` /
  `filtered_expand` 隐式通道、`ListIndexJoinPhysicalOp`（文件 + CMake）与 Expand 的
  allowed-dst 配置入口。相关测试改为**语义契约**（结果、重复 key、NULL、回退），
  不再断言具体物理算子。LDBC 21 条（complex-1..14 + short-1..7）行数与删除前逐条一致。
  性能回退（待规则化恢复）：UNWIND 参数列表逐 key 探针、complex-12/Q12 的
  Apply+IndexScanValues 形状。
- **A4 VLE OR 剪枝归位（完成）**：删除 physical planner 的 `trySetVarlenOrFilters`（识别
  `src.p = a OR dst.q = b`、选索引、写 VLE 字段）。新增单职责逻辑规则
  `PruneVarlenOrFilters`：消费该 Filter，把 src/dst 两侧 index allow-set 写进 VLE
  descriptor；**两侧都必须能解析**（只有单侧时算子的 OR 发射退化为恒真，不等价于 OR
  谓词，故不产生候选）。physical 只把 descriptor 字段交给现有 VLE 算子执行。
  迁移中发现并修复的框架缺陷：
  - VLE 的 `src/dst_filter_*` 字段缺失于 `operator_hash` / `operator_eq` → 剪枝候选与
    未剪枝 VLE 去重 → `Memo::mergeGroups` 把父 group 并进子 group → 自引用 group →
    `Memo::copyOut` 无限递归爆栈（gdb 确认）。已把字段纳入内容身份，并在 `mergeGroups`
    加"会形成自引用则拒绝合并 + 警告"的防御；
  - VLE `*0..` 无界 `max_hops=-1` 时估计为 0 行/0 成本（父比较全盲），改为按深度上限估计；
  - `cloneBoundLogicalOperator` 补 VLE/Expand 的 `dst_slot_id`/`planner_dst_slot_id` 与
    VLE 的 `src/dst_filter_*`（此前 clone 丢 slot 身份与 allow-set），加 clone 回归测试。
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
