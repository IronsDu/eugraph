# CrossProduct 相关变量等值约束设计

> 本设计用于解决当前 TCK 剩余的一类根因问题：
> `With1[3]`、`With6[2]`、`Match3[18][20][21]`、`Match7[3][9][11][17]`
> 以及部分 OPTIONAL MATCH 场景。

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

1. 每个 CrossProduct / LeftJoin 的左右子计划在**独立作用域**中绑定；
2. 两侧同名变量通过**显式等值约束**连接；
3. 实体等值约束通过 `id()` 比较 ID；
4. 不破坏现有 `BoundColumnRef` / SlotId / ProjectionExtract 架构；
5. 下游 `RETURN/WITH` 只看到**唯一**的规范列（同名变量取左/外层列）。

---

## 三、设计

### 3.1 新增逻辑结构：`BoundJoinEquality`

在 `bound_binary_join_op.hpp` 中新增：

```cpp
struct BoundJoinEquality {
    std::string var_name;
    BoundColumnRef left_ref;   // 左输出中的列
    BoundColumnRef right_ref;  // 右输出中的列（局部列号，由 planner 加 offset）
    bool compare_by_id = false; // 图实体用 id()
};
```

`BoundBinaryJoinOp` 增加：

```cpp
std::vector<BoundJoinEquality> equalities;
```

`BoundLeftJoinOp` 暂不改变 correlation 语义，但 planner 可使用相同解析逻辑。

### 3.2 Binder：独立作用域绑定右子计划

`bindSingleQuery` 的 `needs_cross` 分支：

1. `auto saved = ctx_.save();`
2. `ctx_.beginSubScope();` （清空符号，`next_column_index = 0`）
3. `right = bindMatch(match, std::nullopt, /*skip_where=*/true)`；
4. 保存 `right_symbols = ctx_.symbols`、`right_cols = ctx_.next_column_index`；
5. `ctx_.restore(saved)`；
6. 对 `right_symbols` 中**只出现在右侧**的变量：
   - `column_index += saved.next_column_index`（左列数）；
   - 写入 `ctx_.symbols`；
   - 同名变量不覆盖左列，保留左列为规范列；
7. `ctx_.next_column_index = saved.next_column_index + right_cols`；
8. 构造 `BoundBinaryJoinOp(left, right)`；
9. 对每个同时出现在 `saved.symbols` 和 `right_symbols` 的变量：
   - 类型相同则加入 `equalities`；
   - `compare_by_id` = 变量类型是 VERTEX/EDGE/PATH/拓扑形态；
   - left_ref 使用 `saved.symbols[name]` 的 column_index/slot_id；
   - right_ref 使用 `right_symbols[name]` 的 column_index/slot_id（局部，planner 会加 offset）；
10. 如果 MATCH 有 WHERE，WHERE 在等值约束**之上**绑定（与现状一致）。

### 3.3 Binder：多 pattern part 之间

`bindMatch` 内部对 `pi > 0` 的 pattern part 与 `previous` 构造 CrossProduct 后，执行同样的“同名变量等值约束”逻辑：

- 左符号表 = 进入本 pattern part 前 `ctx_.symbols` 的快照；
- 右符号表 = 当前 pattern part 绑定后的新增/复用符号；
- 按 3.2 生成 `equalities`。

注意：`pi == 0` 的 parent 路径不重复加约束；parent 已经在 pipeline 中。

### 3.4 Binder：OPTIONAL MATCH 非起点绑定变量

`bindOptionalMatch` 中，当 `bound_vars` 非空但 pattern 起点未绑定时：

1. `ctx_.beginSubScope()`；
2. 为每个 `bound_vars` 创建 `BoundCorrelatedSourceOp` 列；
3. 以该 source 作为 parent 调 `bindMatch(match, parent, ...)`；
4. `bindMatch` 走 3.2 的新逻辑：起点未绑定则“扫描起点 × CorrelatedSource”，后续 hop 中的绑定变量通过 Expand/VarLenExpand 的 bound filter 生效；
5. `left_join.correlation` 继续使用 SlotId 解析（已实现）。

