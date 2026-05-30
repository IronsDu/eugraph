# Temporal 属性存取往返 — 重构设计

> 分支: `fix/temporal-property-roundtrip`
> 状态: **已实现**（416 测试全部通过，含新增 OrderByEdgeId 测试）

---

## 问题

时间值通过 `CREATE` 存入属性后，`MATCH` 读出再访问字段（如 `d.year`）返回 null。

### 根因

`CreateNodePhysicalOp` 和 `CreateEdgePhysicalOp` 中，属性值求值和类型推断**只处理 `BoundLiteral`**。当属性值是函数调用（如 `date(...)`、`datetime(...)`）时，值为 `BoundFunctionCall`，不在处理范围内，属性槽为空。

三处受影响代码路径：

| 位置 | 问题 |
|------|------|
| `physical_planner.cpp` CreateNode 段 | `label_properties` 在 plan time 将 BoundLiteral 转 PropertyValue，非 literal 表达式被丢弃 |
| `physical_planner.cpp` CreateEdge 段 | 同上 |
| `create_node_physical_op.cpp` | `pending_props_` 求值只处理 BoundLiteral（anon 和 non-anon 两条路径） |
| `create_edge_physical_op.cpp` | 同上 |
| `physical_planner.cpp` 类型推断 | `pending_props` 类型只从 BoundLiteral 取，BoundFunctionCall 的 `return_type` 被忽略 |
| `create_node_physical_op.cpp:70` | `getOrCreateAnonPropId(prop_name, PropertyType::ANY)` 匿名标签类型硬编码为 ANY |

---

## 设计方案

### 核心思路

参照 `SetPhysicalOp` 的正确实现模式——使用 `VectorizedEvaluator` 逐行求值表达式，将求值从 plan time 移到 run time。

### 数据结构变更

**CreateNodePhysicalOp:**

```cpp
// 旧: vector<pair<LabelId, Properties>>  // Properties = vector<optional<PropertyValue>>
// 新: vector<pair<LabelId, vector<pair<uint16_t, BoundExpression>>>>
```

`label_props_` → `label_prop_exprs_`：不再在 plan time 评估 BoundLiteral → PropertyValue，改为直接传递 BoundExpression 到算子，由算子在 run time 用 VectorizedEvaluator 求值。

**CreateEdgePhysicalOp:**

```cpp
// 旧: Properties props_  // vector<optional<PropertyValue>>
// 新: vector<pair<uint16_t, BoundExpression>> prop_exprs_
```

同样改为传递 BoundExpression。

### 执行模型变更

**旧模型（单行 drain）：**

```cpp
executeChunk() {
    if (child_) {
        // drain child, discard output
    }
    // create ONE vertex with pre-evaluated properties
    co_yield output;
}
```

**新模型（逐行求值）：**

```cpp
executeChunk() {
    if (child_) {
        // iterate child DataChunks
        for each row {
            evaluate property expressions using VectorizedEvaluator
            create vertex, allocate VertexId dynamically
            co_yield output
        }
    } else {
        // standalone: evaluate constant expressions, create one vertex
        co_yield output
    }
}
```

### 类型推断

新增 `boundExprToPropertyType(const BoundExpression&)` 辅助函数，使用 `getBoundExprType()` 获取表达式的 result type，映射到 `PropertyType`。覆盖 `BoundLiteral`、`BoundFunctionCall` 等所有表达式类型。

### VertexId / EdgeId 分配

- 旧: PlanContext 预分配 `assigned_vid_` / `assigned_eid_`
- 新: 所有 VertexId / EdgeId 统一通过 `meta_.nextVertexId()` / `meta_.nextEdgeId()` 运行时动态分配，不再 planner 预分配
- `PlanContext` 已移除 `variable_vertex_ids`、`variable_edge_ids`、`next_vertex_id`、`next_edge_id` 字段

### EvalContext

通过 `PhysicalOperator::setEvalContext(ctx.eval_ctx)` 设置（已在基类中），在 physical planner 创建算子时调用。

---

## 已完成的改动

### Phase 1: 表达式求值 + 属性归属

1. **`physical_planner.cpp`**：新增 `boundExprToPropertyType`；CreateNode/CreateEdge 段改为传递 BoundExpression；类型推断统一使用新辅助函数；`setEvalContext` 调用
2. **`create_node_physical_op.hpp`**：`label_props_` → `label_prop_exprs_`（类型改为 `PropExprs`）
3. **`create_node_physical_op.cpp`**：重写 executeChunk()，使用 VectorizedEvaluator 逐行求值
4. **`create_edge_physical_op.hpp`**：`props_` → `prop_exprs_`；新增 `meta_` 引用
5. **`create_edge_physical_op.cpp`**：重写 executeChunk()，同上模式

### Phase 2: CreateNode/CreateEdge 算子重构

6. **`alter_vertex_label_physical_op.hpp/cpp`**：删除（顶点 pending_props 统一走 `__anon__` 轻量路径）
7. **`CMakeLists.txt`**：移除已删除算子的编译条目
8. **`physical_planner.hpp`**：`PlanContext` 移除 `variable_vertex_ids`、`variable_edge_ids`、`next_vertex_id`、`next_edge_id` 字段
9. **`physical_planner.cpp`**：移除 `AlterVertexLabelPhysicalOp` 构建逻辑；CreateNode 输出 schema 包含 child 列；CreateEdge src/dst 改为列索引
10. **`create_node_physical_op.hpp/cpp`**：移除 `assigned_vid_`；新增 `anon_registered_`、`resolved_pending_`；逐行创建 + 动态 VID + `__anon__` 自动创建
11. **`create_edge_physical_op.hpp/cpp`**：`src_id_`/`dst_id_`/`assigned_eid_` → `src_col_idx_`/`dst_col_idx_`；逐行创建 + 动态 EID + 从 DataChunk 解析 VID
12. **`query_executor.cpp`**：移除 PlanContext 中 ID 预分配初始化
13. **`tests/test_query_executor.cpp`**：更新已有测试 + 新增 23 个单元测试覆盖重构

