# ProjectionExtract 按需属性物化设计

> `ProjectionExtractPhysicalOp` 是 EuGraph 查询引擎的按需属性物化算子。它插入在 Scan/Expand 等拓扑算子的输出位置，根据下游消费者的需求（Filter 过滤某属性、RETURN 返回整节点、SET/REMOVE 修改节点等），一次性加载所需的属性、标签或构造完整对象，输出适配下游需求的扁平列布局。核心原则：**拓扑算子只生产最小形态（VertexRef/EdgeKey），语义数据由 Extractor 按需追加。本文统称"Demand-Pull Lowering（DPL）"。**

## 一、核心数据结构

### 1.1 SlotId

`SlotId` 是 `uint32_t` 标识符，在 binder/Optimizer 阶段分配，物理规划期通过 `TupleSlotLayout` 映射为物理列下标。两个区间：

- 用户可见 slot：`[1, 2^31)`，由 `SlotAllocator::next()` 分配
- 内部 slot（PE 追加列）：`[2^31, 2^32)`，由 `SlotAllocator::nextInternal()` 分配，bit 31 = `kInternalSlotFlag`

内部 slot 在物理列里是普通列，只是命名用 `__pe_<slot>` 前缀避免与用户变量名冲突。后续若需要过滤内部列，可直接判定 `isInternalSlot(id)`。

### 1.2 PEPlan

每个变量的物化决策结果。一份 PEPlan 描述为一个变量追加的所有列。

```cpp
struct PEPlan {
    SlotId source_slot_id = INVALID_SLOT_ID;     // 源变量 canonical slot
    SlotId object_slot_id = INVALID_SLOT_ID;     // ConstructVertex/Edge 对象列
    // 扁平点属性列：(label, prop) → slot 的parallel arrays
    vector<pair<LabelId, uint16_t>> prop_order;
    vector<SlotId> prop_slot_ids;
    // 扁平边属性列
    vector<pair<EdgeLabelId, uint16_t>> edge_prop_order;
    vector<SlotId> edge_prop_slot_ids;
    SlotId labels_slot_id = INVALID_SLOT_ID;     // LoadVertexLabels
    SlotId type_slot_id = INVALID_SLOT_ID;       // LoadEdgeType
};
```

覆盖原则：若 `object_slot_id != INVALID`，所有 `prop_*` / `labels_*` / `type_*` slot 闲置（属性已在对象内），运行时从对象列取值。

若 source 本身已是构造对象（CreateNode/Merge 输出 VERTEX），`object_slot_id` 直接 alias `source_slot_id`，不追加列。

### 1.3 VariableRequirement / PlanRequirements

下游消费者的需求声明，由阶段 1（Collect）遍历 BoundExpression 树汇总。

```cpp
struct VariableRequirement {
    bool need_whole_vertex = false;   // RETURN n / SET n / BoundDynamicPropertyRef
    bool need_whole_edge = false;     // RETURN r / BoundDynamicPropertyRef(edge)
    bool need_vertex_labels = false;  // labels(n) / n::Label
    bool need_edge_type = false;      // type(r)
    vector<pair<LabelId, uint16_t>> vertex_props;       // n.name / n.age 等
    vector<pair<EdgeLabelId, uint16_t>> edge_props;     // r.since 等
};
using PlanRequirements = unordered_map<SlotId, VariableRequirement>;
```

需求按 canonical slot 聚合，所有消费者的需求合并到同一 key 下。

### 1.4 ColumnSpec（7 种）

`ProjectionExtractPhysicalOp` 按 `ColumnSpec` 数组生成输出列：

| Kind | 语义 | 触发条件 |
|------|------|----------|
| `Passthrough` | 原样复制上游列 | 无需求的中间变量 |
| `LoadVertexProp` | `store.getVertexProperty(vref, label, prop)` | 扁平点属性访问 |
| `LoadEdgeProp` | `store.getEdgeProperty(ekey, label, prop)` | 扁平边属性访问 |
| `LoadVertexLabels` | `store.getVertexLabels(vref)` → `List<String>` | `labels(n)` / `n::Label` |
| `LoadEdgeType` | 内存字典 `edge_label_id → name` | `type(r)` |
| `ConstructVertex` | `store.getVertexLabels + getVertexProperty` → `VertexValue` | `need_whole_vertex` |
| `ConstructEdge` | 内存组装 `EdgeValue` | `need_whole_edge` |

