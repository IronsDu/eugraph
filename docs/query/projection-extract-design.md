# ProjectionExtractPhysicalOp 设计文档（**老方案，仅作参考**）

> **⚠️ 本文档描述的是 ProjectionExtract 的**最初实现方案**（projection-extract-op + slot-id-refactor 过渡态），保留作历史参考。**
>
> 该方案已决定**不再继续推进**，主要问题：
> - 维护两套并行列标识（`column_index` runtime + `slot_id` compile-time），导致 base_col / ProjectResetMap / LeftJoinColMap / in_schema_changing_op 等复杂机制纠缠
> - `ConstructVertex` 就地替换列内容，破坏 layout 单调性，引发大量边角 bug
>
> **目标方案见 [demand-pull-lowering-design.md](demand-pull-lowering-design.md)**——基于 SlotId 单一标识 + PE 只追加契约 + 三阶段 batch 流水线。
>
> 本文档的若干内容在新方案中仍然适用（如问题背景、7 种 ColumnSpec、拓扑/语义类型对齐、关键实现细节），可作为参考。

## 一、问题与背景

Scan（LabelScan/AllNodeScan/IndexScan）和 Expand 算子最初在 `executeChunk()` 中**内联加载**顶点/边属性以及顶点 labels。这导致：

1. **算子职责不清晰**：扫描/展开算子同时承担了数据定位、labels 加载、属性加载三件事
2. **性能浪费**：labels 和属性对所有行加载，即使下游 Filter 淘汰大部分行，或者查询根本不需要（如 `count(*)`）
3. **AllNodeScan 尤其浪费**：无条件加载**全部标签的全部属性**
4. **Expand 忽略 `edge_prop_ids_`**：代码实际加载全部边属性，过滤参数未生效

## 二、目标

将原本分散在各拓扑算子（`Expand`, `VarLenExpand`, `LabelScan`）里的 inline property loading 统一到一个算子 `ProjectionExtractPhysicalOp`，根据下游需求**按需选择**轻量（LoadVertexProp 扁平列）或重量（ConstructVertex 完整对象）路径。

```
旧:   Scan → [Filter] → Project
        (加载 labels + 全部属性)

新:   Scan → ProjectionExtract → Filter → Project
        (拓扑id) (按需加载 labels/属性)  (只对幸存行)
```

Scan/Expand 只产出拓扑类型（VertexRef / EdgeKey / PathTopology），需要什么再挂什么算子：

- Scan 输出 `VertexRef{id}` — VERTEX_REF 类型，仅含 id
- Expand 输出 `EdgeKey{id, src_id, dst_id, label_id, seq}` — 仅结构字段
- `ProjectionExtractPhysicalOp` — 融合点/边属性抽取 + Project 语义的统一算子（7 种 ColumnSpec）
- `PathElementPropertyReadPhysicalOp` — 仅用于 PathBuild 路径，将 PathTopology 升级为 PathValue

## 三、整体流程（端到端）

从 Cypher 文本到可执行物理算子树，按以下顺序进行。前三步在 binder，后五步在 `PhysicalPlanner::planBound`（`physical_planner.cpp:959`）。

```
【Binder 阶段】
0. parse + bind        → 生成 BoundLogicalPlan（逻辑算子树）
                         同时为每个变量分配全局唯一 slot_id（makeColumnInfo）
                         query_executor 把 var_slots + slot_allocator 种子写入 PlanContext

【PhysicalPlanner::planBound 阶段】
1. collectPlanRequirements(root)
                       → 一次遍历整棵 bound 树，收集每个变量的属性需求
                         （in_schema_changing_op 分叉：need_whole_vertex vs vertex_props）
                         结果写入 ctx.requirements

2. buildExtractionInfo(root, requirements)
                       → 为需要列偏移校正的变量建立 PropertyExtractionInfo（base_col 等）

3. updateExtractionBaseCols(root, ...)
                       → 遍历算子树累加各算子输出列数，填充 base_col；
                         记录 ProjectResetMap（哪个 Project/Aggregate reset 了哪些变量）
                         和 LeftJoinColMap

4. rewriteColumnIndicesWithResets(root, ...)   ← column_rewrite pass
                       → 把 Filter/Sort 里的 BoundPropertyRef 重写为 BoundColumnRef，
                         并校正 BoundColumnRef 直通引用的 column_index（按 base_col）

5. planBoundOperator(root)   ← 递归自底向上建物理算子
                       → 每遇到拓扑算子（Scan/Expand/VarLenExpand dst）后，
                         调用 dispatchProjectionExtract 插入 ProjectionExtractPhysicalOp，
                         按 requirements 生成 7 种 ColumnSpec（含列去重 existing_cols）

6. finalizePlanResult  → 收尾，得到物理算子树根

7. compileOperatorTree(root)   ← ExpressionCompiler
                       → 自顶向下遍历，每个算子用 TupleSlotLayout 把表达式里的
                         slot_id 解析为运行时 column_index（编译期一次，运行时零开销）
```

