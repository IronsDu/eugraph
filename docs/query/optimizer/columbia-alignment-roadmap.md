# Columbia 对齐改造路线图

> 本文档记录 eugraph Cascades 优化器与 Columbia 参考实现（`/home/dodo/code/fuck/columbia`）逐项核对后
> 发现的差异、修复进度与验证方式。设计总览见 [cascades-optimizer.md](cascades-optimizer.md)。

## 状态约定

- [ ] 未开始
- [x] 已修复并有用例覆盖
- 每个修复必须满足：**修复前用例失败（红），修复后用例通过（绿）**。

## 问题清单与进度

### P0 — 会产生错误查询结果

- [x] **P0-1 FilterPushdown 错误穿透 LIMIT/SKIP（含 Binder WHERE 位置修正）**
  - 根因：`FilterPushdownRule::isPenetrable` 曾把 `Skip/Limit` 视为“schema 不变即可穿透”；同时
    `Binder::bindWith` 在存在 SKIP/LIMIT 时把 WHERE 提前放到了 Project 之后、Limit/Skip 之前。
    两处都会造成 `Filter(Limit(...)) -> Limit(Filter(...))` 的错误语义。
  - 复现：
    - `MATCH (n:Person) WITH n LIMIT 1 WHERE n.age > 25 RETURN n.name` 返回了应被 LIMIT 截掉的行。
    - `MATCH (n:Person) WITH n SKIP 1 LIMIT 1 WHERE n.age > 25 RETURN n.name` 返回空。
  - 用例：`FilterPushdownTest.FilterAboveLimitNotPushed` /
    `FilterPushdownTest.FilterAboveSkipNotPushed`；
    `QueryExecutorWithTest.WithLimitBeforeWhereKeepsFilterAboveLimit` /
    `QueryExecutorWithTest.WithSkipLimitBeforeWhereKeepsFilterAboveSkip`。
  - 相关文档：`cascades-optimizer.md` 第六节可穿透性表需要同步修正。

### P1 — Memo / 任务状态机正确性

- [x] **P1-1 `mergeGroups` 重路由 child_groups 后未重建全局哈希**
  - 根因：`memo.cpp::mergeGroups` 只改 `GroupExpr::child_groups`，`hash_table_` 仍按旧
    `(op, old_child_groups)` 索引；`unregisterInHash` 从未被调用。
  - 用例：`GroupMergeTest.FindDuplicateSurvivesAfterChildReroute`。
- [x] **P1-2 `mergeGroups` 未合并 Winner-circle 与 Group 状态**
  - 根因：只搬 `logical_exprs/physical_exprs`，`winners/requirements/explored/optimized` 全部丢弃。
  - 用例：`GroupMergeTest.WinnersMergedIntoSurvivor`。
- [x] **P1-3 `ApplyRuleTask` 缺少 Last 收尾析构**
  - 根因：`OExprTask` 把 `last_` 转交给最后一条 `ApplyRuleTask` 后，如果该规则
    `condition()==false` 或 substitute 全部重复，`last_` 无人接手，group 永远停在
    `exploring/optimizing=true`。Columbia 在 `APPLY_RULE::~APPLY_RULE` 中收尾。
  - 用例：`TaskStateTest.LastApplyRuleConditionFailureStillClosesExploration`。

### P1 — 内容感知哈希 / 相等性

- [x] **P1-4 hash/eq 漏掉语义字段导致错误 Group 合并**
  - 已确认漏字段：
    - `BoundColumnRef`: `slot_id`、`scope_id`（违反 `SlotId = semantic binding` 不变量）。
    - `BoundCorrelatedSourceOp`: `slot_ids`。
    - `BoundExpandOp`: `src_slot_id/edge_slot_id/dst_slot_id`、`dst_label_ids`。
    - `BoundVarLenExpandOp`: `dst_slot_id/edge_slot_id`、`dst_label_ids`。
    - `BoundMergeOp`: `on_create_items`、`on_match_items`。
    - `BoundCallOp` hash 漏 `output_types`（eq 已比较）。
  - 规划器内部字段 `planner_*_slot_id` 不在逻辑等价键中，保持排除。
  - 用例：`MemoContentHashTest.DistinctColumnRefSlotsDoNotMerge`、
    `MemoContentHashTest.DistinctExpandDstLabelsDoNotMerge`、
    `MemoContentHashTest.DistinctMergeSetItemsDoNotMerge`、
    `MemoContentHashTest.DistinctCorrelatedSourceSlotsDoNotMerge`、
    `MemoContentHashTest.DistinctVarLenDstLabelsDoNotMerge`。

