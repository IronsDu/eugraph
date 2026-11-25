# Optimizer / Access Path 后续规划

> 状态：2026-11-25。当前 PR #331 是 access-path lowering 的过渡阶段，尚未达到
> “physical planner 只 lowering”的目标。本文把新增的 optimizer 债务与既有执行层
> 性能待办放在同一优先级队列里。

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

### P0 — #331：IndexJoin descriptor + 删除 tryPlan*

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

### P1.1 — Slot ownership 收尾
### P1.1 — Slot ownership 收尾

- optimizer 规则生成 Project/alias 时同步创建/继承 SlotId；
- 删除 physical planner 的 `allocateAllSlots` backfill；
- 明确 user slot / internal column 的分配者；
- 增加“optimizer 后不存在未登记用户 slot”的校验。

### P2 — PE / LateMaterialize enforcer 化

- PE 边界由 ChosenPlan/PhysProp 描述；
- physical planner 只实例化 enforcer；
- LateMaterialize 同样从计划描述驱动；
- 解决当前 physical planner 自行决定 PE 边界的问题。

### P3 — degree / selectivity 统计（先设计评审）

- `(edge_label, src_label, dst_label)` 边数；
- in/out degree；
- index selectivity；
- StatsDelta / snapshot / journal / Catalog 全链路；
- 先定粒度、写时维护 vs ANALYZE、兼容和回退，再实现。

### P4 — c3：非 identity Project / 无显式 label

依赖 P3 的 endpoint label 信息：

- 目标变量无显式 label 时安全解析父 label 索引；
- Filter 穿过 load/PE 边界；
- 保留残差谓词；
- 不允许靠数据分布猜 label。

### P5 — EXPLAIN 全面使用 SlotSemantics

- `IndexScan properties=[<prop#N>]` → 属性名；
- `Filter(message < ...)` → `message.creationDate < ...`；
- Projection load/materialize 显示完整属性；
- Bolt plan / Web Graph 同步受益。

### P6 — 既有执行层性能待办

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
