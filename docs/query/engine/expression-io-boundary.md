# 表达式求值不应发起 IO：把属性读取收敛到算子

Status: 设计（未实施）。本文记录问题、证据、目标不变量与分阶段落地方案。

## 问题

当前"取属性"这件事可以发生在**表达式求值内部**：

- `PropRef` / `DynamicPropertyRef` 的求值会访问存储（`ExpressionEvaluator`
  持有 `EvalContext.store`）；
- 为了兜住这种按行读取，求值器里还有一整套**投机预取**：`prefetch()` 的 dry
  pass、`prefetched_vertex_data_` 缓存、`needsPrefetchRec`、`prefetchHits /
  misses / missingPrefetchCount`；
- 于是"哪里发生了 IO"既不在计划里、也不在算子边界上，而是藏在表达式里 +
  预取启发式里。

这带来三个具体后果：

1. **计划不可解释**：`EXPLAIN`/`PROFILE` 看不到属性读取；要回答"这 18 万次读是
   谁发的"只能靠插桩（本轮就是这么做出来的）。
2. **性能不可控**：预取是投机性的，可能读多（见下），也可能读少（miss 时静默
   返回 NULL）。
3. **失败静默**：预取缺失时求值器把属性当 NULL，而不是报错或回退——结果错得
   很安静。

## 本轮的三条实证（都是隐藏 IO 的直接后果）

| 现象 | 根因 | 修法 | 收益 |
|---|---|---|---|
| `startNode(r).id` 每行一次存储读（~307k 次） | 合成顶点无属性 → 动态属性访问发起 IO | 提升成列（每个 `r` 算一次） | −18% |
| 分支 `b.id` 读 152k 次 | `LoadVertexPropCoalesce` 按行读、不去重 | 按 chunk 去重 + 查询级缓存 | −6% |
| 分支 Filter 读 ~180k 次、822 ms | **预取扫描 chunk 的全部图列**，把每行的匿名 Comment/Post 也读了 | 只扫表达式实际引用的列 | −40%（1.67 s → 1.0 s） |

第三条尤其说明问题：谓词只需要一个端点比较，却因为"表达式可能读属性"而把整行
的顶点都取了出来。修完之后两个分支 Filter 的 `wtSearchNear` 归零。

## 目标不变量

> **表达式求值只读 chunk 里的列，绝不访问存储。**
> 任何属性读取都必须由一个显式算子（`ProjectionExtract` / 未来的
> `LoadProperty`、`LoadDynamicProperty`）物化成列，IO 归属该算子。

推论：

- 表达式成为纯函数（无副作用、可重复求值、可重排/CSE/跨算子移动/向量化）；
- `EXPLAIN` 里每一笔 IO 都能追到算子；
- 预取机制（dry pass、缓存、miss 计数）整体消失。

## 边界与取舍

1. **动态属性**（`n.unknownProp`、`startNode(r).id`）无法在计划期拍平成扁平列。
   它们需要一个"动态取属性"算子，输出一列 `ANY`；IO 仍算在算子上。关键是**不要
   把它藏回求值器**。
2. **实体列的属性语义要写死**：要么"`VERTEX`/`EDGE` 列一定带全量属性"（当前
   ctor PE 基本如此，约 2 次读/顶点），要么"实体列只带 id，属性必须显式 Load"。
   前者对现有代码更友好，后者更省读。
3. **批量语义不能丢**：显式算子必须保留按 chunk 批量 + 按 `VertexId` 去重（本轮
   `b.id` 的 A/B 证明批量+去重优于逐行读，也优于投机预取）。
4. **迁移面很大**：所有带表达式的算子（Filter/Project/Aggregate/Sort/Set/Merge…）
   目前都依赖这条隐式兜底。需求侧已经存在（`PlanRequirements` / PE 计划），缺的
   是**覆盖完整性**——本轮卡住的正是"probe 产出的变量没有 PE 计划"这一类缺口。

## 分阶段落地

1. **让它变响**：求值器不再持有 `store`（或只记账不读）；本该 IO 的位置记
   `unplanned_io` 计数、返回 NULL，debug 构建直接 assert。
2. **加校验**：lowering 之后遍历物理树，若某算子表达式里的 `PropRef` /
   `DynamicPropertyRef` 没有被输入列覆盖 → 报错并打印算子与变量名。用它把缺口
   一条条找出来。
3. **补计划**：让所有属性需求都由 PE 显式物化，含动态属性算子。
4. **删机制**：`prefetch()` 全家、`EvalContext.store/meta`、prefetch 计数器；
   验收 = `PROFILE` 里 Filter/Project 的 `wtSearchNear == 0`。
5. **加不变量测试**：任意查询跑完断言"非 IO 算子的存储读 == 0"，防止回潮。

## 验收判据

- 任意查询的 PROFILE 中，表达式算子的 `wtSearchNear` 为 0；
- 与现状等价的查询结果（既有测试 + TCK 全绿）；
- 不得以"预取缓存"替代显式物化：缓存只能出现在 IO 算子内部，用于合并同一批
  顶点读取。