### P1 — CBO 属性需求与 Enforcer

- [x] **P1-5 O_INPUTS 将父 materialization 需求全量下传给每个输入**
  - 根因：`inputRequiredProp` 对每个 child 都 `merge(localReqdMat)`，Join 左子树会被要求物化
    右子树变量，反之亦然。Columbia 对应 `PHYS_OP::InputReqdProp` 按输入 schema 计算。
  - 用例：`EnricherE2ETest.MultiVariableJoinProducesChosenPlan`。
- [x] **P1-6 Enricher 多变量未链式化，且注册 winner 前未校验 provided.satisfies(required)**
  - 根因：`insertEnricherEnforcer` 每个 enforcer 的 child 都是原 group，且只返回最后一个；
    父任务随后把“只提供部分变量”的 enforcer 注册为满足完整需求的 winner。
  - 修复方式：多变量合并为一个 `output_mat` 覆盖全部变量的 enforcer（`materializeChosen`
    已支持按 `enrich_output` 整表 lower）；注册前校验提供物满足需求。
  - 用例：`EnricherE2ETest.MultiVariableJoinProducesChosenPlan`（同一用例覆盖 P1-5/P1-6）。
- [x] **P1-7 全对象 Enricher（Vertex/Edge/PathEnrich）lowering 对 alias/预绑定变量尚不安全**
  - 表现：`QueryExecutorTest.TckWith7Scenario1BoundEndpoint` 在启用 CBO chosen 后返回错误端点；
    RBO 路径通过。根因在 `planChosen` 的全对象 Enricher 下推逻辑与别名/预绑定 edge 的相互作用。
  - 处置：`LogicalOptimizer::optimize` 发现 chosen 树含全对象 Enricher 时置空 `plan.chosen`，
    回退 `planBound`。扁平 `*PropertyExtract` 路径保持启用。
  - 用例：`QueryExecutorTest.TckWith7Scenario1BoundEndpoint`（回归）；后续完善 lowering 后移除 gate。

### P2 — 性能 / 能力增强（待排期）

- [x] **P2-1 Context upper bound 不随 winner 更新收紧**（Columbia `tasks.cpp:1384`）。
- [x] **P2-2 `searchCircle` 未校验 winner done**（非 null 未完成 winner 不再当作最终计划）（Columbia `ssp.cpp` search_circle 有 assert）。
- [ ] **P2-3 O_GROUP 非 any 属性路径缺失**（Columbia O_GROUP 的 enforcer + any-context 重优化）。
- [x] **P2-4 新表达式创建路径绕过 dedup**（`createGroupWithExpr`、`insertPhysExpr` 不查重）。
- [x] **P2-5 `copyOut(prop)` exact→satisfying winner 回退已修复**（子节点属性传播仍未完全镜像 `extractChosen`）。
- [x] **P2-6 `ImplUnionRule` 使用 `CrossProduct` tag**（新增 `PhysicalOpTag::Union`；BinaryJoin 执行路径仍为 CrossProduct）。

- [ ] **P2-7 BINDERY 多 binding / rule subsumption mask 未实现**（`OptRule::fullMatch` 当前无调用）。
- [x] **P2-8 `LogicalOptimizer` 重复调用会重复注册 rules 并复用 Memo**（optimize 入口重置 memo/rules）。

## 已确认的验证基线

- 修复前：
  - `optimizer_tests` 84 例全过，但新增的 P0/P1 用例 9 项全部红（Filter×2、Merge×2、TaskState×1、Hash×5、MultiVar×1）。
  - P2 新增 6 项全部红（UpperBound、UndoneWinner、Dedup×2、copyOut satisfying、Union tag）。
  - `QueryExecutorWithTest.WithLimit/Skip...` 2 项红（实际返回 Bob/Carol，期望 0 行/Bob）。
- 修复后：
  - `optimizer_tests` 102/102 通过。
  - `query_executor_tests` 496/496 通过（含 `TckWith7Scenario1BoundEndpoint` 回归）。
  - TCK 定向回归通过：`with-skip-limit`、`with-where`、`with-orderBy` 合计 320/320。