**一句话概括**：先收集需求（1）→ 校正逻辑列引用（2–4）→ 建物理算子并按需插入 ProjectionExtract（5）→ 把 slot_id 编译成物理列下标（7）。

## 四、七种 ColumnSpec

`ProjectionExtractPhysicalOp` 的输出 schema 由 ColumnSpec 数组描述，每列有一个来源：

| Kind | 行为 | 列数变化 |
|------|------|---------|
| `Passthrough` | 从上游列直接透传 | — |
| `LoadVertexProp` | 按 (label_id, prop_id) 加载单个点属性 | +1 |
| `LoadEdgeProp` | 按 (edge_label_id, prop_id) 加载单个边属性 | +1 |
| `LoadVertexLabels` | 加载 labels 为 `List<String>` | +1 |
| `LoadEdgeType` | 加载 edge type 为 `String` | +1 |
| `ConstructVertex` | 加载完整 VertexValue（labels + 全部属性），就地替换源列 | 0 |
| `ConstructEdge` | 加载完整 EdgeValue（全部属性），就地替换源列 | 0 |

**核心原则**：
- **覆盖原则**：`need_whole_vertex` 覆盖 `need_props`（同一变量不重复加载）
- **Per-row 缓存**：行内多个 spec 引用同一 source_col 时共享 vid/eid（避免重复解析 Value 变体）
- **列顺序**：Passthrough → Vertex props → Edge props → Vertex labels → Edge type（确保 column_rewrite 偏移计算正确）

## 五、核心决策：两种物理策略

围绕上面 7 种 ColumnSpec，对每个变量归结为两种策略之一：

| 策略 | 触发条件 | 物理行为 | 列数变化 |
|------|---------|---------|---------|
| **ConstructVertex/Edge** | `need_whole_vertex` = true | 就地替换源列为完整对象 | 不变 |
| **LoadVertexProp** | `vertex_props` 非空 | 追加扁平属性列 | +N 列 |

## 六、分叉机制：`in_schema_changing_op`

同一个属性引用 `n.name`，在不同上下文中产生**不同的 BoundExpression 类型**，从而走向不同策略。分叉的关键是一个标志位：`in_schema_changing_op`。

### 6.1 标志位的含义

`in_schema_changing_op = true`：表达式位于**会改变输出 schema 的算子内**（Project/WITH, Aggregate）。此时 flat column index 无法追踪——因为上游 schema 变了，原来算好的列偏移失效。因此**保守走重量路径**（need_whole_vertex），让 evaluator 从完整对象运行时解析属性。

`in_schema_changing_op = false`：表达式位于**不改变 schema 的算子内**（Filter, Sort）。列索引稳定，可以**精确走轻量路径**（vertex_props），只加载需要的属性。

### 6.2 代码实现

在 `column_rewrite.cpp` 的 `collectExprReqs` 中：

```cpp
// BoundPropertyRef 分支
if (in_schema_changing_op) {
    // Project/WITH/Aggregate 内部 → 重量路径
    vr.need_whole_vertex = true;
} else {
    // Filter/Sort 内部 → 轻量路径
    for (const auto& cand : ptr->candidates)
        vr.vertex_props.emplace_back(cand.label_id, cand.prop_id);
}

// BoundColumnRef 分支（RETURN n, WITH n）
if (ptr.type.kind == BoundTypeKind::VERTEX)
    reqs[ptr.name].need_whole_vertex = true;
```

