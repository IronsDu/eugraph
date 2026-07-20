# SlotId 架构设计文档

## 一、背景与动机

### 1.1 当前 `column_rewrite` 架构的问题

`feature/projection-extract-op` 分支的目标是将分散在各拓扑算子中的 inline property loading 统一到 `ProjectionExtractPhysicalOp`。

实现分为三步：

1. `collectPlanRequirements` 扫描表达式树，收集每个变量需要的属性
2. `dispatchProjectionExtract` 在每个拓扑算子后包裹物理属性加载器，**可能追加扁平列**
3. `column_rewrite`（`updateBaseCols` + `rewriteOp`）重写表达式中的列索引

**核心痛点在第 3 步**：因为第 2 步的 `dispatchProjectionExtract` 改变了物理列数（追加 `LoadVertexProp`/`LoadEdgeProp` 等扁平列），但第 3 步的 `column_rewrite` 用第 1 步的 binder 列数来计算索引，三层列数不同步。

具体表现：

- **`column_rewrite.cpp` 已膨胀到 ~1200 行**，包含 `updateBaseCols`、`rewriteOp`、`buildInputScope`、`project_resets`、`left_join_cols` 等多个相互耦合的机制
- **7 个 bugfix commit 全部在修边界 case**：LeftJoin 右子树偏移、BinaryJoin 同理、SemiJoin 漏遍历、Project reset 跨子树污染、CREATE pending_props 漏收集 等
- **2 个剩余回归无法安全修复**（Match7 [7]、With7 [1]），因为涉及物理列布局与 binder 列数的不匹配

### 1.2 根因：column_index 作为表达式的载体

整个问题的根源在于：**BoundColumnRef 持有的是物理数组下标 `column_index`**。

当 `dispatchProjectionExtract` 追加扁平列时，所有后续列的物理下标都会变化。`column_rewrite` 必须递归遍历整棵表达式树来修正这些下标——而 Join 子树、WITH 别名、Project 重命名等使得这个过程极其复杂且脆弱。

### 1.3 DuckDB 的启示：ColumnBinding

DuckDB 在逻辑表达式层使用 `ColumnBinding`（一个 `{table_index, column_index}` 的结构体），**而不是物理数组下标**。物理下标只在算子初始化时通过 `ColumnBindingResolver` 一次性翻译并固化。

```cpp
// DuckDB: src/include/common/types/column_binding.hpp
struct ColumnBinding {
    idx_t table_index;   // 逻辑算子节点的 ID
    idx_t column_index;  // 该算子输出的第几列
};
```

DuckDB 的所有逻辑算子必须实现 `GetColumnBindings()`，返回当前输出列的 Binding 列表。Join 拼接时直接 merge 左右列表，Projection 重命名时产生新的 Binding。**无论如何变形，表达式持有的 Binding 永远不变**。

## 二、设计目标

用逻辑上不可变的 **SlotId** 替代物理 **column_index** 作为表达式的列引用载体。物理列下标只在算子 `init()` 时通过 `TupleSlotLayout` 一次性翻译并缓存。

### 2.1 核心收益

| 维度 | 当前架构 | SlotId 架构 |
|------|---------|------------|
## 二-A、核心设计：ProjectionExtractPhysicalOp 的两种策略

### 关键洞察：同一个 `n.name`，不同上下文产生不同表达式类型

`ProjectionExtractPhysicalOp` 根据下游需求为每个变量选择两种物理策略之一：

| 策略 | 触发条件 | 物理行为 | 列数变化 | 最终表达式类型 |
|------|---------|---------|---------|-------------|
| **ConstructVertex/Edge** | `need_whole_vertex/edge = true` | 就地替换源列为完整对象 | **不变** | `BoundPropertyRef`（运行时从 VertexValue 解析属性） |
| **LoadVertexProp** | `vertex_props` 非空，且无 `need_whole_vertex` | **追加**扁平属性列 | **增加** | `BoundColumnRef`（指向追加的扁平列） |

### `need_whole_vertex` 何时为 true？

`collectPlanRequirements` 中，以下情况触发 `need_whole_vertex`：

