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

## 剩余两项待办的深度说明

### P2-3：O_GROUP 非 any 属性路径 + Sort Enforcer

**作用**：Cascades 的优化上下文除了代价上界，还携带“要求哪种物理属性”（`PhysProp`）。Columbia 的
`O_GROUP` 在遇到 sorted 等非 any 需求时，会同时做两件事：用当前属性优化一遍；用 any 优化出最便宜的
未排序计划，再在它上面插入 Sort Enforcer 升级为有序计划，最后按代价比较“原生有序”与“any+Sort”。

**当前状态**：`PhysProp` 已定义 sort order，但根 context 固定 any，`O_INPUTS` 不传播排序需求，
`O_GROUP` 没有 enforcer + any-context 分支，也没有规则能产出 sorted winner。当前 `ORDER BY` 查询
正确，是因为逻辑计划里本身有 `BoundSortOp`，直接被实现成 `SortPhysicalOp`，并不经过这套属性机制。

**影响评估**：
- 当前：无正确性影响，该路径实际是死代码。
- 未来触发点：
  1. 索引序扫描替代 Sort（`ORDER BY age LIMIT n` 借助 age 索引避免全排序）；
  2. Sort-merge join 需要知道子计划是否有序；
  3. 避免上层 ORDER BY 与下层已有有序计划的重复排序；
  4. 手工构造 sorted context 的测试/扩展会选不出 plan 并回退 RBO。

### P2-7：BINDERY 多 binding + Rule Subsumption Mask

**BINDERY 的作用**：Columbia 的 BINDERY 会把规则 pattern 的每个非叶子输入绑定到 group 内所有等价
表达式，枚举出全部 binding，再对每个 binding 分别执行 condition/substitute。例如结合律 pattern
`(L1 join L2) join L3` 应匹配组内所有左深 join。

**当前状态**：`OptRule::fullMatch` 存在但无调用；`ApplyRuleTask` 只做根类型 `topMatch` 后对单个
GroupExpr 执行规则；FilterPushdown 自己只看 `logical_exprs.back()`。当前规则集全部是“单算子 →
单算子”或单步下推，所以够用。

**影响评估**：
- 当前：无正确性影响，现有规则不需要多 binding。
- 未来触发点：加入 Join 交换律/结合律、多层 transformation、或一个 group 内有多个等价表达式时，
  只处理一个表达式会漏掉大量合法计划，搜索空间不完整，选不出最优计划。

**Subsumption mask 的作用**：让规则声明“应用完我之后，新表达式上哪些规则不必再触发”，防止
交换律/结合律等规则互相反复生成对方，导致不收敛或搜索爆炸。

**当前状态**：`GroupExpr::rule_mask` 只表达“某规则在该表达式上是否已触发”，新表达式 mask 始终为 0，
没有规则间 subsumption 声明。

**影响评估**：
- 当前：FilterPushdown 单调下推，实现规则单次触发，无循环风险。
- 未来触发点：加入可互相重写的规则群后，优化循环可能长期不收敛，最终撞上
  `kMaxIterations = 100000` 提前终止；或者 Memo 膨胀、优化时间暴涨。结果通常仍正确，但可能
  表现为复杂 join 查询突然变慢或优化超时。

## 第三轮核对：其余 Columbia 差异（新增观察）

以下差异不影响当前规则集的正确性，先记录供后续规则扩展时一并处理：

- [ ] **R-1 O_EXPR 无条件探索全部 child group**
  `OExprTask::perform` 对所有 child 都推 `EGroupTask`；Columbia 只为规则 original pattern 中非叶子
  输入推 E_GROUP。曾尝试按 pattern 收紧，但导致 `WithWhere2` 复杂 WITH WHERE 查询 TCK 回归
  （0 行 vs 预期 2 行），说明当前规则/搜索空间构造仍依赖 child exploration 先完成，暂保持无条件探索。
- [x] **R-2 E_GROUP 只探索 first logical expr**（改为遍历全部 logical expr）
  Columbia 也这么做，但依赖 transformation rule 的合流性；eugraph 靠 O_GROUP 遍历全部 logical expr
  兜底。引入非合流多规则后，explore 阶段可能在 condition 之前不完整。
