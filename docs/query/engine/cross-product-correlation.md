# CrossProduct 相关变量等值约束设计

> 本设计用于解决当前 TCK 剩余的一类根因问题：
> `With1[3]`、`With6[2]`、`Match3[18][20][21]`、`Match7[3][9][11][17]`
> 以及部分 OPTIONAL MATCH 场景。

## 〇、Review 修订记录

- v1：把等值约束作为左右 `BoundColumnRef` 存进 Join；问题：绕开 DPL。
- v2：改为在 Join 上存 `BoundExpression` 谓词；问题：Join 职责膨胀、planner 需要新概念、三个 Binder 调用点重复、优化器/DPL 需要多处特判。
- v3（当前）：**移除 Join 上的等值约束字段**。Binder 直接把等值谓词降级为显式 `BoundFilterOp` 放在 CrossProduct 之上；所有下游模块只看到统一的 Filter 语义。

---

## 一、根因分析

### 1.1 Binder 使用单一可变 BindContext，右子计划复用外层列

当 `MATCH` 跟在 `WITH` 之后、且首个 pattern 起点不在 `WITH` 输出中时（`needs_cross = true`），当前实现：

1. 在**同一个** `BindContext` 中，以 `std::nullopt` 作为 parent 绑定右侧 `MATCH`；
2. 右侧 pattern 中出现的 `r2`、`b` 等外层变量会被 `bindNodePattern` / `bindRelationshipPattern` 当成“已绑定”，复用外层 `column_index`；
3. 但右子计划是独立算子（Scan/Expand），它的输入 chunk 中**并没有**外层列。

结果：右子计划运行时读到的是自己局部列或错位列；而左右两侧同名变量在 CrossProduct 后也没有等值约束。

### 1.2 CrossProduct 缺少同名变量等值约束

`bindSingleQuery` 在 `needs_cross` 时直接构造：

```
CrossProduct(left=WITH 输出, right=独立 MATCH)
```

两侧可以同时包含同名变量（例如 `r2`），但没有任何 Filter 保证它们相等。这导致：

- `MATCH ... WITH r1 AS r2 MATCH ()-[r2]->()`：右子计划实际扫描了**全部**边，左 `r2` 被忽略；
- `MATCH (a)-->(x), (b)-->(x), (c)-->(x)`：每个 pattern part 独立扫描，重复变量没有交点约束。

### 1.3 实体等值比较不可靠

`cypherCompareValues` 对图实体的跨形态比较（`EdgeValue` vs `EdgeKey`、`VertexValue` vs `VertexRef`）没有真正的 ID 比较逻辑。直接构造 `left = right` 的 `BoundBinaryOp` 会：
- 对相同 `type category` 的不同 Value variant 返回 0（视作相等），导致过滤失效；
- 或对某些形态返回错误结果。

正确做法是：跨列实体等值一律包装为 `id(left) = id(right)`。

### 1.4 OPTIONAL MATCH 非起点绑定变量

`bindOptionalMatch` 只处理了两种关联情况：
- pattern 起点本身已绑定（用 `CorrelatedSource` 关联起点）；
- pattern 完全无绑定变量（普通 LeftJoin，右侧独立扫描）。

对于 `OPTIONAL MATCH (x)-->(b)`（起点 `x` 新，但 `b` 已绑定），当前会落入“完全无绑定变量”分支，`b` 没有被注入右子计划，也没有等值约束。

---

## 二、设计目标

1. **职责单一**：逻辑算子树只包含标准算子（Scan/Expand/Join/Filter/Project...），不新增“带条件的 Join”这种复合算子；
2. **机制唯一**：跨作用域同名变量等值只由**一种 helper** 生成，任何调用点不得手写；
3. **实体等值正确**：跨列实体比较统一走 `id()`；
4. **作用域显式**：右侧独立绑定，快照/恢复/合并由 helper 封装；
5. **列规范显式**：同名变量以左/外层列为规范列，右列作为内部列不暴露。

---

## 三、模块职责