### 1.5 SlotResolver

统一 name→canonical 解析的只读视图，持有 `NameSlotMap const&` 和 `AliasSlotMap const&`：

```cpp
class SlotResolver {
    SlotId slotForName(const string& name) const;
    SlotId canonicalOf(SlotId slot) const;           // alias_map 链式解析
    SlotId canonicalForName(const string& name) const; // name → slot → canonical
};
```

所有 read 路径统一经过 SlotResolver，避免各处复制 canonical 逻辑导致需求碎片化。

### 1.6 ProjectItem 字段

`BoundProjectOp::ProjectItem` 内置于字段驱动 scope 隔离（方案 B'）：

```cpp
struct ProjectItem {
    BoundExpression expr;
    string alias;
    BoundType result_type;
    SlotId output_slot = INVALID_SLOT_ID;  // 物理 Project 输出槽
    SlotId input_slot  = INVALID_SLOT_ID;  // src 在输入 scope 的槽
    SlotId alias_slot  = INVALID_SLOT_ID;  // 本 Project 为 alias 分配的槽
};
```

`input_slot` / `alias_slot` 在 `allocateSlotsInOp` 中捕获，取代从全局 `name_to_slot` 取值。这是多层 WITH scope 隔离的关键基础设施（见 §5.1）。

## 二、六阶段管线

`PhysicalPlanner::planBound` 入口按顺序运行：

```
Phase 0:  Slot 预分配
  allocateAllSlots         → 每个变量名分配 slot_id
  collectAliasSlotMap      → 构建 alias_map（alias_slot → input_slot）
  buildFreshExpandMap      → 收集 WITH 后重新引入的 Expand 变量

Phase 1: 需求收集
  collectPlanRequirements  → 遍历下游表达式，汇总 per-slot VariableRequirement

Phase 2: 决策
  collectSourceTypes       → 获取各 slot 的 BoundTypeKind（VERTEX_REF vs VERTEX）
  buildExtractionInfo      → 根据 reqs + source_types 生成 PEPlan，分配内部 slot

Phase 3: 别名透传
  lowerAliasPassthrough    → 为每个 WITH alias 传播 PE 派生 slot

Phase 4: 表达式改写
  rewriteColumnIndices     → BoundPropertyRef → BoundColumnRef(slot_id)
```

完成上述 Pass 后进入 `planBoundOperator`（RBO 遍历 Bound 树生成物理算子），最后 `compileOperatorTree` 把 `slot_id` 映射为物理列下标。

### Phase 0: Slot 预分配

**`allocateAllSlots`**：深度优先遍历 Bound 树，为每个变量（Scan/LabelScan/Expand/Create/Project alias/Aggregate 等）分配 slot_id。递归进入子节点（bottom-up）：先处理 child，再捕获 `input_slot`，最后分配 `alias_slot`。这是 scope 隔离的关键——child 结束后输入 scope 固定，再覆写 `name_to_slot` 才不会干扰 `input_slot` 捕获。

**`collectAliasSlotMap`**：遍历 Project 中 `BoundVariableRef(X) AS Y`（X ≠ Y），记录 `alias_map[slot(Y)] = slot(X)`。读取 `item.input_slot` / `item.alias_slot` 而非从 `name_to_slot` 查。

**`buildFreshExpandMap`**：当 MATCH 在 WITH 后重新引入同名变量（`WITH a AS b ... MATCH (a)-[r]->(b)`），binder 为 Expand 的 dst/edge 分配新的 planner slot（不与别名链混淆）。该 map 传给 Phase 1/Phase 2 以创建独立 PEPlan。

### Phase 1: 需求收集

`collectPlanRequirements` 遍历整个 Bound 树，从每个下游消费者的表达式中提取需求：
- **Project/RETURN**：按 `BoundColumnRef` 类型标记 `need_whole_*`（VERTEX/EDGE/PATH 类型）；对 `BoundPropertyRef` 提取 `(label, prop)` 对；对 `labels(n)`/`type(r)` 标记标签/类型需求
- **Filter/Sort/Aggregate**：`PropertyRef` 提取属性需求
- **SET/REMOVE/DELETE**：目标变量标记 `need_whole_*`
- **MERGE/CREATE**：属性过滤表达式（`*_prop_filters` / `label_properties` / `properties`）和未解析表达式（`pending_props`）均提取需求
- **keys()/properties()**：对 VERTEX/VERTEX_REF 标记 `need_whole_vertex`，对 EDGE 标记 `need_whole_edge`，对 MAP 不标需求