1. **`RETURN n`**（Project 内部）→ `in_schema_changing_op=true` → `need_whole_vertex`
2. **`SET n.x` / `REMOVE n.x`** → 需要完整对象来修改属性
3. **`BoundDynamicPropertyRef`**（无 label 约束，如 `MATCH (n) RETURN n.name`）→ 运行时按名字查找属性
4. **`type(r)`, `labels(n)`, `startNode(r)`, `endNode(r)`** 等函数 → 需要完整实体

### 分叉点示例

```cypher
-- 场景A：RETURN n 触发 need_whole_vertex → ConstructVertex，无反列
MATCH (n) RETURN n, n.name

-- 场景B：Filter 触发 vertex_props，无 RETURN n →
--        LoadVertexProp，有反列 n.name:STRING
MATCH (n:Person) WHERE n.name = 'Alice' RETURN 1
```

**关键结论**：`RETURN n` 是否存在，决定了走 ConstructVertex（不偏移）还是 LoadVertexProp（偏移）。列偏移 **只发生** 在 Filter/Sort 引用属性但 RETURN 不返回该变量的场景。

### covering principle

当同一变量同时触发 `vertex_props` 和 `need_whole_vertex` 时（如 `WHERE n.name='Alice' RETURN n`），`need_whole_vertex` **优先**——ConstructVertex 覆盖所有属性需求，跳过 LoadVertexProp。

| 表达式稳定性 | column_index 随列追加而变化 | SlotId 永远不变 |
| Join 列布局 | 需手动计算 offset（`left_join_cols`） | 直接拼接 Layout |
| WITH 别名 | `project_resets` map 追踪 | 新 SlotId，永不冲突 |
| 代码量 | column_rewrite ~1200 行 | Layout + Compiler ~200 行 |
| 运行时开销 | 无（column_index 直接可用） | 无（init 时一次性翻译） |

### 2.2 对 eugraph 的适配：为何用 `uint32_t SlotId` 而非 DuckDB 的复合结构

DuckDB 使用 `{table_index, column_index}` 是因为关系型查询涉及多表 Join，每个表有独立的列集合。`table_index` 定位"哪个算子/表"，`column_index` 定位"表内第几列"。

eugraph 的图查询以**变量**（`a`、`r`、`b`）和**属性**（`a.name`、`r.since`）为核心，变量跨越多层算子（Scan → Expand → Filter → RETURN），不适合绑定到某个特定算子。

**扁平全局 `uint32_t SlotId` 更自然**：

```
Slot 1  → 变量 a (VertexRef)
Slot 2  → 属性 a.name (String)    ← 由 ProjectionExtract 追加
Slot 3  → 属性 a.age (Int64)      ← 同上
Slot 4  → 边 r (EdgeKey)
Slot 5  → 属性 r.since (Int64)
Slot 6  → 变量 b (VertexRef)
Slot 7  → 属性 b.name (String)
```

每个变量和每个追加的属性列都有**全局唯一的 SlotId**，由 Binder 统一分配，此后永不改变。

## 三、核心类型设计

### 3.1 SlotId

```cpp
using SlotId = uint32_t;
constexpr SlotId INVALID_SLOT_ID = 0;
```

全局唯一的逻辑列标识符。Binder 分配，表达式持有，物理算子通过 Layout 翻译。

### 3.2 TupleSlotLayout

每个物理算子维护的映射表：**SlotId → 物理列下标**。

```cpp
class TupleSlotLayout {
public:
    TupleSlotLayout& append(SlotId id);          // 尾部追加一列
    int getColumnIndex(SlotId id) const;         // SlotId → 物理下标（首次命中）
    size_t size() const;                          // 物理列数
    void merge(const TupleSlotLayout& other);     // 拼接另一个 Layout（Join 用）

    /// 根据 Project/Aggregate 的输出表达式列表，构建全新 Layout。
    /// 原 Layout 中不可见的旧 Slot 自然消失（列裁剪）。
    /// WITH b.name AS name, 100 AS score → output_slots = [Slot_bname, Slot_100]
    static TupleSlotLayout project(const std::vector<SlotId>& output_slots);
};
```

**`project()` vs `replace()`**：