以下情况也触发 `need_whole_vertex`：
- `BoundDynamicPropertyRef`（无 label 约束，运行时按名字查找属性）
- `type(r)`, `labels(n)`, `startNode(r)`, `endNode(r)` 函数调用
- `SET n.x` / `REMOVE n.x`
- `BoundLabelCast`（`n::Label`）
- `DELETE` target（按 TargetKind 设置 need_whole_vertex / need_whole_edge）
- `properties(n)` / `keys(n)`（按参数类型设置 need_whole_vertex / need_whole_edge）

### 6.3 具体例子：`MATCH (n) RETURN n, n.name`

```
1. collectPlanRequirements 遍历
   Project(RETURN) 的 items: [n, n.name]

2. n: BoundColumnRef, type=VERTEX
   → reqs["n"].need_whole_vertex = true

3. n.name: BoundPropertyRef, in_schema_changing_op=true
   → reqs["n"].need_whole_vertex = true

4. 合并结果: reqs["n"] = {need_whole_vertex: true}

5. dispatchProjectionExtract:
   need_whole_vertex=true → ConstructVertex 就地替换
   → 物理输出 1 列 [n:VERTEX]，不追加列
```

### 6.4 具体例子：`MATCH (n:Person) WHERE n.name = 'Alice' RETURN 1`

```
1. collectPlanRequirements 遍历
   Filter 的 predicate: n.name = 'Alice'

2. n.name: BoundPropertyRef, in_schema_changing_op=false
   → reqs["n"].vertex_props = [(Person_label, name_prop_id)]

3. RETURN 1: BoundLiteral → 无变量引用

4. 合并结果: reqs["n"] = {vertex_props: [(Person, name)], need_whole_vertex: false}

5. dispatchProjectionExtract:
   vertex_props 非空, need_whole_vertex=false
   → Passthrough(n) + LoadVertexProp(n.name)
   → 物理输出 2 列 [n:VERTEX_REF, n.name:STRING]
```

### 6.5 covering principle

同一变量同时触及两条路径时（如 `WHERE n.name='Alice' RETURN n`），合并结果中有 `need_whole_vertex=true`。`dispatchProjectionExtract` 优先走 ConstructVertex，跳过 LoadVertexProp：

```
reqs["n"] = {need_whole_vertex: true, vertex_props: [(Person, name)]}

dispatchProjectionExtract:
  need_whole_vertex=true → ConstructVertex（覆盖原则）
  vertex_props 被跳过
```

## 七、关键洞察：列偏移只在一个特定场景发生

**`RETURN n` 是否存在，决定了是否触发 `need_whole_vertex`**。

因为 Project 内部所有属性引用走 `in_schema_changing_op=true` → `need_whole_vertex`。只有 Filter/Sort 中的属性引用才走 `vertex_props`。所以：

- 有 `RETURN n`（或在 WITH/Project 中引用属性）→ `need_whole_vertex` → ConstructVertex → **不偏移**
- 只有 Filter/Sort 引用属性 → `vertex_props` → LoadVertexProp → **偏移**

列偏移**只在** Filter/Sort 引用属性但外层没有 `RETURN n` 的场景。

## 八、需求收集与合并（collectPlanRequirements）

`collectPlanRequirements` 从 BoundLogicalOperator 树根遍历，收集每个变量的需求。同一变量可能在多处被引用，产生不同需求：

```
Filter: n.name='Alice' → vertex_props = [(Person, name)]
RETURN: n              → need_whole_vertex = true
───────────────────────────────────────────────────
合并:   reqs["n"] = {need_whole_vertex: true, vertex_props: [(Person, name)]}
```

`dispatchProjectionExtract` 看到合并后的需求，`need_whole_vertex` 优先（covering principle），走 ConstructVertex。

关键路由逻辑：
- ProjectOp / AggregateOp 内（`in_schema_changing_op=true`）：BoundPropertyRef 路由到 `need_whole_vertex`
- FilterOp / SortOp 内（`in_schema_changing_op=false`）：BoundPropertyRef 追加到 `vertex_props` / `edge_props`
- SET / REMOVE target：`need_whole_vertex = true`
- DELETE target：按 TargetKind 设 `need_whole_vertex` / `need_whole_edge`
- `BoundLabelCast`（`n::Label`）：`need_whole_vertex = true`
- `properties(n)` / `keys(n)`：按参数类型设 `need_whole_vertex` / `need_whole_edge`
- `labels(n)`：`need_vertex_labels = true`
- `type(r)`：`need_edge_type = true`