- [x] **R-3 FilterPushdown 只看 child group 的最后一个 logical expr**（substitute 枚举全部可穿透 child）
  组内有多个等价表达式时可能漏掉可下推形态或选到非代表性形态。当前组通常只有 1 个表达式，影响小。
- [x] **R-4 文档与实现不一致：Filter 自身可穿透**（isPenetrable 增加 Filter）
  `cascades-optimizer.md` 可穿透表标记 Filter ✅，但 `isPenetrable` 没有 `OptNodeType::Filter`。
  相邻 Filter 合并/下推当前不会发生（性能优化缺口，不是语义 bug）。
- [ ] **R-5 clone 与 hash/eq 的 slot 字段不一致**
  hash/eq 已纳入 `BoundExpandOp`/`BoundVarLenExpandOp` 的 binder slot 字段，但
  `cloneBoundLogicalOperator` 仍未拷贝这些字段。规则 substitute 产生的克隆会以 INVALID slot 参与
  dedup，可能漏合并，增加重复表达式。
- [x] **R-6 extractChosen 未按 child schema 过滤 materialization 需求**
  `OInputsTask::inputRequiredProp` 已过滤，但 `extractChosen` 仍把父需求全集塞给每个 child，随后靠
  any-fallback 解包。物化需求复杂时 chosen 树可能与 winner 链不一致。
- [ ] **R-7 copyOut(prop) 的子节点仍统一按 any 递归**
  本轮只修了根 winner 选择；子节点属性传播尚未镜像 `extractChosen`。
- [ ] **R-8 O_INPUTS 仍把每个新 winner 直接 done=true**
  Columbia 只在最后一个 O_INPUTS 收尾时才 SetDone。当前靠 LIFO 原子性 + searchCircle 未完成防护规避，
  但多 context/重入场景下语义仍与 Columbia 不完全一致。
- [ ] **R-9 输入 group 的 context 上界仍为 infinity**
  本轮只收紧当前 context；Columbia 会为每个输入计算 `InputBd = LocalUB - CostSoFar + InputCost`。
  跨输入剪枝能力仍弱。
- [ ] **R-10 混合类型多变量 Enricher 使用近似 tag**
  多变量 Enricher 合并为一个 PhysicalExpr 时，混合 vertex/edge/path 只能选一个 tag 参与 cost model。
  当前仅影响代价精度。
- [ ] **R-11 InterestingProps / Context::done / const-group 短路未实现或未接线**
  `InterestingProps` 无调用；`Context::done` 无使用；O_GROUP 的 const group 快速路径只有注释没有代码。
- [x] **R-13 dedup 短路导致的 ExprId 空洞**（生产路径改为 INVALID id + insert 时分配；避免 duplicate 后 id 间隙越界）
- [ ] **R-12 Group::getLogProp 仅支持 logical_exprs**
  physical-only group 会得到空 LogProp。当前 Enricher 插入原 group 规避了该限制；未来若做真正的
  enforcer chain（每个 enforcer 一个 group）需要先补物理 group 的逻辑属性推导。

## 已确认的验证基线

- 修复前：
  - `optimizer_tests` 84 例全过，但新增的 P0/P1 用例 9 项全部红（Filter×2、Merge×2、TaskState×1、Hash×5、MultiVar×1）。
  - P2 新增 6 项全部红（UpperBound、UndoneWinner、Dedup×2、copyOut satisfying、Union tag）。
  - `QueryExecutorWithTest.WithLimit/Skip...` 2 项红（实际返回 Bob/Carol，期望 0 行/Bob）。
- 修复后：
  - `optimizer_tests` 105/105 通过。
  - `query_executor_tests` 496/496 通过（含 `TckWith7Scenario1BoundEndpoint` 回归）。
  - TCK 定向回归通过：`with-skip-limit`、`with-where`、`with-orderBy` 合计 320/320。
- 全量验证（本分支最终）：
  - CTest 单元/集成（排除 tck_tests 与外部驱动集成）：1028 个测试，100% 通过，4 个 LoaderIntegration 按预期 Skip。
  - 全量 TCK：3897 scenarios，3845 passed，52 undefined（均为 CALL/procedure 未实现场景），0 failed；执行耗时 13m05s。