---

## 已修复的回归 Bug

### Bug 1: 重复 child 执行（8→5 failures）

`executeChunk()` 中存在两个相同的 child 执行块（~line 125 和 ~line 252），各自调用 `child_->executeChunk()` 并 co_yield 输出。对于 chained CREATE `(a), (b), (c)`，导致指数级重复执行（inst=3 调用 1x，inst=2 调用 2x，inst=1 调用 4x → 共 7 次 insertVertex 而非 3 次）。

**修复：** 删除第二个 child 执行块，只保留一个 pass-through 块。

### Bug 2: 多标签属性混淆（5→1 failures）

`buildProps` 将所有标签的属性合并到一个 Properties vector，然后全部赋给 `label_ids_[0]`。对于 `CREATE (n:Person:Employee {age:30, salary:50000})`，salary（Employee 属性）被存到了 Person 下面。

**修复：** 将 `buildProps` 替换为 `buildLabelProps`，按标签分别构建 Properties。`resolved_pending` 从 `(pid, expr)` 改为 `(LabelId, pid, expr)` 三元组以跟踪标签归属。

### Bug 3: 匿名属性类型过于具体（1→0 failures）

旧代码使用 `PropertyType::ANY` 调用 `getOrCreateAnonPropId`。新代码使用 `boundExprToPropertyType(expr)` 返回具体类型（INT64、STRING 等）。这导致 `UnlabeledNodeSamePropNameDifferentTypes` 失败——同名 prop 在不同节点上有不同类型时，第二个节点的值被按第一个节点的类型读取。

**修复：** 匿名标签属性注册改回 `PropertyType::ANY`。

### Bug 4: CREATE 列索引与物理管线不匹配

`CREATE (a)-[r:KNOWS]->(b) RETURN r` 返回 `id=0`。根因：binder 绑定顺序为 (a, r, b)，但物理管线列顺序为 (a, b, r)。`RETURN r` 的 BoundColumnRef 指向错误列。

**修复：** `bindCreate` 中先绑定 dst node 再绑定 edge，使列索引顺序 (a, b, r) 匹配物理管线。

### Bug 5: ORDER BY 在投影之后执行导致列索引错位

`MATCH ()-[r:KNOWS]->() RETURN r.id ORDER BY r.id` 排序无效（返回降序）。根因：ORDER BY 在 ProjectOp 之后执行，但 ORDER BY 表达式 `r.id` 的 BoundColumnRef 引用的是原始 MATCH 列索引，投影后 DataChunk 列数不同。

**修复：** 将 ORDER BY 移到 ProjectOp 之前执行（物理管线：child → Sort → Project）。ORDER BY 引用 RETURN 别名时，重新绑定原始 RETURN 表达式。

### Bug 6: EdgeValue 结构字段访问（r.id）返回 null

`r.id`、`r.src_id` 等边结构字段访问返回 null。根因：binder 的 EDGE 属性分支创建空 BoundPropertyRef（无 candidates、无 property_name），evaluator 无法区分具体字段。

**修复：** BoundPropertyRef 新增 `property_name` 字段；binder 存储字段名并设置 result_type；evaluator 识别结构字段名直接提取。

## 验证结果

- 416 测试全部通过（342 query executor + 74 其他）

---

## 不涉及的改动

- **SetPhysicalOp**：已正确使用 VectorizedEvaluator，无需改动
- **Binder**：已正确绑定函数调用为 BoundFunctionCall，无需改动
- **ValueCodec**：已正确编解码 temporal 类型，无需改动
- **evalPropertyRef**：已正确将 PropertyValue(DateTimeValue) 转回 Value，无需改动
- **temporalAccessorImpl**：已正确提取字段，无需改动

---

## 涉及的 Cypher 模式（需覆盖）

| 模式 | 示例 | 说明 |
|------|------|------|
| 匿名标签 + 常量函数调用 | `CREATE ({dt: date({year:1984,month:10,day:11})})` | TCK Temporal4 |
| 有标签 + 嵌套函数调用 | `CREATE (:Val {dt: date({year:1984,month:10,day:11})})` | TCK Temporal5 |
| 边属性 | `CREATE (a)-[r:T {ts: datetime()}]->(b)` | |
| WITH 传递 | `WITH datetime() AS d CREATE (n {dt: d})` | |
| UNWIND + CREATE | `UNWIND [1,2] AS x CREATE ({v: x})` | 多行 |
| MATCH + CREATE | `MATCH (m) CREATE (n {v: m.x})` | 变量引用 |
| SET temporal | `SET n.dt = datetime()` | 已支持，确认不回归 |
| CREATE 无 RETURN | `CREATE (a:Person)-[:KNOWS]->(b:Person)` | 输出 0 列 |
| ORDER BY r.id | `MATCH ()-[r:T]->() RETURN r ORDER BY r.id` | 边结构字段 + 排序 |

---

## 参考

- 正确范例：`SetPhysicalOp`（`src/query/physical_plan/operator/set_physical_op.cpp`）
- 设计文档：[query-engine-design.md](query-engine-design.md)
- TCK 结果：[tck-results.md](../../tests/tck-results.md)（item 7 已解决，item 8 本轮修复）