### 8.1 表达式覆盖（collectExprReqs 必须递归的类型）

`collectExprReqs` 必须为所有复合表达式类型写递归分支，否则内部对外部变量的引用无法触发需求收集：

- `BoundMap`：`RETURN {node1: n, rel: r}` 中 `n`、`r` 需触发 `need_whole_vertex/edge`
- `BoundSubscript` / `BoundSlice`
- `BoundAllExpr` / `BoundAnyExpr` / `BoundNoneExpr` / `BoundSingleExpr`：如 `all(x IN nodes(p) WHERE x.name = 'a')` 中 `x.name`
- `BoundListComprehension`：如 `RETURN [key IN keys(r) | r[key]]` 中 `keys(r)` 与 `r[key]` 均引用边属性

列表推导式 / 量词表达式的**循环变量**作为 `loop_var_skip` 形参下传：`BoundVariableRef` / `BoundColumnRef` 命中该名字时不登记 plan-level requirement（循环变量属于 runtime 作用域，不在 plan 输出列里）。

## 九、需求何时为"空"

`VariableRequirement.empty()` 返回 true 当**所有字段**为 false/空：

```cpp
bool empty() const {
    return !need_whole_vertex && !need_whole_edge && !need_vertex_labels &&
           !need_edge_type && vertex_props.empty() && edge_props.empty();
}
```

`dispatchProjectionExtract` 遇到空需求时，走 Passthrough（不加列，不构造）。

## 十、dispatchProjectionExtract

替代旧的 5 处 inline loader 调用（Scan × 3 + IndexScan + Expand dst + VarLenExpand dst）。读取 `ctx.requirements`，为每个变量构建 ColumnSpec 数组，插入 `ProjectionExtractPhysicalOp`。

| 逻辑算子 | 抽取方式 |
|---------|------|
| BoundScanOp / BoundLabelScanOp | `dispatchProjectionExtract` |
| BoundExpandOp | `dispatchProjectionExtract` |
| BoundVarLenExpandOp | `dispatchProjectionExtract` (dst) + `wrapPathElementPropertyRead` (path, 若存在) |
| BoundPathBuildOp | `wrapPathElementPropertyRead` |
| tryBoundIndexScan / tryBoundEdgeIndexScan | `dispatchProjectionExtract` |

**列去重**：ProjectionExtract 在管线中可能多次执行（Filter 内联属性抽取 + 后续算子再次抽取），第二次会按 reqs 重复 emit `var.prop` 列，挤掉下游列。`dispatchProjectionExtract` 构建 `existing_cols` 集合，已存在的 `var.prop_name` 跳过。

## 十一、PathElementPropertyReadPhysicalOp

`src/query/physical_plan/operator/path_element_property_read_physical_op.hpp/.cpp`

仅用于 PathBuild / VarLenExpand path 路径，读路径元素属性和标签，将 PathTopology 升级为 PathValue。

```
输入:  DataChunk, col_idx 指向 PATH 列 (PathValue)
参数:  path_variable
行为:  遍历每行 PathValue 的 elements：
       - VertexValue: 若 properties 为空，getVertexLabels(id) → 对每个 label getVertexProperties(id, lid)
       - EdgeValue:   若 properties 为空，getEdgeProperties(label_id, id)
输出:  同 schema，path 列被替换为填充属性后的 PathValue
```

> **当前限制（待优化）**：此算子为路径内所有顶点/边加载**全部属性**。原因是路径元素类型在 binder 阶段为 ANY，`x.name` 绑定为 `BoundDynamicPropertyRef`（运行时动态查找），planner 无法在编译期确定需要哪些属性。
>
> **优化方向 A（按需加载）**：增强 planner，从 quantifier WHERE 子句等表达式树中提取 `BoundDynamicPropertyRef` 的 property name 集合，传给此算子按需加载。
>
> **优化方向 B（消除此算子）**：在 binder 阶段分析 `nodes(p)` / `relationships(p)` 被哪些表达式消费，推导路径内顶点/边需要哪些属性，复用 `ProjectionExtractPhysicalOp` 的列加载能力作用于 PathValue 内的元素。