| 模块 | 职责 | 不负责 |
|------|------|--------|
| `Binder` | 解析作用域、生成逻辑算子树（含显式 Filter） | 不决定物理列布局 |
| `Optimizer` | 对标准逻辑算子做规则改写 | 不感知“等值约束”新概念 |
| `PhysicalPlanner` | 把标准逻辑算子映射成物理算子 | 不重新构造任何谓词 |
| `column_rewrite / DPL` | 遍历标准表达式树分配 slot、改写列引用 | 不新增 Join equalities 分支 |

---

## 四、设计

### 4.1 逻辑结构：等值谓词 = 显式 `BoundFilterOp`

**不修改 `BoundBinaryJoinOp` 的数据结构。**

Binder 生成如下逻辑树：

```
BoundFilterOp(predicate = AND(eq1, eq2, ...))
└── BoundBinaryJoinOp(Cross)
    ├── left  = WITH 输出
    └── right = 独立 MATCH
```

其中每个 `eq` 是普通 `BoundExpression`：

- 图实体：`id(BoundColumnRef(left)) = id(BoundColumnRef(right))`
- 标量：`BoundColumnRef(left) = BoundColumnRef(right)`

收益：

- Optimizer 按普通 Filter 处理（FilterPushdown 只会处理普通谓词）；
- DPL 按普通表达式处理，无需改 Join 遍历；
- PhysicalPlanner 直接复用 `BoundFilterOp` 分支，无新增概念；
- `operator_eq/hash/memo/remap` 全部无需为 Join 加 equalities 分支。

### 4.2 Binder helper：`bindCrossWithEqualities`

新增唯一入口（内部静态 helper）：

```cpp
struct CrossJoinBindResult {
    BoundLogicalOperator plan; // Filter(CrossProduct) 或 CrossProduct
};

std::optional<CrossJoinBindResult> bindCrossWithEqualities(
    BoundLogicalOperator left,
    BoundLogicalOperator right,
    const BindContext::Snapshot& left_scope,
    const BindContext::Snapshot& right_scope);
```

语义：

1. 收集 `left_scope.symbols` 与 `right_scope.symbols` 中的同名变量；
2. 对每个同名变量：
   - 类型兼容 → 生成 `eq`；
   - 类型不兼容 → 报 `VariableTypeConflict`（不静默跳过）；
3. 将多个 `eq` AND 成一个谓词（只有一个时直接用）；
4. 生成 `BoundFilterOp(predicate, child=BoundBinaryJoinOp(Cross, left, right))`；
5. 调用方继续在该 Filter 之上绑定 WHERE/RETURN。

### 4.3 `bindSingleQuery` 的 `needs_cross` 分支

1. `auto left = std::move(*current);`
2. `auto left_scope = ctx_.save();`
3. `ctx_.beginSubScope();`
4. `right = bindMatch(match, std::nullopt, /*skip_where=*/true);`
5. `auto right_scope = ctx_.save();`
6. `ctx_.restore(left_scope);`
7. 合并右新变量：
   - 仅当变量名不在 left 符号表中时：`column_index += left_scope.next_column_index`，写入 `ctx_.symbols`；
   - `ctx_.next_column_index = left_scope.next_column_index + right_scope.next_column_index`；
8. `result = bindCrossWithEqualities(left, right, left_scope, right_scope)`；
9. 若 MATCH 有 WHERE，在 `result.plan` 之上绑定 WHERE。

### 4.4 `bindMatch` 多 pattern part

对 `pi > 0` 的每个 pattern part：

1. `left_scope = ctx_.save()`（此时符号表 = 前序 pattern 的合并结果）；
2. `ctx_.beginSubScope()` 后绑定当前 part；
3. `right_scope = ctx_.save()`；
4. `ctx_.restore(left_scope)`；
5. 合并右新变量（同 4.3 的规则）；
6. `previous = bindCrossWithEqualities(previous, current_part, left_scope, right_scope).plan`。

`pi == 0` 的 parent 路径不调用该 helper，避免重复约束。

### 4.5 `bindOptionalMatch` 非起点绑定变量

当 `bound_vars` 非空、pattern 起点未绑定时：