### 3.5 物理计划：等值约束落地

`planBoundOperator(BoundBinaryJoinOp)`：

1. 先规划左右子计划（现有逻辑）；
2. 对每个 `BoundJoinEquality`：
   - left 物理列 = `lr.slot_layout.getColumnIndex(left_ref.slot_id)`，缺失则回退 `left_ref.column_index`；
   - right 物理列 = `rr.slot_layout.getColumnIndex(right_ref.slot_id)`，缺失则回退 `right_ref.column_index + lr.output_schema.size()`；
3. 在 CrossProduct 之上插入 `FilterPhysicalOp`，谓词为：
   - `compare_by_id == true`：`BoundFunctionCall(id(left)) = BoundFunctionCall(id(right))`；
   - 否则：`left = right`；
4. 输出 schema 使用现有 CrossProduct schema（左列 + 右列），SlotLayout 合并。

### 3.6 实体 ID 比较辅助函数

在 Binder 中新增一个 helper：

```cpp
BoundExpression makeEntityEquality(const BoundColumnRef& left,
                                  const BoundColumnRef& right);
```

内部使用 `FunctionRegistry::lookup("id", {ref.type})` 构造 `BoundFunctionCall`，并用 `resolveBinaryBatchFn(EQ, INT64, INT64)` 作为 batch_fn。

---

## 四、列规范与作用域规则

- 同名变量在 CrossProduct 后**以左列（外层列）为规范列**。
- `ctx_.symbols` 中同名变量不覆盖；右列只用于等值约束，不作为下游解析目标。
- `BoundColumnRef` 仍携带 `slot_id`，由 ExpressionCompiler 在物理算子 init 时解析为最终物理列。

---

## 五、测试计划

1. 单测：
   - `MATCH ()-[r1]->(:X) WITH r1 AS r2 MATCH ()-[r2]->() RETURN r2 AS rel` → 2 行 `[:T1]`, `[:T2]`；
   - `MATCH ()-[r1]->(:X) WITH r1 AS r2, count(*) AS c MATCH ()-[r2]->() RETURN r2` → 行为与 With6[2] 一致；
   - `MATCH (a {name:'A'}), (b {name:'B'}), (c {name:'C'}) MATCH (a)-->(x), (b)-->(x), (c)-->(x) RETURN x` → 2 行；
   - `OPTIONAL MATCH (x)-->(b)`（b 已绑定）→ 正确返回匹配行。
2. TCK：
   - `clauses/with`（当前 2 失败 → 预期 0）
   - `clauses/match`（当前 13 失败 → 预期显著下降）
   - `clauses/match-where`、`clauses/delete`（回归）

---

## 六、不做的事

- 不重写 ColumnResolver；
- 不改变 ProjectionExtract 的追加列模型；
- 不引入新的运行时 Join 算子；
- 不在物理算子里用变量名字符串做热路径比较。

---

## 七、相关文件

| 文件 | 变更 |
|------|------|
| `src/query/planner/binder/binder.cpp` | needs_cross 独立作用域 + equalities |
| `src/query/planner/binder/bind_match.cpp` | 多 pattern part 等值约束 + OPTIONAL 非起点绑定 |
| `src/query/planner/logical_plan/operator/bound_binary_join_op.hpp` | 新增 equalities |
| `src/query/planner/logical_plan/operator/bound_left_join_op.hpp` | 不变 |
| `src/query/physical_plan/physical_planner.cpp` | 等值约束落地为 Filter |
| `src/query/physical_plan/operator/cross_product_physical_op.*` | 不变（Filter 在上层） |
| `src/query/optimizer/column_rewrite.cpp` | 遍历新增的 equalities 表达式（确保 slot 分配与 rewrite） |
| `src/query/optimizer/operator_eq.cpp` / `operator_hash.cpp` | equalities 参与相等性/哈希 |
| `src/query/optimizer/memo.cpp` | 深拷贝 equalities |