## 十二、关键实现细节

### 12.1 DICTIONARY selection vector 保留

Read 算子对 DICTIONARY 列做 passthrough 时必须**保留原始 dict_sel**，不能用 identity selection 替换。原始 buffer 的行数可能远小于逻辑行数（如 CorrelatedSource buffer 只有 1 行，但 Expand 输出 N 行）。FLAT 列则使用 identity selection。

### 12.2 自关系变量搜索

`(n)-[r]->(n)` 中变量 `n` 在 schema 中出现两次。dispatchProjectionExtract 的变量查找从后往前搜索，匹配 Expand 产出的新列（末尾出现），而非 Scan 的输入列。

### 12.3 keys(n) 全属性需求

`keys(n)` 和 `properties(n)` 在 binder 中触发所有标签的全属性需求，确保 ConstructVertex 加载全部属性。TCK 快照验证依赖此行为。

### 12.4 VarLenExpand 边属性过滤保留

VarLenExpand 的 `edge_prop_filters_`（DFS 逐跳过滤）继续使用 `getEdgeProperties()`。输出 EdgeValue 已剥离属性。

### 12.5 VarLenExpand 路径元素 labels 加载

VarLenExpand 构建 PathValue 时为所有内部顶点（含中间节点和终点）调用 `getVertexLabels()` 填充 `VertexValue.labels`。这是 `nodes(p)` / `relationships(p)` 能在结果中正确显示标签的前提。

### 12.6 异构列表的全属性预加载

当 VERTEX 和 EDGE 类型的列被放入同一个列表（如 `[n, r]`），binder 的 `BoundType::merge` 会将结果类型合并为 ANY。此时列表元素绑定为 `BoundDynamicPropertyRef`。为此，`bindListExpr` 检测到合并类型为 ANY 时，对列表中每个 VERTEX/EDGE 元素预加载其所有标签的全部属性。

## 十三、拓扑/语义类型对齐

ProjectionExtract 落地后暴露出三类「binder 类型为语义形式（VERTEX），运行时为拓扑形式（VertexRef）」的边界场景：

- **CorrelatedSource**：binder 把 correlated 变量类型标为 `VERTEX`，但 LeftJoin 实际传入的是上游扫描产出的 `VertexRef`，`Column::setValue(VERTEX, VertexRef)` 静默丢弃 → OPTIONAL MATCH 全表 0 命中。修复：`CorrelatedSourcePhysicalOp::executeChunk` 按运行时值推导列 kind。
- **Project 的 BoundColumnRef 直通**：`WITH b` / `RETURN b` 中 item 是 `BoundColumnRef`，binder 类型为 `VERTEX`，Project 据此建 VERTEX 列，写入 VertexRef 时被丢弃。修复：Project 对 `BoundColumnRef` item 改用源列的运行时 type。
- **column_rewrite 漏改直通引用**：`BoundColumnRef`（item.expr 本身就是直接变量引用）在上游 ProjectionExtract 追加属性列后，源列下标发生偏移。修复：`rewriteExpr` 新增 `BoundColumnRef` 分支，按 `PropertyExtractionInfo::base_col` 校正 `column_index`。
- **base_col 统计补全**：`updateBaseCols` 补齐 `BoundCorrelatedSourceOp`（右子树 correlated 列）和 `BoundVarLenExpandOp`（dst/path/edge 输出列）的列数累加。

## 十四、相关文件

| 文件 | 作用 |
|------|------|
| `column_rewrite.cpp:collectExprReqs` | 需求收集，`in_schema_changing_op` 分叉 |
| `physical_planner.cpp:dispatchProjectionExtract` | 物理列生成，covering principle |
| `physical_planner.cpp:makeSlotLayout` | Layout 构建（SlotId 架构） |
| `bind_pattern.cpp:makeColumnInfo` | SlotId 分配 |
| `bind_return.cpp:bindWith` | WITH scope reset, slot_id 携带 |
| `operator/projection_extract_physical_op.hpp/.cpp` | 7 种 ColumnSpec 执行 |
| `operator/path_element_property_read_physical_op.hpp/.cpp` | PathBuild 路径元素属性加载 |