1. `ctx_.beginSubScope()`；
2. 为每个 `bound_vars` 创建 `BoundCorrelatedSourceOp` 列；
3. 以该 source 作为 parent 调 `bindMatch`；
4. `bindMatch` 支持“parent 存在但起点未绑定”：
   - 起点按普通 Scan 绑定；
   - 仅 `pi == 0` 时构造 `CrossProduct(parent, start_scan)`；
   - 后续 hop 通过 Expand/VarLenExpand 的 bound filter 生效；
   - parent 中的绑定变量在 CrossProduct 后保持原 SlotId，不新增同名列；
5. `left_join.correlation` 继续使用现有 SlotId 解析。

**回归风险**：实现时重点验证 `RETURN` 对绑定变量的解析，确保它指向 parent 列而非右侧新列。

### 4.6 实体 ID 比较辅助函数

```cpp
BoundExpression makeEntityEquality(const BoundColumnRef& left,
                                  const BoundColumnRef& right);
```

- 使用 `FunctionRegistry::lookup("id", {ref.type})`；
- 若 lookup 返回 nullptr：回退 `left = right` 并打 warning；
- batch_fn = `resolveBinaryBatchFn(EQ, INT64, INT64)`。

---

## 五、优化器不变量

1. **跨 Join 谓词不得下推**：等值 Filter 的谓词同时引用左右两侧列，FilterPushdown 必须保持它位于 Join 之上。
   - 现有 pushdown 规则若只处理单侧引用，需要增加 guard；
   - 或者等值 Filter 使用 `BoundFilterOp` 并在 optimizer 规则中标记不可下推（若该规则已存在，直接复用）。
2. **右列是内部列**：右侧同名列仅服务于等值谓词，不进入 `ctx_.symbols` 的规范列；`RETURN *`/`WITH *` 只看到规范列。
3. **SlotId 稳定**：右侧变量在独立作用域中获得新 SlotId；合并后其右列物理位置由 ExpressionCompiler 解析，Binder 不写死物理列号。

---

## 六、测试计划

1. 单测：
   - `MATCH ()-[r1]->(:X) WITH r1 AS r2 MATCH ()-[r2]->() RETURN r2 AS rel` → 2 行 `[:T1]`, `[:T2]`；
   - `MATCH ()-[r1]->(:X) WITH r1 AS r2, count(*) AS c MATCH ()-[r2]->() RETURN r2` → With6[2] 语义；
   - `MATCH (a {name:'A'}), (b {name:'B'}), (c {name:'C'}) MATCH (a)-->(x), (b)-->(x), (c)-->(x) RETURN x` → 2 行；
   - `OPTIONAL MATCH (x)-->(b)`（b 已绑定）→ 正确返回匹配行；
   - 同名变量类型冲突 → `VariableTypeConflict`。
2. TCK：
   - `clauses/with`（当前 2 失败 → 预期 0）
   - `clauses/match`（当前 13 失败 → 预期显著下降）
   - `clauses/match-where`、`clauses/delete`、`expressions/list`（回归）

---

## 七、不做的事

- 不重写 ColumnResolver；
- 不改变 ProjectionExtract 的追加列模型；
- 不新增运行时 Join 算子；
- 不在物理算子里用变量名字符串做热路径比较；
- 不修改 `BoundBinaryJoinOp` 的数据结构。

---

## 八、相关文件

| 文件 | 变更 |
|------|------|
| `src/query/planner/binder/binder.cpp` | `needs_cross` 使用 `bindCrossWithEqualities` |
| `src/query/planner/binder/bind_match.cpp` | 多 pattern part / OPTIONAL 非起点绑定调用同一 helper |
| `src/query/planner/binder/bind_return.cpp` 或新 helper 文件 | `bindCrossWithEqualities` / `makeEntityEquality` |
| `src/query/physical_plan/physical_planner.cpp` | 无新增概念（Filter 走现有分支） |
| `src/query/optimizer/` | 仅必要时为跨 Join 谓词增加 pushdown guard |
| `src/query/planner/logical_plan/operator/bound_binary_join_op.hpp` | **不修改** |
