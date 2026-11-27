# Physical planner 遗留 access-path / PE 决策清单

> 状态：2026-11-27。目标：`query/physical_plan` 只做 lowering（见
> [模块职责与代码归属](module-responsibilities.md)）。本文件把仍留在 physical planner
> 的“决策”逐项列出来，给出归属、迁移方式和建议顺序，避免用一次性大改硬拆。
>
> 参照实现：[IndexProbe operator and rule split](index-probe-operator.md)
> （R1 形状 / R2 只细化 probe group / R3 tag / lowering 只消费 descriptor）。

## 0. 判定标准

- 这个决定会**改变计划或代价** → optimizer（rule / descriptor / cost model）；
- 只是把已决定的计划**实例化** → physical lowering；
- 禁止按 query / label / 具体 ID 特化；
- 禁止 physical planner 用估算结果“改回”另一个计划（代价门禁、预算回退同属此类）。

## 1. 清单

| # | 决策 | 现状位置 | 为什么越界 | 目标归属 | 迁移方式 | 风险 |
|---|---|---|---|---|---|---|
| J1 | `Filter(join)` 的 join 算法（HashJoin vs CrossProduct+Filter） | `planBoundOperator` Filter 分支 → `tryPlanGenericHashJoin`（自带 `hash_cost < cross_cost` 门禁、build 侧选择、超预算回退） | 物理层比较代价并改计划；CBO 的 join tag 只被用来“别选 Cross”，没有被真正执行 | `ImplHashJoin` / `ImplBinaryJoin` 产 tag；lowering 只实例化 | 第一步已完成：BinaryJoin 分支按 chosen tag 实例化 HashJoin（`planHonoredHashJoin`）。第二步：把 Filter 上的等值 conjunct 吸收进 `join_conditions`，Filter 可删 | 高（join 语义） |
| J2 | `Filter(Expand)` → EdgeIndexScan | `tryBoundEdgeIndexScan` | 物理层选边索引 access path | `GenerateEdgeIndexScan` + descriptor + `ImplEdgeIndexScan` | 新增 `BoundEdgeIndexScanOp` | 中 |
| J3 | `Filter(CrossProduct(Unwind, LabelScan))` → `Apply(IndexScanValues)` | `tryPlanCorrelatedIndexProbe` | 物理层选索引 access path | rule + correlated-values descriptor | 新增 descriptor | 中 |
| J4 | `Filter(CrossProduct)` 的 `x.prop IN left.list` → ListIndexJoin | `tryPlanListIndexJoin` | 同上 | rule + descriptor | 新增 descriptor | 中 |
| J5 | VLE OR 过滤的 src/dst 索引 allow-set | `trySetVarlenOrFilters` + Filter 分支 | 物理层选索引 / 裁剪 | `optimizer/analysis` 计算 + VLE descriptor 字段 | 把 allow-set 变成逻辑字段 | 中 |
| J6 | HashJoin build 侧与预算回退 | `tryPlanGenericHashJoin` / `planHonoredHashJoin` / `planIndexJoin` | build 侧是实例化细节；但“超预算改计划”是物理层决策 | build 侧留在 lowering；超预算由成本模型提前排除，或明确报错 | 随 J1 收口 | 中 |
| J7 | PE / LateMaterialize 边界（`deferred_slots`、filter boundary） | `planBoundOperator` Filter / Limit 分支 | 物理层决定物化边界 | ChosenPlan/PhysProp enforcer（P2） | 先补 EXPLAIN 标记，再迁 | 高 |
| J8 | IndexJoin 规则内的 `source_card > 1000` 门禁、`build_left`；单步静态 probe 与 IndexProbe 规则族的职责重叠 | `GenerateIndexJoinRule`、`IndexJoin` tag 的 `input0 * 0.001` 成本 | 规则里做代价型裁剪 / 物理策略；同一个 access path 有两个候选，且 IndexJoin 的 `0.001` 让它在 CBO 里永远最便宜 | cost model + descriptor | 已做：单步静态 probe 交由 IndexProbe 族（IndexJoin 只保留 dynamic-key / 多跳链）。待做：IndexJoin 成本去 hack、`source_card` 门禁迁入成本模型 | 中 |

## 2. 已完成

- P1.4 普通 IndexScan：`GenerateIndexScan` + `ImplIndexScan`，physical 只实例化 descriptor；
- IndexProbe：R1/R2/R3 + `planIndexProbeJoin`（含两 child group memo 契约、hash/eq、LogProp、requirements、copyIn/copyOut/clone）；
- `GenerateIndexJoin` 从 `rules/physical` 归入 `rules/logical`，删除死 helper；
- **J1 第一步**：`planBoundOperator` 的 BinaryJoin 分支按 CBO 的 chosen tag 实例化 HashJoin
  （`planHonoredHashJoin`），不再无条件 CrossProduct；标签无法表达的形状回退 CrossProduct；
- access-path 候选的 RBO 边界：`Memo::copyOut` 跳过 ProbeJoin 候选（写查询常无 CBO
  winner，RBO 回退必须是可 lower 的计划）；R1 与 join commute 共用 `commuteSafe` 边界；
- **单步静态 destination probe 归一**：`GenerateIndexJoinRule` 只保留 dynamic-key /
  多跳链（它独有的能力）；单步静态 probe 由 IndexProbe 族负责。这同时修掉了旧
  IndexJoin 计划在 `collect(distinct 顶点) + UNWIND` 锚点下返回 0 行的问题（#335）：
  c9 由 0 行 → 20 行。代价：c2 计划从 IndexJoin 变为 ProbeJoin（853→1173 ms），
  实测（warm 3 轮 min）：c2 20 行 / 35.8 ms（旧 IndexJoin 853 ms）、c6 10 行 / 12.6 ms、
  c12 4 行 / 384.9 ms、c9 20 行 / 24.8 s（原 IndexJoin 0 行 5.6 s）。
- **单边界 range 不产生 probe 候选**：无索引分布统计时单边界 range 可能扫掉几乎整个
  label；R1 只接受有界区间/等值。c2/c9（`creationDate <= / <`）因此回到通用计划，
  c3（双边界）保留 probe。c9 仍慢是它本身的真实工作量（旧计划因 bug 只做了 0 行），
  不是这次改动引入的。
- **可观测性**：`EUGRAPH_CBO_TRACE=1` 打印每个候选的 `group/expr/tag/out_card/local/
  total/best`，这次的成本分析就是靠它完成的（此前 CBO 决策不可观测）。

## 3. 建议顺序

1. **J1 收口**：join_conditions 语义化（等值 conjunct 吸收进 join，Filter 只留残差），
   然后删除 `tryPlanGenericHashJoin` 的代价门禁/预算回退。属于 Cypher 语义面（T2），
   先单独评审。
2. J2 / J3 / J4 逐个 descriptor 化，每个都按 R1/R2/R3 拆分并配规则单测。
3. J5 / J7。
4. 每一步的验收：规则单测（候选形状 + group 引用）、相关 query tests、EXPLAIN 标记、
   结果与 neo4j/独立路径对照。

## 4. 相关缺陷

- [#335](https://github.com/IronsDu/eugraph/issues/335)：`collect(distinct 顶点) + UNWIND`
  之后接 `Expand` 返回 0 行（LDBC c9）。另含 `HashJoin build side exceeded memory row
  budget` 的物理层 build 侧估算问题，属于 J6。

## 5. 相关文档

- [模块职责与代码归属](module-responsibilities.md)
- [IndexProbe operator and rule split](index-probe-operator.md)
- [Optimizer / Access Path 后续规划](optimizer-next-steps.md)