所有变量引用经 `SlotResolver::canonicalForName` 解析为 canonical slot ID，需求按 canonical slot 聚合。

### Phase 2: 决策

`collectSourceTypes` 记录各 slot 的 `BoundTypeKind`：Scan/Expand 输出 `VERTEX_REF`/`EDGE_KEY`，CreateNode/CreateEdge/Merge 输出 `VERTEX`/`EDGE`。

`buildExtractionInfo` 按变量逐个决策：

| source 已是构造对象 (VERTEX/EDGE) | `need_whole_*` | 决策 |
|----------------------------------|----------------|------|
| 是 | true | `object_slot_id = source_slot_id`（alias，不追加 Construct 列） |
| 是 | false | 仅按需追加扁平 prop/labels/type 列 |
| 否 | true | 分配 `object_slot_id`（ConstructVertex/Edge）+ 跳过扁平列（覆盖原则） |
| 否 | false | 按需分配扁平 prop/labels/type 列 |

覆盖原则：一旦 `object_slot_id` 是真正的 Construct 列（非 alias），所有扁平需求被吸收——运行时从构造对象取值。

### Phase 3: 别名透传

`lowerAliasPassthrough` 的核心问题：`WITH n AS a ... RETURN a.name` 中，`a` 是 `n` 的透传别名。`n` 的 PEPlan 里有追加属性列，但 `a` 的 PEPlan（如果有的话）是空的——Phase 1 的需求收集 key 在 `n` 的 canonical slot 上。

解法：对所有 `BoundVariableRef(X) AS Y` 透传项，将 `X` 的 PEPlan 派生 slot 全部传播到 `Y`：为每个派生 slot 分配内部 alias slot，记录 `alias_map[alias_slot] = derived_slot`，并向 Project 追加 `BoundColumnRef(derived_slot) AS __alias_<Y>_<kind>` passthrough item。Phase 4 的 rewrite 自动将 `a.name` 的 `BoundPropertyRef` 的 target slot 从 `SLOT_N_OBJ` 改为 `SLOT_A_OBJ_ALIAS`。

### Phase 4: 表达式改写

`rewriteColumnIndices` → `rewriteOp` → `rewriteExpr` 递归处理所有 BoundExpression：

- `BoundPropertyRef(n.name)`：若 `n` 有 `object_slot_id`，重定向 `n` 的内层 `BoundColumnRef.slot_id` 到 `object_slot_id`，保留 `BoundPropertyRef` 外壳（运行时从 `VertexValue` 取值）。否则，用 `prop_order[i]` 匹配 `candidates`，找到后整体替换为 `BoundColumnRef(prop_slot_ids[i])`。
- `BoundVariableRef` / `BoundColumnRef`：若有 `object_slot_id`，重定向到对象列。
- CREATE/MERGE 的属性表达式和 `on_create/on_match` items：与 Filter/Project 同等改写。
- `BoundDynamicPropertyRef`：若有 object slot，重定向；否则不处理（需 whole-object 才可运行）。

## 三、物理算子

### 3.1 ProjectionExtractPhysicalOp

输入：子算子的拓扑输出（含 `VertexRef` / `EdgeKey` / `VertexValue` 等）。输出：按 `ColumnSpec[]` 定义的列。执行流程：

1. 从子算子拉取 chunk
2. 遍历每个 ColumnSpec：
   - `Passthrough`：整体复制上游列
   - `LoadVertexProp` / `LoadEdgeProp`：逐行 `co_await store.getVertexProperty/getEdgeProperty`
   - `LoadVertexLabels`：逐行 `co_await store.getVertexLabels` → `List<String>`
   - `LoadEdgeType`：内存查字典 `edge_label_id → name`
   - `ConstructVertex`：逐行加载 labels + 全部属性 → `VertexValue`
   - `ConstructEdge`：内存组装 `EdgeValue`
3. `co_yield` 输出 chunk

### 3.2 dispatchProjectionExtract

