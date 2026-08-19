# CrossProduct 相关变量等值约束设计

> 本设计用于解决当前 TCK 剩余的一类根因问题：
> `With1[3]`、`With6[2]`、`Match3[18][20][21]`、`Match7[3][9][11][17]`
> 以及部分 OPTIONAL MATCH 场景。

## 〇、Review 修订记录

- v1 初版把 `BoundJoinEquality` 设计为“左右 `BoundColumnRef` 成员”，review 发现这会绕过 DPL 的 slot 分配 / 表达式重写，导致列偏移再次失效。修订为**保存完整 `BoundExpression` 谓词**。
- v1 未明确多 pattern part 的右侧作用域隔离方式；v2 补充。
- v1 未明确“bindMatch 中 parent 存在但起点未绑定”这一路径的输出列规范；v2 补充并给出回归风险。
- v1 未列出 DPL 各 pass 的接入点；v2 补齐。

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

1. 每个 CrossProduct / LeftJoin 的左右子计划在**独立作用域**中绑定；
2. 两侧同名变量通过**显式等值谓词**连接；
3. 实体等值谓词通过 `id()` 比较 ID；
4. 等值谓词是普通 `BoundExpression`，完整接入现有 SlotId / ProjectionExtract / rewriteColumnIndices 流程；
5. 下游 `RETURN/WITH` 只看到**唯一**的规范列（同名变量取左/外层列）。

---

## 三、设计

### 3.1 新增逻辑结构：`BoundJoinEquality`

`bound_binary_join_op.hpp` 中新增：

```cpp
#include "query/planner/bound_expression/bound_expression.hpp" // 完整类型

struct BoundJoinEquality {
    /// 同名变量，仅用于诊断和 canonical 列选择。
    std::string var_name;
    /// 已绑定的完整等值谓词。Binder 生成：
    ///   - 图实体：id(left_ref) = id(right_ref)
    ///   - 标量：  left_ref = right_ref
    /// left_ref.column_index 使用左列号；
    /// right_ref.column_index 在 Binder 中已加上左列数（右局部列 + left_cols）。
    BoundExpression predicate;
};
```

`BoundBinaryJoinOp` 增加：

```cpp
std::vector<BoundJoinEquality> equalities;
```

`BoundLeftJoinOp` 暂不改变 correlation 语义。

**为什么不是左右 `BoundColumnRef` 成员**：如果只存 refs，物理 planner 需要重新构造谓词，会绕开 `column_rewrite` 的 slot 分配与表达式重写；一旦 PE 在 Join 之上追加列，refs 的物理列号又会错位。

### 3.2 Binder：独立作用域绑定右子计划

`bindSingleQuery` 的 `needs_cross` 分支：

1. `auto saved = ctx_.save();`
2. `ctx_.beginSubScope();`（清空符号，`next_column_index = 0`）
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
   - 类型相同才加入 `equalities`；
   - `left_ref = BoundColumnRef(saved_col.column_index, saved_col.type, name, saved_col.slot_id)`；
   - `right_ref = BoundColumnRef(right_col.column_index + saved.next_column_index, right_col.type, name, right_col.slot_id)`；
   - 若类型为图实体/拓扑形态，`predicate = makeEntityEquality(left_ref, right_ref)`；
   - 否则 `predicate = makeScalarEquality(left_ref, right_ref)`；
10. 如果 MATCH 有 WHERE，WHERE 在等值谓词**之上**绑定。

### 3.3 Binder：多 pattern part 之间

`bindMatch` 中，对每个 pattern part（`pi > 0`）与 `previous` 构造 CrossProduct 前：

1. 保存 `part_saved = ctx_.save()`（包含上一 part 的符号表）；
2. `ctx_.beginSubScope()` 后绑定当前 pattern part；
3. 记录 `part_right_symbols`；
4. `ctx_.restore(part_saved)`；
5. 右侧新变量 `column_index += part_saved.next_column_index` 后合并；
6. 对左右同名变量按 3.2 生成 equalities，挂到本次 CrossProduct；
7. 最后 `previous` 与当前 part 构造 CrossProduct。

注意：`pi == 0` 的 parent 路径不重复加约束；parent 已经在 pipeline 中。

### 3.4 Binder：OPTIONAL MATCH 非起点绑定变量

`bindOptionalMatch` 中，当 `bound_vars` 非空但 pattern 起点未绑定时：

1. `ctx_.beginSubScope()`；
2. 为每个 `bound_vars` 创建 `BoundCorrelatedSourceOp` 列（列号从 0 开始）；
3. 以该 source 作为 parent 调 `bindMatch(match, parent, ...)`；
4. `bindMatch` 必须支持“parent 存在但起点未绑定”：
   - 起点按普通 Scan 绑定；
   - 在 `pi == 0` 时构造 `CrossProduct(parent, start_scan)`；
   - 后续 hop 通过 Expand/VarLenExpand 的 bound filter 生效；
   - 绑定变量在合并后输出中仍以 parent 列为规范列，不能新追加同名列；
5. `left_join.correlation` 继续使用 SlotId 解析（已实现）。

**回归风险提示**：这一步曾出现过“行数正确但 RETURN 输出成顶点”的问题，根因是 CrossProduct 后规范列选择与 DPL 重写不一致。实现时必须保证：
- 绑定变量在 CrossProduct 合并后仍使用 parent 的 SlotId；
- 下游 RETURN 表达式解析到该 SlotId；
- 新增列不覆盖同名 parent 列。