- `replace(old, new)`：WITH 简单别名 `WITH a AS b`，只是重命名，列数不变
- `project(slots)`：WITH 裁剪/重排 `WITH b.name AS name, 100 AS score`，输出列**全部由 slot 列表决定**，原输入列中不在列表里的自动消失

ProjectPhysicalOp（对应 WITH/RETURN）在 init 时调用 `project()` 构建输出 Layout：遍历每个 item 的表达式，提取其引用的 SlotId（或为常量分配新 Slot），组成 `output_slots`，然后 `TupleSlotLayout::project(output_slots)`。

### 3.3 ExpressionCompiler

在物理算子 `init()` 阶段，遍历表达式树，用 `TupleSlotLayout` 把所有 `BoundColumnRef.slot_id` 翻译为 `column_index`，并缓存。

```cpp
class ExpressionCompiler {
    const TupleSlotLayout& layout_;
public:
    void compile(BoundExpression& expr);  // 递归遍历，翻译 slot_id → column_index
};
```

编译后，`BoundColumnRef.column_index` 被设置为正确的物理下标。运行时 Hot Loop 直接使用 `column_index`，没有任何 SlotId 查找开销。

### 3.4 BoundColumnRef 改动

```cpp
struct BoundColumnRef {
    uint32_t column_index = 0;  // 物理下标（init 时由 ExpressionCompiler 填充）
    uint32_t slot_id = 0;       // 逻辑标识（Binder 分配，不可变）
    BoundType type;
    std::string name;
};
```

过渡期两字段共存。最终 `column_index` 可以退化为仅在物理算子使用，逻辑层只用 `slot_id`。

## 四、完整执行流程

以如下 Cypher 为例：

```cypher
MATCH (a:Person) WHERE a.name = 'Alice'
OPTIONAL MATCH (a)-[:KNOWS]->(b:Person)
RETURN a.age, b.name
```

### 4.1 第一阶段：Binder 分配 SlotId

Binder 扫描 AST，为每个变量和需要的属性分配全局唯一 SlotId：

```
Slot 1  → a (VertexRef)           MATCH (a:Person)
Slot 2  → a.name (String)         WHERE a.name = 'Alice' 触发
Slot 3  → a.age (Int64)           RETURN a.age 触发
Slot 4  → b (VertexRef)           OPTIONAL MATCH (b:Person)
Slot 5  → b.name (String)         RETURN b.name 触发
```

所有表达式只记录 SlotId：

- Filter 条件：`BoundColumnRef(slot_id=2) == 'Alice'`
- RETURN 表达式：`[BoundColumnRef(slot_id=3), BoundColumnRef(slot_id=5)]`

**注意**：SlotId 的分配发生在需求收集之后（或同步进行），Binder 知道所有需要的属性列。

### 4.2 第二阶段：物理规划与 Layout 传递

#### 左子树

```
LabelScan(a) → 物理列 [a] → Layout [Slot 1]
     ↓ dispatchProjectionExtract
ProjectionExtract(a.name, a.age) → 物理列 [a, a.name, a.age]
     → Layout [Slot 1, Slot 2, Slot 3]
     ↓
Filter(a.name='Alice') → 物理列不变 → Layout 不变
     → Layout [Slot 1, Slot 2, Slot 3]
```

Filter 编译表达式 `BoundColumnRef(slot_id=2) == 'Alice'`：
- `input_layout.getColumnIndex(Slot 2)` → 返回 **1**（物理下标）
- 编译后的运行时表达式：`chunk.column(1) == 'Alice'`

#### 右子树

```
CorrelatedSource(a) → 物理列 [a] → Layout [Slot 1]
     ↓
Expand(a-[KNOWS]->b) → 物理列 [a, edge, b]
     → Layout [Slot 1, Slot 6, Slot 4]     ← edge 有 Binder 变量，自动获得 Slot 6
     ↓ dispatchProjectionExtract
ProjectionExtract(b.name) → 物理列 [a, edge, b, b.name]
     → Layout [Slot 1, Slot 6, Slot 4, Slot 5]
```

#### LeftJoin 拼接