在 `physical_planner.cpp` 的 RBO 分支中，对每个 scan/expand 的 `PlanOperatorResult` 调用，查询 PEPlan 生成 `ColumnSpec[]` 并插入 `ProjectionExtractPhysicalOp`：

```cpp
plan_result = dispatchProjectionExtract(plan_result, var, ctx);
```

内部逻辑：遍历该变量的 PEPlan，按需 emit `Passthrough` + `LoadVertexProp`* + `ConstructVertex` 等 spec。若 `object_slot_id == source_slot_id`（source 已是构造对象），跳过 Construct。

### 3.3 compileExpressions 协作

物理算子通过 `compileExpressions(input_layout)` 将表达式中的 `slot_id` 编译为物理列下标：

- `FilterPhysicalOp`：编译 predicate
- `ProjectPhysicalOp`：编译所有 items
- `SortPhysicalOp`：编译所有 sort key
- `CreateNodePhysicalOp`：编译 `label_prop_exprs_` + `pending_props_`
- `CreateEdgePhysicalOp`：编译 `prop_exprs_` + `pending_props_`
- `MergePhysicalOp`：编译 `*_prop_filters_` + `*_pending_props_` + `on_create/on_match items`
- `SetPhysicalOp`：编译所有 items 的 `value_expr`

`ExpressionCompiler` 通过 `TupleSlotLayout::getColumnIndex(slot_id)` 将表达式树中的所有 `BoundColumnRef.slot_id` 固化为 `column_index`，运行时直接按列号读 `chunk.columns[idx]`。

## 四、示例

### 示例 1：`MATCH (n:Person) WHERE n.name = 'Alice' RETURN n, n.age`

**Bound 树**（简化）：
```
BoundProjectOp(items=[n, n.age])
  BoundFilterOp(predicate=n.name = 'Alice')
    BoundLabelScanOp(variable=n, labels=[Person])
```

**Phase 0**：`allocateAllSlots` 为 `n` 分配 `SLOT_N=1`。

**Phase 1**：需求收集
- Filter 访问 `n.name` → `reqs[SLOT_N].vertex_props = [(Person, name)]`
- Project item 0：`BoundColumnRef(n)` of VERTEX_REF → `need_whole_vertex = true`

合并需求：`reqs[SLOT_N] = {need_whole_vertex=true, vertex_props=[(Person, name)]}`

**Phase 2**：决策
- LabelScan 输出 VERTEX_REF → 非构造对象
- `need_whole_vertex=true` → 分配 `SLOT_N_OBJ`（ConstructVertex）
- vertex_props 被覆盖原则吸收，不分配扁平列

PEPlan：`{source=SLOT_N, object=SLOT_N_OBJ}`

**Phase 3**：无 WITH alias，跳过。

**Phase 4**：改写
- Filter predicate `n.name`：`BoundPropertyRef` 找到 `PEPlan[SLOT_N].object_slot_id`，走 covering 路径 → 内层 `BoundColumnRef.slot_id = SLOT_N_OBJ`，外层保留 `BoundPropertyRef`（运行时从 VertexValue 取 name）
- Project item 1 `n.age`同上

**物理算子**（RBO 展开）：
```
ProjectPhysicalOp(items=[BoundColumnRef(SLOT_N_OBJ), BoundPropertyRef(BoundColumnRef(SLOT_N_OBJ), age)])
  ProjectionExtractPhysicalOp(specs=[Passthrough(SLOT_N), ConstructVertex(SLOT_N_OBJ)])
    LabelScanPhysicalOp(n)
```

**运行时**：LabelScan 输出 `[n:VertexRef]` → PE 追加 `[n_obj:VertexValue]` → Project 从 `n_obj` 读整对象和属性。

### 示例 2：`MATCH (n:Person) WHERE n.name = 'Alice' RETURN n.name`

与示例 1 区别：Project 不要求整对象，只有 `n.name` 访问。

Phase 1 需求：`reqs[SLOT_N] = {vertex_props=[(Person, name)]}`（无 `need_whole_vertex`）。

Phase 2 决策：无覆盖 → 分配 `SLOT_N_NAME`（LoadVertexProp）。

PEPlan：`{source=SLOT_N, prop_order=[(Person, name)], prop_slot_ids=[SLOT_N_NAME]}`

Phase 4 改写：`n.name` 走 flat 路径 → 整体替换为 `BoundColumnRef(SLOT_N_NAME)`。