### 3.5 物理计划：等值谓词落地

`planBoundOperator(BoundBinaryJoinOp)`：

1. 先规划左右子计划（现有逻辑）；
2. 若 `v.equalities` 非空：
   - 把所有 `predicate` AND 成一个 `BoundBinaryOp`（或逐个 Filter）；
   - 在 CrossProduct 之上插入 `FilterPhysicalOp`；
   - Filter 的输入 layout 是合并后的 layout；
3. `compileOperatorTree` 会通过 ExpressionCompiler 将谓词中的 slot 解析为物理列；
4. 输出 schema 使用现有 CrossProduct schema（左列 + 右列），SlotLayout 合并。

**不要在物理 planner 重新构造谓词**；谓词在 Binder 中已完成类型解析和 batch_fn 解析，planner 只负责把它放进 Filter。

### 3.6 实体 ID 比较辅助函数

Binder 中新增：

```cpp
BoundExpression makeEntityEquality(const BoundColumnRef& left,
                                  const BoundColumnRef& right);
```

- 内部使用 `FunctionRegistry::lookup("id", {ref.type})` 构造 `BoundFunctionCall`；
- `id()` 已注册：VERTEX / VERTEX_REF / EDGE / EDGE_KEY；
- 若 `lookup` 返回 nullptr，回退为 `left = right` 并记录 warning（防止崩溃）；
- batch_fn 使用 `resolveBinaryBatchFn(EQ, INT64, INT64)`。

---

## 四、DPL / Optimizer 接入点

新增 equalities 后，以下 pass 必须同步更新：

| Pass | 位置 | 行为 |
|------|------|------|
| `allocateSlotsInOp` | `column_rewrite.cpp` | 对每个 equality 的 `predicate` 调 `ensureSlotsInExpr`，再递归 children |
| `collectOpReqs` | `column_rewrite.cpp` / `requirement_collector.cpp` | 对每个 predicate 收集需求（当前是 id 函数/列引用，通常无属性需求，但不能漏） |
| `rewriteOp` | `column_rewrite.cpp` | 对每个 predicate 调 `rewriteExpr` |
| `memo.cpp` | BoundBinaryJoinOp 克隆 | `cloneBoundExpression` 深拷贝 predicate |
| `operator_eq.cpp` / `operator_hash.cpp` | Join 相等性/哈希 | 用 `equalBoundExpression` / `hashBoundExpression` 处理 predicate |
| `remapLogicalOpColumnIndices` | `physical_planner.cpp` | **不 remap equalities**（列号已在 Binder 中按合并布局设置） |

---

## 五、列规范与作用域规则

- 同名变量在 CrossProduct 后**以左列（外层列）为规范列**。
- `ctx_.symbols` 中同名变量不覆盖；右列只用于等值谓词，不作为下游解析目标。
- `BoundColumnRef` 仍携带 `slot_id`，由 ExpressionCompiler 在物理算子 init 时解析为最终物理列。

---

## 六、测试计划

1. 单测：
   - `MATCH ()-[r1]->(:X) WITH r1 AS r2 MATCH ()-[r2]->() RETURN r2 AS rel` → 2 行 `[:T1]`, `[:T2]`；
   - `MATCH ()-[r1]->(:X) WITH r1 AS r2, count(*) AS c MATCH ()-[r2]->() RETURN r2` → 行为与 With6[2] 一致；
   - `MATCH (a {name:'A'}), (b {name:'B'}), (c {name:'C'}) MATCH (a)-->(x), (b)-->(x), (c)-->(x) RETURN x` → 2 行；
   - `OPTIONAL MATCH (x)-->(b)`（b 已绑定）→ 正确返回匹配行；
   - `MATCH ()-[r2]->() WITH r2` 等标量同名列场景。
2. TCK：
   - `clauses/with`（当前 2 失败 → 预期 0）
   - `clauses/match`（当前 13 失败 → 预期显著下降）
   - `clauses/match-where`、`clauses/delete`（回归）
   - `expressions/list`（List12 回归）

---

## 七、不做的事

- 不重写 ColumnResolver；
- 不改变 ProjectionExtract 的追加列模型；
- 不引入新的运行时 Join 算子；
- 不在物理算子里用变量名字符串做热路径比较。

---

## 八、相关文件

| 文件 | 变更 |
|------|------|
| `src/query/planner/binder/binder.cpp` | needs_cross 独立作用域 + equalities 生成 |
| `src/query/planner/binder/bind_match.cpp` | 多 pattern part 等值约束 + OPTIONAL 非起点绑定 + parent 未绑定起点路径 |
| `src/query/planner/binder/bind_return.cpp` | `makeEntityEquality` / `makeScalarEquality` helper（或独立 helper 文件） |
| `src/query/planner/logical_plan/operator/bound_binary_join_op.hpp` | 新增 `BoundJoinEquality` 与 equalities 字段 |
| `src/query/planner/logical_plan/operator/bound_left_join_op.hpp` | 不变 |
| `src/query/physical_plan/physical_planner.cpp` | equalities 落地为 Filter |
| `src/query/optimizer/column_rewrite.cpp` | DPL 三个 pass 接入 equalities |
| `src/query/optimizer/requirement_collector.cpp` | equalities 需求收集 |
| `src/query/optimizer/memo.cpp` / `operator_eq.cpp` / `operator_hash.cpp` | equalities 深拷贝 / 相等性 / 哈希 |