```
LeftJoinPhysicalOp
  左输入 Layout: [Slot 1, Slot 2, Slot 3]                              (3 物理列)
  右输入 Layout: [Slot 1, Slot 6, Slot 4, Slot 5]                      (4 物理列)
  输出 Layout: [Slot 1, Slot 2, Slot 3, Slot 1, Slot 6, Slot 4, Slot 5]  (7 列)
```

**关键**：左树加了 `a.name` 和 `a.age`（Slot 2, Slot 3）后变成 3 列。右树表达式持有 Slot 5（b.name）。LeftJoin 只是把左右 Layout 拼起来，**不需要做任何 offset 计算**。`b.name` 在合并 Layout 中的物理位置 = getColumnIndex(Slot 5) = **6**（自动算出来的）。

对比旧架构：binder 以为左树只有 1 列、右树偏移 1，b.name 的 column_index = 1 + 右局部下标 = 错误值。需要 `column_rewrite` 手动修复。

### 4.3 第三阶段：算子初始化（编译期擦除）

RETURN 算子收到 LeftJoin 的输出 Layout：

```
[Slot 1, Slot 2, Slot 3, Slot 1, ..., Slot 4, Slot 5]
```

RETURN 的表达式：`BoundColumnRef(slot_id=3)`（a.age）、`BoundColumnRef(slot_id=5)`（b.name）

ExpressionCompiler 翻译：
```
a.age:  getColumnIndex(Slot 3) → 2  → 固化 column_index = 2
b.name: getColumnIndex(Slot 5) → 6  → 固化 column_index = 6
```

### 4.4 第四阶段：运行时 Hot Loop

```cpp
void ReturnPhysicalOp::execute(DataChunk& chunk) {
    auto& age_col  = chunk.columns[2];   // 编译期固化：a.age
    auto& name_col = chunk.columns[6];   // 编译期固化：b.name
    for (size_t i = 0; i < chunk.count; ++i) {
        emit(age_col.getValue(i), name_col.getValue(i));
    }
}
```

**零元数据查找开销**。纯粹的连续内存数组下标访问。

## 五、关键设计决策

### 5.1 ProjectionExtract "只追加不裁剪"

**决策**：`ProjectionExtractPhysicalOp` 只负责在物理 Chunk 尾部**追加**新列（零拷贝引用），**不删除**任何上游列。

**理由**：
- **实现简单**：不需要分析下游是否需要上游列（"幽灵依赖"分析在 Cypher 图查询中极其复杂，如 `count(a)` 需要 a 的 VertexRef 存活到聚合后）
- **零拷贝性能**：Chunk 间传递列只移动智能指针（`shared_ptr<ColumnVector>`），没有数据拷贝
- **职责分离**：属性提取只管"加列"，列裁剪留给专门的 ProjectionOp 或优化器 pass 处理
- **避免 Index Collapse**：删除列会导致后续列下标前移，在没有 SlotId 的老架构下是灾难

**后续优化**：待 SlotId 稳定后，在优化器层做列裁剪（Column Pruning），通过 `TupleSlotLayout` 在编译期"无视"不需要的列，运行时在 Pipeline 末尾由 ProjectionOp 统一做物理降维。

### 5.2 SlotId 在 Binder 层分配

SlotId 在 Binder 编译期分配，不在物理规划期分配。原因：

- Binder 知道完整的变量生命周期（哪些变量依赖哪些属性）
- `collectPlanRequirements` 的结果（需要哪些属性列）在 Binder 完成绑定后、物理规划前就已确定
- 物理规划只是"执行"这些需求——追加列、构建 Layout

**定理：物理 Chunk 中的每一列都必须有 SlotId。Layout 数组长度必须等于物理 DataChunk 列数，且下标一一对应。**

当前架构中，Binder 为所有变量（包括 Expand 的匿名边 `__anon_edge_0`、匿名节点）调用 `nextColumnIndex()` 分配 column_index，映射到 SlotId 后自然覆盖所有物理列。Expand 输出的 `[a, edge_key, b]` 三列分别对应 Binder 的三个变量，每个都有 column_index。