**物理算子**：
```
ProjectPhysicalOp(items=[BoundColumnRef(SLOT_N_NAME)])
  ProjectionExtractPhysicalOp(specs=[Passthrough(SLOT_N), LoadVertexProp(SLOT_N_NAME, Person, name)])
    LabelScanPhysicalOp(n)
```

与示例 1 对比：示例 1 因 `RETURN n` 走 ConstructVertex（加载全部属性），示例 2 因只返回 `n.name` 走 LoadVertexProp（加载单个属性）。这正是按需决策的核心——同一 Schema 不同需求对应不同的 PEPlan。

## 五、已知未解决问题

### 5.1 多层 WITH 别名链 scope 隔离（部分已修复）

**现象**：`WITH7` scenario 1/2（多层 `WITH ... AS ...` 后再消费）返回 0 行或 null。

**根因**：`allocateSlotsInOp` 按"名字"分配 slot_id，同一名字在不同 scope 复用同一 slot_id。全局 `alias_map` 记录所有 Project 的别名关系，多层 WITH 反向 rename 时形成环（`SLOT_A → SLOT_B → SLOT_A`），`getCanonicalSlot` 循环检测返回错误 canonical，PEPlan 查不到对应 plan。

**已实施**：方案 B'（`ProjectItem` 加 `input_slot`/`alias_slot` 字段 + `collectAliasSlotMapOp`/`lowerAliasPassthroughOp` 改读 per-item 字段）+ fresh Expand scope-aware canonical resolution。两个改动将 scope 信息局部化到 item，等效替换全局 `name_to_slot` 查询。主要 case（With7Scenario1, Merge5Scenario16）已通过。

**残留**：还有少量边界场景（2/8 DPL 回归测试）未通过。完整替代方案（per-Op `OpAliasScope` + 5 个 visitor signature 链改造）工程量较大，暂未实施。

**风险**：低。当前实现不 crash，错误场景返回空结果或 null。方案 B' 已覆盖绝大多数实际查询。

**相关文件**：`src/query/optimizer/column_rewrite.cpp`、`src/query/physical_plan/physical_planner.cpp`、`src/query/planner/slot_id.hpp`

### 5.2 SlotId 分配是 binder 后补丁

`allocateAllSlots` 是在 binder 完成后的预 Pass 中分配 slot_id，而非 binder 自己分配。这意味着别名、遮蔽、派生列的 slot 都是事后从 Bound 树反推。

理想方案：binder 在绑定时分配所有 slot（含 alias、shadowed、derived），消除外部 Pass 的复杂度。但 binder 改动大，当前风险低。

### 5.3 变量遮蔽的 type gate 工作区

`WITH a.name AS a` 中 binder 复用 `a` 的 slot_id，但语义从 VERTEX 变为 STRING。`lowerAliasPassthroughOp` 通过 `isGraphKind(fwd_kind)` type gate 拦截——非图类型的 forward alias 不走 PEPlan 提升。这是工作区，根本修复需要 binder 在遮蔽时分配新 slot_id。

风险：低。type gate 覆盖了已知 case，仅在出现 binder 发出非图类型但 optimizer 错误认为是图类型的 forward 时漏。

## 六、相关文件

| 文件 | 角色 |
|------|------|
| `src/query/optimizer/column_rewrite.hpp` | PEPlan、VariableRequirement、SlotResolver、6 个 Pass 声明 |
| `src/query/optimizer/column_rewrite.cpp` | 6 个 Pass 实现（~1400 行） |
| `src/query/optimizer/requirement_collector.cpp` | `collectOpRequirements`（CBO 路径用） |
| `src/query/physical_plan/physical_planner.cpp` | `planBound` 入口、`dispatchProjectionExtract`、`compileOperatorTree` |
| `src/query/physical_plan/operator/projection_extract_physical_op.hpp` | `ColumnSpec`、`ProjectionExtractPhysicalOp` |
| `src/query/physical_plan/expression_compiler.hpp` | `ExpressionCompiler`：slot→column 编译 |
| `src/query/planner/slot_id.hpp` | `SlotId`、`SlotAllocator`（用户/内部双区间） |
| `docs/query/engine/slot-id-design.md` | SlotId 系统设计 |
| `docs/query/engine/physical-operator-audit.md` | 物理算子职责审计 |
