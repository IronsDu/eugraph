# 模块职责与代码归属

> 状态：2026-11-26。新增/修改代码前先确认归属；边界不清时先问开发者，不要自行跨层放代码。

## 1. 分层职责

| 模块 | 应该做 | 不应该做 |
|---|---|---|
| `query/parser` | AST、语法/关键字、chained comparison 等语法展开 | 绑定、索引、执行 |
| `query/planner/binder` | 名称/类型解析、SlotId 身份、语义绑定、WHERE 结构规范化与 Filter 拆分策略 | 选择索引、比较 access path、实例化物理算子 |
| `query/optimizer/analysis` | 与优化相关的分析：IndexCondition/range 合并、selectivity、requirements、slot 语义、LogProp | 创建 PhysicalOperator、分配执行 buffer |
| `query/optimizer/rules` | 逻辑/物理候选规则，输出 `ChosenPlan` / descriptor | 手工 lowering、执行期决策、分配用户 slot |
| `query/physical_plan` | `ChosenPlan` → PhysicalOperator：schema/layout、column_index、表达式编译、PE/enforcer 实例化、internal PE slot | 选择 forward/reverse、决定 join tag、重新推断 property 语义、重新规划 access path |
| `storage/data` | KV/index 编码、range cursor 的 start/end 与 inclusive 语义、IO/事务 | Cypher 语义、Filter 拆分、优化决策 |
| `explain` | 只读展示：SlotSemantics + PEPlan + Catalog → 可读文本 | 改变计划/执行、隐藏实际算子/Filter |
| `tests` | 分层单元 + QueryExecutor 集成 + LDBC 对照 | 用测试掩盖语义 |
| `docs/benchmark` | 口径、参数、before/after 证据 | 把不同参数/空结果当性能结论 |

## 2. Physical planner 明确职责

### 必须做

- 消费 `ChosenPlan` / descriptor；
- `PhysicalOpTag` → 具体 PhysicalOperator；
- `Schema`、`BoundType`、`TupleSlotLayout`、`column_index`；
- 编译 `BoundExpression`（slot → column）；
- 构建 PE/enforcer 并分配 internal PE slot（由 optimizer 描述边界）；
- 实例化 internal-only 的执行细节（batch buffer、scan cursor 参数）。

### 绝对不能做

- 选择 label scan / index scan / access path；
- 选择 forward/reverse、IndexJoin、HashJoin tag；
- 重新推断 `BoundColumnRef` 对应的 label/property；
- 查 catalog 选 index 或重新解析 range 谓词；
- 分配/重写用户 SlotId；
- 决定 PE / LateMaterialize 边界；
- 在 CBO 之后改变计划形状来“优化”查询。

判断句：

> 如果这个决定会改变计划或成本，它属于 optimizer；如果只是把已决定的计划
> 变成可执行对象，它属于 physical lowering。

## 3. 判断代码归属的问题

1. 它是在**解析语法**、**确定语义身份**、**选择计划**、**实例化执行**、**存储编码**，还是**展示**？
2. 这个决策会改变计划/代价吗？会 → optimizer；只是机械实例化 → physical lowering。
3. 这个值需要跨 module 共享吗？需要 → 放到 `optimizer/analysis` 的只读结构，而不是复制到 physical planner。
4. 如果现有模块边界本身是错的，先停下来问开发者是否调整模块，不要为了方便把代码塞进最近的文件。

## 4. 近期案例

### c3 范围索引

```text
parser: chained comparison → AND              正确
binder: 保留同一 property 的范围 conjunct      正确
optimizer/analysis: normalize + merge bounds  正确
storage: start/end inclusive encoding         正确
physical planner: 实例化 IndexScan            只能消费上面结果
```

反例：在 physical planner 里对 GTE/LTE 做 `value ± 1`，或在 physical planner
里重新实现一套 range merge，属于临时补丁。

### Dynamic IndexJoin

```text
optimizer/rule: 产生 IndexJoin descriptor
physical: DynamicIndexJoinPhysicalOp 实例化
```

反例：`ReexecutePhysicalOp` 让 physical 层隐式决定“source 可以再执行一次”，
既不是 optimizer 候选，也不是显式执行契约。

### Correlated 端点（EXISTS / pattern predicate / comprehension）

```text
binder: 外层已绑定的链端点直接绑成 Expand 的目的列（dst=bound），不造后置等式 Filter
optimizer: AnnotateCorrelatedEndpointProbe 只标注 probe 策略（不改形状、不比成本）
physical: 只实例化 bound-dst Expand / 已标注的 probe
```

反例：先造 `Filter(id(dst)=id(saved))` 再让某层折叠（形状与策略耦合），
或让 physical 按“是否相关”决定 probe。策略的成本化（Impl 候选）等 P3 统计。

### 策略属性（HashJoin build 侧 / shortestPath batch）

```text
optimizer/rules/physical: Impl 规则产候选并携带策略属性（build_left / batch_shortest_path）
physical: lowering 只转发 ChosenPlan 属性，不按形状推断策略
binder/normalization: 只产语义形状与不变量，不写策略字段
```

反例：把 batch 放回 logical operator 由 normalization 设置（策略进入 hash/eq、与形状耦合），
或让算子按形状猜。与形状绑定但尚未成本化的策略（correlated 端点 probe）先以只读标注存在，
成本化随统计一起做。

## 5. 评审清单

- 新 helper 是否在最低层、且没有反向依赖？
- 是否复制了另一层已有的语义（range、slot、property、cost）？
- 新增 descriptor 字段是否同时更新 clone/visitor/explain？
- 是否引入 physical planner 的“策略选择”？如果是，移到 optimizer。
- 是否在 binder 里做索引选择？如果是，移出。
- 是否在 storage 里做 Cypher 语义？如果是，移出。

## 6. 相关文档

- [Optimizer 架构与 Columbia 迁移](optimizer-architecture.md)
- [Optimizer / Access Path 后续规划](optimizer-next-steps.md)
- [Physical planner 遗留决策清单](physical-planner-residue-audit.md)
- [EXPLAIN 语义元信息](explain-semantic-metadata.md)
- [Dynamic IndexJoin 单次 Source 消费](dynamic-index-join-shared-source.md)