**这条定理在当前架构下是自动满足的**——Binder 的 column_index 分配已经覆盖了所有物理列。这里把它显式声明为定理，是为了防止未来引入新的"没有 Binder 变量的物理列"场景（如某种临时 Join key 列）时破坏 Layout 对齐。

在 SlotId 架构下，`LoadVertexProp` 追加的扁平属性列会被 `ConstructVertex`（就地替换，不增列）取代，因此不再有"Binder 不知情的追加列"。Layout 长度与物理 Chunk 长度天然一致。

### 5.3 过渡方案：slot_id 与 column_index 共存

在过渡期，`BoundColumnRef` 同时持有 `slot_id` 和 `column_index`：

```
Binder 阶段：设置 slot_id（全局唯一），column_index 可以是 binder 旧列号或 0
Planner 阶段：不修改表达式（与旧架构的关键区别！）
Init 阶段：ExpressionCompiler 用 TupleSlotLayout 翻译 slot_id → column_index
Runtime：只读 column_index（与旧架构兼容，无需改 evaluator）
```

## 六、解决当前剩余 Bug

### 6.1 Match7 [7]：LeftJoin 左侧扁平列偏移

**场景**：
```cypher
MATCH (a:Person {name:'Alice'})
OPTIONAL MATCH (a)-[:KNOWS]->()-[:KNOWS]->(foo)
RETURN foo
```

**旧架构**：Filter `a.name='Alice'` 导致左侧 `dispatchProjectionExtract` 追加 `LoadVertexProp(a.name)` 列。左物理列 2，binder 左列 1。`foo` 的 binder column_index = 1 + 4 = 5，但实际物理位置是 6。RETURN 读错列。

**SlotId 架构**：`foo` 持有 Slot_foo。LeftJoin 合并左右 Layout 后，`getColumnIndex(Slot_foo)` 自动返回正确物理位置。**无需任何手动 offset 计算**。

### 6.2 With7 [1]：BinaryJoin 变量覆盖

**场景**：
```cypher
MATCH (a:A)-[r:REL]->(b:B)
WITH a AS b, b AS tmp, r AS r
WITH b AS a, r LIMIT 1
MATCH (a)-[r]->(b)
RETURN a, r, b
```

**旧架构**：WITH 链产生别名重命名。第三个 MATCH 创建 BinaryJoin，右子树 `assignVar(a)`/`assignVar(r)` 覆盖左子树的 `base_col`。RETURN 读到右子树的 a、r（全扫顶点和新边），而非 WITH 保留的 a、r。

**SlotId 架构**：WITH `a AS b` → 新 Slot 4（b）。第二个 WITH `b AS a` → 新 Slot 5（a）。第三个 MATCH 的 `(a)` → 新 Slot 6（Scan 产生的 a），`[r]` → 新 Slot 7。**每个别名分配独立的 SlotId，永不存在覆盖问题**。RETURN 持有的 Slot 5（WITH 输出的 a）和 Slot 1（原始 r）在 Layout 中自然定位。

### 6.3 With7 [2]：WITH 输出变量被后续 WITH 作用域重置后 SlotId 丢失

**场景**：
```cypher
MATCH (david {name: 'David'})--(otherPerson)-->()
WITH otherPerson, count(*) AS foaf WHERE foaf > 1
WITH otherPerson
WHERE otherPerson.name <> 'NotOther'
RETURN count(*)
```

**根因**：Binder 在第一个 WITH 内为 `foaf` 分配 SlotId 6，WHERE `foaf > 1` 的 BoundColumnRef.slot_id 也为 6，二者一致。但第二个 `WITH otherPerson` 的作用域重置（`bind_return.cpp` 中 `auto old_symbols = std::move(ctx_.symbols)` 后只回填 `with_outputs`）把 `foaf` 从 `ctx_.symbols` 丢弃。`query_executor.cpp` 只从 `binder.ctx().symbols`（最终作用域）填充 `var_slots`，因此 `var_slots["foaf"]` 缺失；物理规划器的 `allocateAllSlots` 接着为 `foaf` 分配新 Slot 7（与 Binder 的 6 冲突）。Filter 的 layout 拿到 Slot 7，但 predicate 引用 Slot 6——`ExpressionCompiler.getColumnIndex(6)` 返回 -1，`column_index` 保留 Binder 旧值，读错列。

**修复**：在 `BindContext` 增加 `std::unordered_map<std::string, SlotId> all_symbols` 永久记录，Binder 每次为新名字分配 slot 时通过 `allocateNamedSlot` 同步写入。`query_executor.cpp` 在填充 `var_slots` 时迭代 `all_symbols`，确保被作用域重置丢弃但仍被 bound 树中算子（Aggregate `output_names`、Filter `predicate`）引用的名字保留其原始 SlotId。物理规划器随后在 `var_slots` 中找到 `foaf → 6`，不再分配冲突的新 slot，layout 与 predicate 一致。

## 七、实施路线图

### Phase 1：核心类型（已完成 ✅）
- [x] `SlotId`、`TupleSlotLayout`、`ExpressionCompiler` 类型定义
- [x] `BoundColumnRef.slot_id` 字段
- [x] `PhysicalOperator.slot_layout` 成员

### Phase 2：Layout 在 Planner 中传播（已完成 ✅）
- [x] `PlanOperatorResult.slot_layout`
- [x] `extractChildResult` / `finalizePlanResult` 传播 layout
- [x] `makeSlotLayout()` 辅助函数

### Phase 3：Binder 分配 SlotId（✅ 已完成，实现路径不同）
- [x] Binder 中 `SlotAllocator` 全局分配（`BindContext::slot_allocator`）
- [x] `makeColumnInfo` / `allocateNamedSlot` 分配 SlotId
- [x] Planner 通过 `AllocateSlotsInOp` + 预填充 `all_symbols` 为所有变量补充 SlotId
- [x] 内部 SlotId 使用 `kInternalSlotFlag` 分区（`buildExtractionInfo` 分配 `nextInternal()`）

### Phase 4：物理算子 compileExpressions 集成（✅ 已完成）
- [x] 所有物理算子实现 `compileExpressions(child_layout)`
- [x] `compileOperatorTree` 后序（bottom-up）遍历
- [x] `ExpressionCompiler` 解析 `BoundColumnRef.slot_id → column_index`

### Phase 5：column_rewrite 重构为 DPL 管线（✅ 已完成，方向不同）

原计划是"移除 column_rewrite"。实际选择了更彻底的方案：将 `column_rewrite.cpp` 重构为六阶段 Demand-Pull-Lowering 管线（参见 [projection-extract-design.md](projection-extract-design.md)），包含分配、需求收集、PEPlan 构建、别名透传、表达式 rewrite。旧 `updateExtractionBaseCols` / `project_resets` / `left_join_cols` 等机制已移除。

### Phase 6：验证与清理（✅ 已完成）
- [x] 437 单元测试通过
- [x] With7 / Match7 TCK 回归修复
- [x] `column_index` 保留为编译后缓存（ExpressionCompiler 写入）

## 八、与旧架构的对照表

| 概念 | 旧架构 (`column_rewrite`) | 新架构 (`SlotId + Layout`) |
|------|--------------------------|---------------------------|
| 列引用 | `BoundColumnRef.column_index`（物理数组下标） | `BoundColumnRef.slot_id`（全局唯一 ID） + `column_index`（编译后缓存） |
| 列数跟踪 | `updateBaseCols` 遍历 bound tree，模拟 col_count | `TupleSlotLayout.size()` 直接从物理输出 schema 获取 |
| Join 列合并 | `left_join_cols` map + 手动减 offset | `layout.merge(other_layout)` |
| WITH 重命名 | `project_resets[op][var]` 存储输入位置 | 分配新 SlotId，Layout.replace(old, new) |
| 表达式重写 | `rewriteOp` 递归遍历，修改 column_index | `ExpressionCompiler` 在 init() 时一次性翻译 |
| 属性列追加后 | column_index 错位，需 `rewriteOp` 修正 | SlotId 不变，`getColumnIndex()` 自动正确 |
| 代码位置 | `column_rewrite.cpp` (~1200 lines) | `slot_layout.hpp` (~80 lines) + `expression_compiler.hpp` (~80 lines) + planner 中少量集成 |
