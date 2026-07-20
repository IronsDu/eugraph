# 物理算子职责审计

> **文档定位**：记录拓扑/语义类型分离的设计推理，审计各物理算子的列传递浪费，以及仍未落地的优化方向（列裁剪、批量 I/O、CBO）。

当前架构已采用统一的 **`ProjectionExtractPhysicalOp`**（7 种 `ColumnSpec`）实现按需属性加载：

- `LoadVertexProp` / `LoadEdgeProp` / `LoadVertexLabels` / `LoadEdgeType` — 追加扁平列
- `ConstructVertex` / `ConstructEdge` — 原地构造 VertexValue / EdgeValue
- `Passthrough` — 直通列

`Scan` / `Expand` / `VarLenExpand` 输出轻量拓扑类型（`VertexRef` / `EdgeKey` / `PathTopology`），labels 与属性由下游 `ProjectionExtractPhysicalOp` 按 `PlanRequirements` 统一加载。拓扑/语义类型分离的核心收益是：无需完整顶点的查询不再构造完整顶点。

> **详见**：
> - [projection-extract-design.md](projection-extract-design.md) — `ProjectionExtractPhysicalOp` 具体设计、7 种 ColumnSpec、PlanRequirements 收集规则、column_rewrite
> - [cascades-optimizer.md](../optimizer/cascades-optimizer.md) — CBO standalone 模式
> - 下文"共性架构问题"、"第二轮审计"、"剩余未解决问题"等章节中关于**列裁剪、批量 I/O、Merge `evaluateExpr`** 的诊断仍然有效，是后续优化方向。

## 总览

当前架构中，物理算子"自给自足"——扫描全量数据、无条件加载标签和属性、全列透传。根因是**规划阶段没有把下游需求传递给上游算子**。每个算子不知道下游实际需要什么，只能做最坏假设。

DuckDB 的列裁剪（Projection Pushdown）本质上是一种"需求反向传播"：从 plan root 向下遍历，每个算子声明需要哪些列，最底层的 Scan 据此只读所需列。我们需要在此基础上扩展——不只是"需要哪个列"，还要描述**列内字段级**的需求（是否需要 labels、需要哪些 label 的哪些属性）。

---

> ## 关键设计决策：拓扑类型与语义类型分离

图查询中，数据的消费存在两个截然不同的阶段：

1. **拓扑阶段**（scan、expand、filter on id）：只需要 `uint64` 裸 ID，不需要 labels、properties、type name
2. **语义阶段**（property/label 访问、RETURN 序列化）：需要完整的 labels、properties、type name

因此核心设计是：**算子输出类型由其职责决定，拓扑算子不产出语义类型**。

### 类型层次

```
拓扑阶段用（✅ 已实现）：
  - VertexRef { VertexId id }        — 顶点引用，不含任何语义数据
  - EdgeKey { EdgeId id; VertexId src_id, dst_id; EdgeLabelId label_id; uint32_t seq; }
                                     — 边 ID + 拓扑信息，不含 properties/type name
  - PathTopology { vector<VertexId> vertex_ids; vector<EdgeId> edge_ids;
                   vector<EdgeLabelId> edge_label_ids; vector<uint32_t> seqs; }
                                     — 路径骨架（vid/eid/label_id/seq 并行数组）

语义阶段用（已有，未变）：
  - VertexValue { id; labels; properties; deleted }
  - EdgeValue { id; src_id; dst_id; label_id; seq; optional<Properties> properties; deleted }
  - PathValue { vector<ValueStorage> elements }  — 完整路径（element 可含 VertexValue/EdgeValue）
```

对应的 `BoundTypeKind` 枚举已扩展：
- `VERTEX_REF` — 拓扑阶段顶点引用
- `EDGE_KEY` — 拓扑阶段边引用
- `PATH_TOPOLOGY` — 拓扑阶段路径骨架
- `VERTEX` / `EDGE` / `PATH` — 语义阶段（已有，未变）

`ColumnBuffer` 已新增 `vertex_ref_data`、`edge_key_data`、`path_topology_data` 三个类型化存储向量。

### 提升点（Enrich Point）

提升算子负责 `拓扑 → 语义` 的转换。根据语义消费模式的不同，分为两类：

**PropertyExtract（属性批量抽取）—— 90% 的查询走此路径**

适用于用户只需要局部属性、不需要整个点/边/路径对象的场景（如 `RETURN n.name, n.age`）。

- 输入：Var 的拓扑引用（VertexRef / EdgeKey / PathTopology）+ 去重后的属性需求集合
- Execute：批量查存储，直接追加**扁平列**（Vector\<String\>、Vector\<Int64\>、...）到 DataChunk
- 输出：**不包含任何重对象**（无 VertexValue map、无 EdgeValue optional、无 PathValue variant），纯列式数据
- 下游算子（Filter、Project、Sort）以 O(1) 数组下标读取，SIMD 矢量化

**Enricher（点/边/路径全量物化）—— 仅在 RETURN n / RETURN e / RETURN p 时使用**

适用场景：用户明确要求返回整个对象（`RETURN n`、`RETURN r`、`RETURN p`），或同时需要整个对象和局部属性（`RETURN n, n.name, n.age`）。

- 输入：Var 的拓扑引用
- Execute：一次性批量读取所有属性 → 构造 `Vector<VertexValue>` / `EdgeValue` / `PathValue>`
- 输出：**将拓扑列原地替换为语义重对象列**
- **覆盖原则**：如果优化器发现变量同时有 `need_entire_*` 和具体的 `need_props`，只插 Enricher，局部属性需求由下游 Project 从 Enricher 产出的重对象中零 I/O 解引用

**两个算子的选择由需求收集阶段的 `need_entire_*` 标志决定**，而非两个算子同时存在。优化器在 Phase 1 收集时：如果检测到 `RETURN n`（需要整个点），标记 `need_entire_vertex = true`，覆盖并清空该变量的 `need_props`。后续只插入 VertexEnricher，不插入 PropertyExtract——避免"构造 Enricher 时读了 name，又单独去存储读一次 name"的重复 I/O。

这些算子位于管道的**最晚可能位置**——紧邻第一个需要语义信息的算子之前。

**Enricher 的粒度是 per-variable 的**——每个变量的 Enricher/PropertyExtract 独立放置于该变量首次被语义引用的位置，而非一个合并的大算子一次性物化所有变量。planner 从下游树为每个变量分别收集需求，不同变量的 Enricher 与 Filter 等语义算子交织排列，使得先执行的 Filter 可以削减后续 Enricher 需要处理的行数。

```
拓扑阶段                          语义阶段
  │                                  │
  AllNodeScan → VertexRef            │
  Expand      → VertexRef, EdgeKey   │
  │                                  │
  └───── PropertyExtract / Enricher ─┘
                       │
    ── 90% 场景（只需局部属性）：PropertyExtract ──
    VertexPropertyExtract(VertexRef → 追加扁平 Vector<String>, Vector<Int64>)
    EdgePropertyExtract(EdgeKey → 追加扁平 Vector<String>, Vector<Int64>)
    PathPropertyExtract(PathTopology → 追加扁平 Vector<...>)
                       │
    ── 10% 场景（需要完整对象）：Enricher ──
    VertexEnricher(VertexRef → 原地替换为 Vector<VertexValue>)
    EdgeEnricher(EdgeKey → 原地替换为 Vector<EdgeValue>)
    PathEnricher(PathTopology → 原地替换为 Vector<PathValue>)
                       │
  ─────────────────────┼──────────────────────
                       │
    Filter(WHERE n.age > 30)      ← 从扁平列或 VertexValue 读取属性
    Project(RETURN n.name)        ← 从扁平列或 VertexValue 投影
    Sort(ORDER BY n.name)         ← 从扁平列或 VertexValue 排序
                       │
                  RETURN 序列化
```

PropertyExtract 与 Enricher 的核心差异：

|              | PropertyExtract                | Enricher                        |
|--------------|-------------------------------|---------------------------------|
| 触发条件    | 只需要局部属性                    | RETURN n / e / p（需要完整对象）  |
| 输出类型    | 追加的扁平列式 Vector (String, Int64, ...) | 原地替换的 Vector<VertexValue> 等 |
| 内存特点    | 零堆分配，纯连续内存，SIMD 友好        | VertexValue 含 map 堆分配          |
| 列数量变化  | 增加（原始列 + N 个新属性列）         | 不变（替换）                       |
| 覆盖原则    | 无                              | 吸收该变量的所有 need_props          |

好处：
- **类型安全**：拓扑算子无法意外访问 labels/properties（字段不存在）
- **内存**：`uint64` = 8 字节 vs `VertexValue` = 24+ 字节 + 堆分配；`PathTopology` 约 100 字节/5-hop vs `PathValue` 上 K 字节
- **无分支**：语义类型总是完整的——不存在"labels 可能为空"的半成品状态

---

## 物理优化架构：三阶段需求驱动规划

> **职责边界**：此逻辑 100% 运行在 `PhysicalPlanner`（物理优化阶段），不是 Binder（逻辑计划生成）。
> Binder 的职责是 AST → `BoundLogicalPlan`（纯语义映射，不涉及物理算子选择）。
> 需求反向传播、Enricher 注入、列裁剪等属于 **物理优化与算子改写**，是 PhysicalPlanner 的职责。
> `LogicalOptimizer` 在此之前完成 Filter 下推等逻辑等价变换，但不接触物理算子。
>
> 完整流水线：
> ```
> Binder → BoundLogicalPlan → LogicalOptimizer → ColumnResolver → PhysicalPlanner（本架构） → PhysicalOperator 树 → Executor
> ```

将"从 Root 向下逆向收集需求"落地为可实现的 PhysicalPlanner 架构，需要拆分为三个明确的阶段。

### 阶段 1：自底向上（Bottom-Up）构建初始纯拓扑计划

PhysicalPlanner 按常规方式，根据 `BoundLogicalPlan` 生成最基础的、**完全不包含任何 Enricher** 的纯拓扑执行计划链条。此时所有算子输出拓扑类型：

```
AllNodeScan(Person) → Expand(n→m) → Filter(id(n)=10086) → Project(id(m))
```

此时 DataChunk 中只有 VERTEX_REF、EDGE_KEY、PATH_TOPOLOGY。没有任何语义类型。

### 阶段 2：自顶向下（Top-Down）需求反向传播

从 Root 算子（通常为 Project）开始，调用 `collectRequirements(RequirementsContext& ctx)` 逆向 DFS 遍历算子树。各算子的行为：

- **Project(RETURN a.name, b.age)**：向 ctx 注入 `a → {need_props: [name]}` 和 `b → {need_props: [age]}`，将 ctx 传给孩子
- **Filter(WHERE a.gender = 'F')**：向 ctx 追加（Union）`a → {need_props: [gender]}`，将 ctx 传给孩子
- **Sort(ORDER BY n.name)**：向 ctx 注入 `n → {need_props: [name]}`，将 ctx 传给孩子
- **Expand(a → b)**：**关键拦截点**。b 的拓扑生命周期在此终结（再往下只有 a）。检查 ctx 中 b 的全部语义需求，标记为"应在 Expand 之上消费"，然后**从 ctx 中擦除 b 的记录**，只将 a 的需求继续往下传
- **Join / Union / CrossProduct（多子节点）**：**需求合并点**。当反向传播遇到多子节点算子时，ctx 需要分裂到各子分支。但各分支可能对**共享变量**（如菱形查询中的 a）收集到不同的需求。规则：
  1. 先向各子分支分别派发 ctx 的**副本**（`ctx.for_branch(i)`），各分支独立收集
  2. 各分支返回后，对每个共享变量执行 `ctx.merged[var] = Union(各分支对 var 的需求)`
  3. 合并后的全集需求继续向上（向 Root 方向）传播
  4. 这样保证共享变量在 Join 之下注入的 Enricher 包含**所有分支对该变量需求的全集**，不会因某一分支提前擦除而丢失
- **Scan(a)**：终点。a 的所有累积需求在此消费，生成 `VertexEnrich(a)` 插在 Scan 之上

### 阶段 3：算子树改写（Rewriting）—— Enricher 注入

利用 Visitor 模式逆向遍历物理算子树。当 Visitor 回溯（从叶子往根返回的 Pop 阶段）时，根据阶段 2 收集的需求就地改写：

- 在变量首次被语义引用的算子**下方**插入对应的 Enricher
- Enricher 按 per-variable 粒度独立注入，与 Filter 等语义算子交织排列
- 改写后检查：Enricher 之下不应再有该变量的语义需求传播

```
[阶段1]  Scan → Expand → Filter → Project    （纯拓扑）
[阶段2]  需求反向传播 + 就地截断
[阶段3]  Scan → VertexEnrich(a) → Expand → VertexEnrich(b) → Filter → Project
```

### 三个关键机制

#### 机制 1：需求就地截断（Demand Truncation）

当反向传播经过某个算子，导致变量在此算子下方不复存在时，**必须把该变量的需求从 ctx 中清除**，不再向下传播。

反面案例：如果没有就地截断，`MATCH (a)-[:knows]->(b) RETURN b.name` 中 b 的 `need_props: [name]` 会一路传到最底层的 Scan(a)，导致底层误以为 a 也需要加载 b 的属性。

正确做法：Expand(a→b) 收到上层对 b 的需求后，标记"在 Expand 上方插入 VertexEnrich(b)"，然后**擦除 ctx 中 b 的记录**，只把 a 的需求（若有）继续往下传给 Scan(a)。

```
规则：算子 O 产出了变量 v，则经过 O 反向传播时，
      ctx 中所有以 v 为 key 的需求被就地消费并擦除。
     （Scan 产出 n；Expand 产出 r 和 m；VarLenExpand 产出 p）
```

#### 机制 2：需求精准分割（Split Enricher）

必须防止 Planner 无脑生成一个包含巨大 map 的 Enricher。分割判断逻辑：

- **同一消费点合并**：`WHERE a.age > 20 AND a.city = 'BJ'` → 两个属性在同一个 Filter 中消费 → 插入**单个** `VertexEnrich(a, props=[age, city])`
- **跨算子拆分**：`WHERE a.age > 20 AND b.city = 'BJ'` 中间隔了 Expand → a 和 b 分属不同拓扑域 → 必须拆为两个独立 Enricher，分别在各自 Filter 之前
- **同一变量跨多级消费合并**：`WHERE n.age > 30 RETURN n.name ORDER BY n.name` → n 的需求在 Filter、Sort、Project 三级分别引用；逆向收集时做 Union（`{age} ∪ {name}`），统一在 Filter 之前插入**单个** `VertexEnrich(n, props=[age, name])`

分割的判定标准：**两个属性引用之间是否存在 Expand/VarLenExpand 阻隔变量的拓扑可见性**。存在则分割，不存在则合并。

#### 机制 3：编译期内联优化（Compile-time Inline）

在反向收集时，如果遇到 `WHERE id(a) = 999` 这种带常量 ID 的过滤：

1. Planner 直接转化为 `NodeIndexScan(id=999)`，跳过全表扫描
2. 由于 IndexScan 只返回一个确定顶点，a 的 `VertexEnrich` 可在规划期直接从元数据/缓存读取 a 的属性值，将运行时 I/O 降为 0
3. Enricher 对 CONSTANT 列的处理天然支持此优化——仅发起 1 次查询，规划期可更进一步完全消除

### 四个设计盲区与修正

上述三阶段 + 三机制是正确的基础框架，但在落地高性能 C++ Planner 时，存在四个如果不解决就会导致执行计划性能崩溃的盲区。

#### 盲区 1：多拓扑分支的重复物化（Duplicate Enrichment）

**致命场景**：
```cypher
MATCH (a)-[:knows]->(b), (a)-[:works_at]->(c)
WHERE a.age > 30 AND b.city = 'NY' AND c.name = 'Google'
RETURN a.name
```

**问题**：a 出现在两个拓扑分支（knows 分支和 works_at 分支）中。如果 Planner 死板地按"每个变量首次被语义引用时"逆向插入 Enricher，a 可能在 knows 分支被 Enrich 一次，又在 works_at 分支被 Enrich 一次。同一个顶点 a 的属性被重复加载和反序列化两次。

**修正 — 物化状态上下文（Materialization Context）**：
Planner 的反向传播不仅记录"需求"，还要记录 `is_variable_enriched[var_name]` 状态位。一旦变量 a 在某个子树分支中被标记为已 Enrich，当需求流传播到另一个分支时，跳过重复注入，改为通过 DataChunk 列引用直接复用。

```
规则：维护全局 enriched_vars: set<string>。
     每次决定为变量 v 注入 Enricher 前，先检查 enriched_vars.contains(v)。
     若已富集 → 跳过；若未富集 → 注入并标记。
     在 HashJoin/Union 等多输入算子处，共享的变量只需在 Join 之前的一个公共位置注入一次。
```

#### 盲区 2：变长路径最晚物化的内存爆炸

**致命场景**：
```cypher
MATCH p = (a)-[:knows*1..5]->(b)
WHERE a.age > 30
RETURN p
```

**问题**：按"最晚物化"原则，PathEnricher 放在 RETURN 之前的最后位置。但 1~5 跳展开可能爆出千万级 PathTopology 骨架。如果全部囤积在内存中直到最终 PathEnricher 才处理，内存瞬间撑爆。

**修正 — 基数阈值触发尽早物化（Cardinality-Gated Early Enrichment）**：
Planner 必须具备启发式规则：当变长路径或宽表 Join 的预估基数超过阈值时，将"最晚物化"降级为"阶段性局部物化"——边展开、边局部 Enrich 过滤、边释放拓扑骨架。

```
规则：预估中间基数 > CHUNK_SIZE (1024) × EARLY_ENRICH_MULTIPLIER (默认 10) 时：
      在 VarLenExpand 输出端立即插入轻量级 PathEnricher + Filter，
      将空间复杂度从 O(全量路径) 降到 O(Chunk缓冲区)。
```

注意：此规则是安全阀（safety valve）。在基数可控的查询中，最晚物化仍然是最优策略。

#### 盲区 3：多变量 Filter 的顺序未优化（选择率倒置）

**致命场景**：
```cypher
MATCH (a)-[:knows]->(b)
WHERE a.type = 'NormalUser' AND b.is_terrorist = true
```

**问题**：假设全图 100 万 NormalUser，但只有 5 个 is_terrorist。如果 Planner 顺着拓扑顺序生成 `Enrich(a) → Filter(a) → Expand → Enrich(b) → Filter(b)`，引擎必须先对 100 万个 a 发起 Enrich I/O。但正确的做法是从极低选择率的 b 侧优先驱动。

**修正 — 轻量级代价模型（Lightweight CBO）**：
在反向收集需求后，Planner 不能无脑就地插 Enricher。必须结合简单的选择率估算来排序 Enricher+Filter 的执行顺序。

```
规则：对每个 Filter 中的属性条件，估算选择率（selectivity）：
      - 等值条件（=）：默认 0.01（1%）
      - 范围条件（>, <）：默认 0.1（10%）
      - 存在性检查（IS NOT NULL）：默认 0.9（90%）
      - 如有统计信息（直方图），使用实际选择率

      优先执行选择率最低的 Enricher+Filter 对。
      在 TC-11 的交织场景中，Planner 按选择率升序排列各变量的 (Enricher, Filter) 对。
```

此 CBO 不需要完整的统计信息——仅用默认启发式值即可在绝大多数场景下避免最坏情况。

#### 盲区 4：属性依赖拓扑的隐藏依赖（Data-Dependency Deadlock）

**致命场景**：
```cypher
MATCH (a)-[:knows]->(b)
WHERE id(b) = a.target_neighbor_id
```

**问题**：Filter 中 `a.target_neighbor_id` 是 a 的语义属性，但 `id(b)` 依赖 Expand 产出 b。按"最晚物化"原则，a 的 Enricher 可能被放在 Expand 之后——但 Expand 还未发生，Filter 无法拿到 b 的 id。更隐蔽的是，a 的属性 `target_neighbor_id` 是 Filter 求值的必要条件，而该 Filter 又位于 Expand 之后。如果 Planner 不识别这种跨变量依赖，会生成无法求值的计划。

**修正 — 依赖边提升（Dependency Hoisting）**：
反向传播时必须识别 Filter 表达式中的"跨变量属性依赖拓扑"逻辑。

```
规则：扫描 Filter 表达式树，识别跨变量属性引用。
      若表达式 E 同时引用变量 a 的属性和变量 b 的拓扑信息：
      - 被依赖的变量 a 的 Enricher 必须强制提升（Hoist）到产出变量 b 的 Expand 算子之前
      - 即：Enricher(a) → Expand(a→b) → Filter(id(b) == a.target_neighbor_id)
      - 这是对"最晚位置"原则的唯一例外——数据依赖强制提前物化。

实现：在阶段 2 反向传播收集需求时，检查每个 Filter 的表达式树，
      若发现 BoundColumnRef(attr_of_x) 与 BoundColumnRef(topology_of_y) 共存，
      且 y 由当前算子之下的 Expand 产出，则将 x 的 Enricher 标记为 hoisted=true，
      在 Expand 之前注入。
```

#### 盲区 5：菱形查询分支需求分裂与合并（Requirement Splitting & Merging at Join）

**致命场景**：
```cypher
MATCH (a)-[:knows]->(b)-[:works_at]->(c),
      (a)-[:likes]->(d)-[:manages]->(c)
WHERE b.city = 'NY' AND d.level > 5
RETURN c.title
```

**问题**：反向传播到达 HashJoin(b, d → c)（两条路径的汇聚点）时，需求流一分为二：
- 分支 1（经过 b）：收集到对 c 的需求 `{title}`，b 的需求 `{city}`。a 无需求。
- 分支 2（经过 d）：收集到对 c 的需求 `{title}`，d 的需求 `{level}`。a 无需求。

如果没有合并逻辑，分支 1 先在 Scan(a) 上方插了一个空需求 Enricher 并擦除了 a 的记录。但分支 2 可能因为后续规则变化需要 a 的属性——此时分支 1 的提前擦除导致需求丢失。更隐蔽的是：两个分支对共享变量 c 的需求都是 `{title}`（一致），但如果有分支额外需要 `c.extra`，不一致的需求会导致注入位置二义性。

**修正 — Join 节点需求合并（Requirement Merge at Join）**：

```
规则：当反向传播遇到多子节点算子（Join/Union/CrossProduct）时：
  1. 为每个子分支创建 ctx 的副本（ctx.for_branch(i)），各分支独立收集
  2. 各分支返回后，对每个共享变量执行全分支需求的并集：
     ctx.merged[var] = Union_over_all_branches(branch_ctx[var])
  3. 合并后的全集需求沿树向上（向 Root 方向）继续传播
  4. 共享变量的 Enricher 在 Join 之下的公共祖先位置注入一次，包含全部分支需求的并集

示例（上述菱形查询）：
  - 分支 1 对 b 的需求：{city}；分支 2 对 d 的需求：{level}
  - 共享变量 a：两分支均无需求 → 合并后仍为空 → 不注入 Enricher(a)
  - 共享变量 c：两分支均有 {title} → 合并后 {title}
  - 变量 b, d：非共享变量，各自在所属分支内独立消费
```

### 需求描述模型

planner 从 Root 向下逆向收集需求，以变量名为 key：

```
VertexRequirement {
    need_entire_vertex: bool                       // RETURN n → 需要完整 VertexValue
    need_labels: bool                              // labels(n)、n::Label → 需要 labels 集合
    need_properties: map<LabelId, set<PropId>>     // n.name、n.age → 需要哪些 label 的哪些属性
}

EdgeRequirement {
    need_entire_edge: bool                         // RETURN e → 需要完整 EdgeValue
    need_type: bool                                // type(r) → 需要 type name
    need_properties: map<LabelId, set<PropId>>     // r.weight → 需要哪些属性
}

PathRequirement {
    need_entire_path: bool                         // RETURN p → 需要完整 PathValue
    // 按路径位置索引的需求。key = 路径中第 k 个元素的位置（0-indexed）。
    // vertex_positions: 哪些位置是顶点，取 VertexRequirement
    // edge_positions:   哪些位置是边，取 EdgeRequirement
    vertex_positions: map<size_t, VertexRequirement>
    edge_positions:   map<size_t, EdgeRequirement>
}

// planner 持有的全局需求表，key = 变量名
map<string, VertexRequirement> vertex_reqs;
map<string, EdgeRequirement> edge_reqs;
map<string, PathRequirement> path_reqs;
```

需求在 planner 阶段沿算子树**从 Root 向下传播**，经过变量产出算子时实施**就地截断**。planner 在决定插入 PropertyExtract 还是 Enricher 时，用变量名查表：

- `need_entire_vertex == true` → 插 `VertexEnricher`（全量物化），同时清空该变量的 `need_props`（覆盖原则防重复 I/O）
- `!need_entire_vertex && need_props 非空` → 插 `VertexPropertyExtract`（按需扁平抽取）
- `need_entire_edge == true` → 插 `EdgeEnricher`，清空 `need_props`
- `!need_entire_edge && need_props 非空` → 插 `EdgePropertyExtract`
- `need_entire_path == true` → 插 `PathEnricher`

---

## 各算子现状与理想职责

### 1. AllNodeScanPhysicalOp

**当前状态**：输出 `VertexRef`（拓扑类型）。流式扫描，不再构造 `VertexValue`。labels 与属性由下游 `ProjectionExtractPhysicalOp` 按 `PlanRequirements` 加载（`LoadVertexLabels` / `LoadVertexProp` / `ConstructVertex` spec）。

---

### 2. LabelScanPhysicalOp

**当前状态**：输出 `VertexRef`。同 AllNodeScan，labels/属性由下游 `ProjectionExtractPhysicalOp` 加载。

**待完成**：多 label 扫描优化。

---

### 3. ExpandPhysicalOp

**当前状态**：
- src 列：从 child 传递，保持拓扑或升级后状态
- edge 列：输出 `EdgeKey`（`{id, src_id, dst_id, label_id, seq}`），仅在 `edge_var_` 非空时
- dst 列：输出 `VertexRef`，仅在 `dst_var_` 非空时
- 语义升级由 `ProjectionExtractPhysicalOp` 完成：`LoadVertexLabels` / `LoadVertexProp` / `ConstructVertex`（dst）、`LoadEdgeProp` / `LoadEdgeType` / `ConstructEdge`（edge）
- 修复了 `edges[i].seq`（uint64_t）到 `EdgeKey::seq`（uint32_t）的窄化转换

**待完成**：主动 CONSTANT 提升（检测所有输出行共享同一源顶点时源端列改用 `Column::constant()`）。

---

### 4. VarLenExpandPhysicalOp

**当前状态**：
- dst 列：输出 `VertexRef`，labels/属性由下游 `ProjectionExtractPhysicalOp` 加载
- path 列：输出 `Path()` 类型。VarLenExpandPhysicalOp 内部为路径元素加载 labels（DFS 合法性检查 + path 构建），但不再为 dst 列重复加载。`wrapPathElementPropertyRead` → `PathElementPropertyReadPhysicalOp` 加载路径元素中的边属性和顶点标签。
- edge list 列（如有）：输出 `List<EdgeValue>`，边属性由边属性过滤逻辑加载
- src 列读取同时支持 `VertexRef` 和 `VertexValue`（兼容 child 可能已升级的场景）

**待完成**：PathTopology 输出（当前仍输出 PathValue）；路径中间顶点的 labels 按需加载（当前全量加载）。

---

### 5. ProjectionExtractPhysicalOp（✅ 已实现）

**概括**：融合点/边属性抽取 + Project 语义的统一算子。详见 [projection-extract-design.md](projection-extract-design.md)。

```
ColumnSpec 7 种 Kind：
  Passthrough        {source_col}              → 上游列直接透传
  LoadVertexProp     {source_col, lid, pid}    → 单点属性
  LoadEdgeProp       {source_col, elid, pid}   → 单边属性
  LoadVertexLabels   {source_col}              → List<String>（顶点 labels）
  LoadEdgeType       {source_col}              → String（边 type name）
  ConstructVertex    {source_col}              → VertexValue（labels + 全部属性）
  ConstructEdge      {source_col}              → EdgeValue（全部属性）
```

**当前状态**：
- 输出 schema 精确匹配下游需求：Passthrough → Vertex props → Edge props → Vertex labels → Edge type（顺序确保 `column_rewrite` 偏移计算正确）
- Per-row 缓存：行内多个 spec 引用同一 source_col 时共享 vid/eid（`resolveVertexId` / `resolveEdgeId`）
- 覆盖原则：`need_whole_vertex` / `need_whole_edge` 覆盖 `need_props`（同一变量不重复加载）
- 列去重：管线中多次 dispatch 时，已存在的 `var.prop_name` 列不重复 emit
- 5 处原 inline loader（Scan × 3 + IndexScan + Expand dst + VarLenExpand dst）全部替换为 `dispatchProjectionExtract`

**已收敛到此算子的设计稿提案**：
- 设计稿 `VertexPropertyExtractPhysicalOp`（追加扁平列）→ `LoadVertexProp` / `LoadVertexLabels` spec
- 设计稿 `VertexEnricherPhysicalOp`（原地替换为 VertexValue）→ `ConstructVertex` spec
- 设计稿 `EdgePropertyExtractPhysicalOp` → `LoadEdgeProp` / `LoadEdgeType` spec
- 设计稿 `EdgeEnricherPhysicalOp` → `ConstructEdge` spec

---

### 6. PathElementPropertyReadPhysicalOp（保留）

**当前状态**：仅用于 PathBuild 路径，将 `PathTopology` 升级为 `PathValue`。当前全量加载路径元素的属性和 labels。

**待完成**：按需加载（设计稿中 `PathPropertyExtractPhysicalOp` / `PathEnricherPhysicalOp` 的目标，未实现）。


**理想输出**：将 `PathTopology` 列原地替换为 `PathValue` 列。

**关键设计**：
- 只在 planner 检查到 `path_reqs[var].need_entire_path == true` 时才被插入
- 展平去重批量 I/O（与 PropertyExtract 相同），区别是构造完整 PathValue 而非扁平列
- 替换当前 `PathElementPropertyReadPhysicalOp`

---

### 8. EdgeIndexScanPhysicalOp

**现状**：通过边属性索引扫描。捕获到匹配边后，**无条件**获取 src 和 dst 两端的 vertex labels。

**问题**：
- 每行两次 `co_await getVertexLabels`，与查询是否需要 labels 完全无关

**理想输出类型**：
- `col[src_var]` = `uint64`
- `col[dst_var]` = `uint64`
- `col[edge_var]` = `EdgeKey` 或 `EdgeValue`（仅当索引扫描本身的属性过滤需要回传属性时带 properties）

**理想职责**：只做索引扫描和边属性过滤。vertex labels 由 `VertexEnrich` 按需加载；edge type name 和额外属性由 `EdgeEnrich` 按需加载。

**关键改动**：
- 删除 src/dst 的 `getVertexLabels` 调用
- src/dst 输出类型从 `VertexValue` 改为 `uint64`
- edge 输出类型从完整 `EdgeValue` 改为 `EdgeKey`（或仅含过滤所需属性的 EdgeValue）

---

### 9. SetPhysicalOp / RemovePhysicalOp / DeletePhysicalOp

**现状**：操作顶点/边后标记 `deleted` 字段，尝试通过 DICTIONARY 列的 buffer 回写。

**理想输出类型**：不改动 schema，只修改目标列

**理想职责**：做 mutation 操作，写回 storage 并同步内存列。不加载额外的 labels/properties。无需变更。

---

### 10. FilterPhysicalOp / ProjectPhysicalOp / SortPhysicalOp / LimitPhysicalOp / DistinctPhysicalOp

**现状**：过滤/投影/排序/分页/去重。

**理想输出类型**：同现状

**理想职责**：保持现状。列裁剪的责任在 planner——应该在 plan 构建后插入列裁剪算子，而不是要求这些算子自己判断。无需变更。

---

### 11. CrossProductPhysicalOp / LeftJoinPhysicalOp / UnionPhysicalOp

**现状**：多输入源合并。合并时保留所有输入列。

**理想职责**：由 planner 在合并前对左右两侧分别插入列裁剪算子，减少进入 CrossProduct 的列数。算子自身无需变更。

---

### 12. MergePhysicalOp

**现状**：MERGE 算子在输出时调用 `getVertexProperties(vid, lid)` 加载全量属性。

**理想输出类型**：顶点/边的 properties 仅加载 RETURN/ON MATCH/ON CREATE 中引用的属性

**理想职责**：接收 planner 传递的需求信息，按需加载。输出使用语义类型。

---

## 计划示例

### `MATCH (n:Person)-[r:KNOWS]->(m:Person) RETURN m.name`

binder 从 `m:Person` 解析出 `(Person, name)` 的 prop_id，直接走强模式。不需要 labels。只需要局部属性 → 走 PropertyExtract。

```
AllNodeScan(Person)       → col[0] = VertexRef
Expand(n, r, m)           → col[0] = VertexRef, col[1] = EdgeKey, col[2] = VertexRef
VertexPropertyExtract(m,  → 追加 col[3] = StringVector（扁平 name 列）
  need_props={Person: [name]})
Project(RETURN m.name)    → col[0] = string
```

全程无 VertexValue 构造，无 map 堆分配。

### `MATCH (n:Person)-[r:KNOWS]->(m:Person) RETURN m, m.name, m.age`

`RETURN m` 明确要求完整点对象 → 走 Enricher。局部属性被覆盖——不再单独抽取。

```
AllNodeScan(Person)       → col[0] = VertexRef
Expand(n, r, m)           → col[0] = VertexRef, col[1] = EdgeKey, col[2] = VertexRef
VertexEnricher(m)         → col[2] 原地替换为 Vector<VertexValue>
Project                   → 表达式引擎从 col[2] 的 VertexValue 中零 I/O 解引用 name/age
```

存储只读一次构造 VertexValue，Project 纯内存操作。无重复 I/O。

### `MATCH (n:Person)-[r:KNOWS]->(m:Person) RETURN m.name, labels(m), m.age`

binder 解析出 `(Person, name)`、`(Person, age)`。`labels(m)` 显式请求 labels。不要求整个点 → 走 PropertyExtract + labels。

```
AllNodeScan(Person)       → col[0] = VertexRef
Expand(n, r, m)           → col[0] = VertexRef, col[1] = EdgeKey, col[2] = VertexRef
VertexPropertyExtract(m,  → 追加 col[3]=StringVector(name), col[4]=Int64Vector(age),
  need_labels=true,         同时通过 needs_labels 标记提供 labels
  need_props={Person: [name, age]})
Project                   → col[0]=string, col[1]=ListValue, col[2]=int64
```

### 边缘 case：无 label 的匿名节点 `CREATE ({name: 'x'}) RETURN n.name`

binder 无法从上下文推断 label。退化为 `BoundDynamicPropertyRef`，强制 `need_labels = true`。VertexEnrich 加载 `__anon__` 的 labels 和全部属性，运行时由 `BoundDynamicPropertyRef` 遍历 labels 找到 `name`。

### `MATCH p=(a)-[*2..3]->(b) RETURN p`

`RETURN p` 明确要求完整路径对象 → 走 PathEnricher。

```
AllNodeScan          → col[0] = VertexRef
VarLenExpand(p)      → col[0] = VertexRef, col[1] = VertexRef, col[2] = PathTopology
PathEnricher(p)      → col[2] 原地替换为 Vector<PathValue>（展平去重批量 I/O）
Project              → col[0] = PathValue
```

### `MATCH p=(a)-[*2..3]->(b) RETURN [n IN nodes(p) | n.name]`

只需要路径中顶点的 name 属性 → 走 PathPropertyExtract，不构造 PathValue。

```
AllNodeScan            → col[0] = VertexRef
VarLenExpand(p)        → col[0] = VertexRef, col[1] = VertexRef, col[2] = PathTopology
PathPropertyExtract(p, → 追加 col[3] = List<String>（扁平 name 列表列）
  vertex_positions: all → {need_props: [name]})
Project                → col[0] = List<String>
```

### `MATCH (n) RETURN n`

`RETURN n` 要求完整点对象 → 走 VertexEnricher。

```
AllNodeScan        → col[0] = VertexRef
VertexEnricher(n)  → col[0] 原地替换为 Vector<VertexValue>
Project            → col[0] 透传
```

### `MATCH (a)-[r:KNOWS]->(b) RETURN type(r), r.since`

binder 从 `r:KNOWS` 解析出 label。只需要 type name + 局部属性 → 走 EdgePropertyExtract。

```
AllNodeScan               → col[0] = VertexRef
Expand(a, r, b, KNOWS)    → col[0] = VertexRef, col[1] = EdgeKey, col[2] = VertexRef
EdgePropertyExtract(r,    → 追加 col[3]=StringVector(type), col[4]=Int64Vector(since)
  need_type=true, need_props={KNOWS: [since]})
Project                   → col[0]=string, col[1]=int64
```

全程无 EdgeValue 构造。

### `MATCH (a)-[r:KNOWS]->(b) RETURN r`

`RETURN r` 要求完整边对象 → 走 EdgeEnricher。

```
AllNodeScan               → col[0] = VertexRef
Expand(a, r, b, KNOWS)    → col[0] = VertexRef, col[1] = EdgeKey, col[2] = VertexRef
EdgeEnricher(r)           → col[1] 原地替换为 Vector<EdgeValue>
Project                   → col[1] 透传
```

### `MATCH (n)-[:KNOWS]->(m) WHERE n.age > 30 AND m.city = 'Beijing' RETURN m.name`

这是展示 per-variable 细粒度交织的核心案例。n 和 m 各自走 PropertyExtract（不需要完整点），n 的 Filter 先削减行数，m 的 PropertyExtract 只处理幸存行。

```
AllNodeScan                → col[0] = VertexRef
Expand(n, r, m, KNOWS)     → col[0] = VertexRef, col[1] = EdgeKey, col[2] = VertexRef
VertexPropertyExtract(n,   → 追加 col[3] = Int64Vector（扁平 age 列）
  need_props={Person: [age]})
Filter(n.age > 30)         → 从 col[3] 读取，可能削减 90% 行
VertexPropertyExtract(m,   → 追加 col[4] = StringVector(city), col[5] = StringVector(name)
  need_props={Person: [city, name]})
Filter(m.city='Beijing')   → 从 col[4] 读取
Project                    → col[0] = string
```

全程零 VertexValue 构造，Filter 直接访问扁平列。PropertyExtract 按 per-variable 独立放置，与 Filter 交织，先执行的 Filter 削减行数后，后续 PropertyExtract 只处理幸存行——避免"先统一 Enrich 再过滤"的资源浪费。

---

## 共性架构问题

### 一、逐行 co_await 的普遍存在

每个 read 类算子逐行 `co_await`。1024 行 = 1024 次协程切换 + 1024 次 storage round-trip。

**解法**：统一改为两阶段——收集本 chunk 内所有目标 ID → 一次 batch co_await → 逐行回填。

### 二、重复拆装 DataChunk

> 历史问题，已通过 `ProjectionExtractPhysicalOp` 单算子统一解决（一次构造完整输出 schema，无中间 DataChunk 重建）。

~~VertexLabelRead → VertexPropertyRead 链式包装~~（已删除）。

**当前方案**：`ProjectionExtractPhysicalOp` 一次构建所有 `ColumnSpec` 的输出列，无中间拆装。

### 二点五、DataChunk 列替换接口

Enricher 算子执行 `uint64 → VertexValue` 或 `EdgeKey → EdgeValue` 的类型转换时，不是在 Chunk 末尾追加新列，而是**原位替换**指定列——`col[i]` 从拓扑类型变为语义类型，列数量不变。这要求 DataChunk 提供 `replaceColumn(col_idx, new_vector)` 接口：

- 创建新的 `Vector<VertexValue>`（或 `EdgeValue`），从 `Vector<uint64_t>`（或 `EdgeKey`）逐行构建
- 用新 Vector 替换 Chunk 中对应槽位
- 旧 Vector 随新 Vector 的 ownership 转移而释放

原位替换优于追加新列的理由：(1) 下游算子通过列索引引用变量，列数量不变则索引不变；(2) 避免 Chunk 同时持有同一变量的拓扑和语义两份数据；(3) 语义类型 Vector 替换拓扑类型 Vector 后，拓扑类型数据自然释放，不额外占用内存。

### 三、planner 无条件包裹

> 历史问题，已通过 `dispatchProjectionExtract` + `PlanRequirements` 解决。

~~`wrapVertexLabelRead`、`wrapVertexPropertyRead` 无条件调用~~（已删除）。

**当前方案**：`dispatchProjectionExtract` 从 `BoundLogicalPlan` 根遍历收集 `PlanRequirements`，按需构建 `ColumnSpec`。无需求时只输出 `Passthrough` spec。

### 四、重复的 AST/表达式遍历

binder 中多处复制 visitor 模板：`hasAggregate`、`expressionReferencesVariable`、`walkAndReplaceAggCalls`、`collectColumnNames`。

**解法**：提取通用 `ExpressionVisitor` 基类。

---

## 第二轮审计：Sort、PathBuild、Merge evaluateExpr、CrossProduct 的列传递浪费

以下是对 6 个算子的补充审计。Aggregate 和 Project 已确认无浪费，其余 4 个存在显著问题。

### SortPhysicalOp — 全列物化但只按少数列排序

`sort_physical_op.cpp:37-43, 117-139`：Sort 把**全部**输入列的每一行物化到 `all_rows`（`vector<Value>`）。但排序比较只用 `all_keys`（`sort_items_` 表达式求值结果，通常 1-3 列）。如果输入 20 列、排序键 1 列，95% 的物化数据完全不参与排序。

**浪费比例**：典型场景 ~90-98%。

**解法**：Sort 只物化 sort keys + 下游实际需要的列。planner 在列裁剪 pass 中标记 Sort 需要保留的列集合。

### PathBuildPhysicalOp — 全列拷贝只为一个 path 列

`path_build_physical_op.cpp:36-41`：PathBuild 把输入的所有列拷贝到输出，再追加一个 path 列。但路径构建本身（`element_cols_`）只需要路径元素列，其余列不被 PathBuild 读取。

`MATCH p=(a)-[r]->(b) RETURN p` 中，输入可能有 a、r、b 三列（含属性），PathBuild 全量拷贝它们，而下游 Project 只保留 path 列。

**浪费比例**：典型场景 ~80-95%。

**解法**：PathBuild 输出仅包含路径构建所需的 element columns + 新 path 列。其余列在 PathBuild 之前由列裁剪算子丢弃。

### MergePhysicalOp::evaluateExpr — 每次求值创建完整 DataChunk

`merge_physical_op.cpp:58-78`：`evaluateExpr` 为每一次表达式求值创建一个全新的单行 DataChunk，把**所有**输入列拷贝进去。但表达式通常只引用 1 列（如 `n.age > 30` 只关心 `n` 列）。

调用频率：每个 prop_filter 和 pending_prop 都会调用，且 findMatchingNode/findMatchingEdge 对每个候选顶点/边都会调用。如果 MERGE 有 3 个属性过滤、100 个候选节点、20 个输入列：
- 调用次数：3 × 100 = 300 次 evaluateExpr
- 每次拷贝 20 列 → 总共 6000 次不必要的列拷贝
- 实际只需要 300 次

**浪费比例**：~95% per call，且调用频率极高。

**解法**：`evaluateExpr` 改为只拷贝表达式实际引用的列（可从 `BoundExpression` 中收集 `BoundColumnRef`）。或者让 `VectorizedEvaluator` 支持行级求值模式，不创建临时 DataChunk。

### CrossProductPhysicalOp — 全列交叉

`cross_product_physical_op.cpp:7-8, 29-34`：CrossProduct 输出 `left_cols + right_cols` 列。左右侧的全部列都进入交叉积。N×M 的 row explosion 叠加全列拷贝，内存开销是两者乘积。

**浪费比例**：典型场景 ~70-90%。

**解法**：planner 在 CrossProduct 之前对左右两侧分别插入列裁剪算子。

### 汇总

| 算子 | 浪费比例 | 严重度 |
|------|----------|--------|
| Merge `evaluateExpr` | ~95%/call，高频调用 | **最严重** |
| Sort | ~90-98% | 高 |
| PathBuild | ~80-95% | 高 |
| CrossProduct | ~70-90% | 高 |
| Aggregate | 0%（已正确） | — |
| Project | 0%（已正确） | — |

### 根因

和第一部分审计结论一致：**缺少列裁剪 pass**。Project 知道下游需要哪些列，但这个信息没有向上游传播。所有中间算子（Sort、PathBuild、CrossProduct、Filter、Expand）默认携带全部输入列。

---

## 从典型 Cypher 查询审视

以下按查询类型审视整个 pipeline 的浪费点。

### `MATCH (n) RETURN n ORDER BY n.age`

```
AllNodeScan   → uint64
VertexEnrich  → VertexValue{id, properties: {age}}
Sort(age)     → 按 age 排序 —— 此时 chunk 只有 1 列 (n)
Project       → n
```

**关键**：因为 Enricher 已经裁剪了属性（只加载 age），Sort 收到的 chunk 已经是最小列集。**拓扑/语义分离 + 列裁剪后，Sort 本身不再是问题。**

### `MATCH p=(a)-[r]->(b) RETURN p`

```
AllNodeScan    → uint64
Expand(a,r,b)  → uint64, EdgeKey, uint64 （3 列）
PathEnrich     → 列裁剪后只有顶点/边 ID 列被需要，PathBuild 代替 PathEnrich
PathBuild      → 输入 3 列（a.id, r.id, b.id），输出 +1 列 path
Project        → 只保留 p
```

**关键**：拓扑阶段 a、r、b 都是裸 ID/EdgeKey（8 字节级），不是胖 VertexValue。PathBuild 输入 3 列轻量数据，拷贝开销可忽略。**类型分离已解决 PathBuild 的列拷贝浪费。**

### `MATCH (a), (b) RETURN a, b`

```
AllNodeScan(a)      → uint64
AllNodeScan(b)      → uint64
CrossProduct(a, b)  → 2 列（左右各 1 列 uint64）
Project             → a, b
```

**关键**：CrossProduct 的左右输入都只有一个 `uint64` 列。如果中间插入了 Enricher，也只加载 RETURN 需要的属性。**类型分离保证 CrossProduct 的列宽度最小。**

### `MERGE (a:Person)-[r:KNOWS {since: 2020}]->(b:Person)`

```
LabelScan(a, Person)    → uint64
LabelScan(b, Person)    → uint64
CrossProduct(a, b)      → 2 列 uint64
MergePhysicalOp         → 内部 evaluateExpr 只拷贝表达式引用的列
```

**关键**：MergePhysicalOp 的 evaluateExpr 在做属性比较时（`since: 2020`），输入 chunk 只有 2 列裸 uint64。此时拷贝整列的开销已经非常低。但 evaluateExpr 对每个候选边调用，需要改为批量模式。**Column 宽度已最小化，但调用频率问题仍需 fix。**

### `MATCH (n) WITH n SKIP 10 LIMIT 5 RETURN n.name`

```
AllNodeScan    → uint64
VertexEnrich   → VertexValue{id, properties: {name}}
Skip           → 透传全列
Limit          → 透传全列
Project        → name
```

**关键**：Skip/Limit 在 Enricher 之后，但此时 chunk 已经被 Project 裁剪或 Enricher 已完成——只有 1-2 列。**Skip/Limit 不需要特殊优化，受益于上游裁剪。**

### 总结

| 浪费点 | 是否被本文设计解决 |
|--------|------------------|
| Scan/Expand 输出胖 VertexValue | ✅ 类型分离（VertexRef/EdgeKey） |
| LabelRead/PropertyRead 无条件执行 | ✅ PropertyExtract/Enricher 按需加载 |
| VertexValue 的 map 堆分配（90% 查询不需完整点） | ✅ PropertyExtract 追加扁平列，零堆分配 |
| labels 被无意义加载 | ✅ binder 强模式解析 |
| PathElement 全量属性加载 | ✅ PathPropertyExtract / PathEnricher |
| RETURN n + n.name 重复读存储 | ✅ 覆盖原则——只插 VertexEnricher，Project 零 I/O 解引用 |
| Sort 全列物化 | ✅ 上游裁剪后 Sort 输入已最小 |
| PathBuild 全列拷贝 | ✅ 拓扑类型分离后输入列极轻 |
| CrossProduct 全列交叉 | ✅ 拓扑类型分离后每侧 1-2 列 |
| Merge evaluateExpr | ⚠️ 列宽度已最小，但需修复调用频率和单行 DataChunk 重建 |

---

## 与现有设计文档的冲突分析

以下基于对全部 33 个设计文档的审阅，列出了与本文建议存在冲突或需要协调的设计点。

### 需要新增的类型（与 `type-definitions.md` 冲突）

当前 `Value` variant 没有 `VertexRef`、`EdgeKey`、`PathTopology` 类型。`BoundTypeKind` 枚举也没有对应的列类型。

**需要新增**：
- `VertexRef` — 顶点引用，包装 `uint64`。与裸 `uint64` 的关键区别：(1) `BoundTypeKind::VERTEX_REF` 让 Enricher 能识别出"这是一个待提升的顶点 ID"而非普通整数；(2) 类型安全——不会被误当作 EdgeId、count 或其他整数；(3) 运行时大小与 `uint64` 完全一致（零开销包装）。注意代码库已有 `VertexId = uint64_t` typedef，但 typedef 不提供类型安全，需要 strong type（`struct VertexRef { VertexId id; };`）
- `EdgeKey { EdgeId id; VertexId src_id, dst_id; EdgeLabelId label_id; uint32_t seq; }` — 与现有 `EdgeValue` 的区别是不含 `optional<Properties>` 和 type name。对应 `BoundTypeKind::EDGE_KEY`
- `PathTopology` — 路径骨架，元素序列为 `[(vid, eid, label_id, seq), ...]`，不含 labels 和 properties。对应 `BoundTypeKind::PATH_TOPOLOGY`

### BoundDynamicPropertyRef 的消除（与 `multi-label-design.md` 协调）

`BoundDynamicPropertyRef` 是弱模式属性访问（`n.prop`），在运行时遍历 `VertexValue.labels` 来按名称匹配属性。这强制要求 `need_labels = true`，与按需加载理念矛盾。

**根因**：binder 当前没有充分利用已知信息来解析 `(label_id, prop_id)`。例如 `MATCH (n:Person) RETURN n.name` 中，binder 从 MATCH 的 label filter 知道 `n` 有 label `Person`，完全可以解析出 `(Person, name)` 的 prop_id。但这些信息没有被利用，退化为 `BoundDynamicPropertyRef`。

**解法**：在 binder 中扩展属性解析逻辑。当 binding `PropertyAccess` 表达式时，优先尝试从上下文推断候选 label：

1. 从 MATCH 的 label filter（`n:Person`）获取已知 label
2. 从 CREATE 的 label（`CREATE (n:Person {name: ...})`）获取已知 label
3. 从 RETURN/WITH 上游的 VertexValue 类型信息推断

只有在以上都无法确定时（如匿名节点 `CREATE ({name: 'x'}) RETURN n.name`，label 在绑定阶段未知），才退化为 `BoundDynamicPropertyRef`。

这样 `VertexEnrichPhysicalOp` 的常规路径是：`need_labels = false`，`need_props = {label_id: [prop_id, ...]}`。labels 只在显式请求时加载（`labels(n)` 函数、弱模式退化等边缘场景）。

### SET/REMOVE 便利模式（与 `mixed-mode-design.md` 协调）

便利模式 SET/REMOVE（`n.prop = val`）运行时可从 `VertexValue.labels` 推断目标 label。但如果 binder 在绑定阶段就从上下文解析出 `(label_id, prop_id)`（与属性读相同逻辑），则直接走强模式，不需要 labels。

解析失败时退化为 `BoundDynamicPropertyRef`，此时需 `need_labels = true`。VertexEnrich 负责加载，SET/REMOVE 算子自身不加载 labels。

### VarLenExpand 内部需要 labels 做 DFS（与 `projection-extract-design.md` 协调）

当前 `VarLenExpandPhysicalOp` 在 DFS 路径构建时需要目标顶点的 labels（用于判断路径是否合法）。这部分 labels 的使用是**算子内部**的，不输出到下游列。

**解法**：VarLenExpand 内部保持当前行为——DFS 期间按需获取 labels（这是算子自身逻辑需要，不是 over-fetching）。但输出列（src、dst、path）不再携带 labels——那些由 Enricher 按需补齐。`projection-extract-design.md` 中已经描述了这个方向。

### Cascades 优化器的 Filter pushdown 与 Enricher 位置（与 `cascades-optimizer.md` 协调）

如果 `Filter(WHERE n.name = 'Alice')` 需要先通过 `VertexEnrich` 加载 `name` 属性才能执行，那么 Filter 不能被 pushdown 到 Enricher 之下。当前 `FilterPushdownRule` 不穿透 Project，但 Enricher 是一个新的算子类型，需要更新规则。

**解法**：`FilterPushdownRule` 更新——Filter 不能穿透 `VertexEnrichPhysicalOp` 或 `EdgeEnrichPhysicalOp`（类似于不能穿透 Project）。或者，需求收集在优化器之前完成，Filter 下的 Enricher 仅加载 Filter 需要的属性。

### 列类型系统与表达式求值器（与 `query-engine-design.md` 协调）

`evalPropertyRef` 当前期望 `VertexValue` 列。如果拓扑阶段输出 `uint64` 列（`BoundTypeKind::INT64`），表达式求值器访问属性时会拿不到 `VertexValue`。

**解法**：确保 `VertexEnrich` 在 `evalPropertyRef` 之前执行。planner 在计划阶段保证：任何引用顶点属性的表达式所在的算子，其上游必然有 `VertexEnrich`。`VertexRef` 列不会出现在需要属性访问的表达式所在位置——如果出现，这是 planner 的 bug，`BoundTypeKind::VERTEX_REF` 的类型检查可以在 debug 模式捕获。

---

## 剩余未解决问题

以下是本设计方案有意留到后续处理的问题，以及尚未详细展开的领域。

### VertexRef 必须是强类型

代码库已有 `using VertexId = uint64_t`，但 typedef 不提供类型安全——`VertexId` 可以隐式转换为任何接受 `uint64_t` 的地方。`VertexRef` 需要是强类型（`struct VertexRef { VertexId id; };`），理由：

1. **Enricher 分发**：`VertexEnrichPhysicalOp` 扫描输入列，找到 `BoundTypeKind::VERTEX_REF` 的列进行提升。如果拓扑阶段输出 `INT64` 列，Enricher 无法区分"这是顶点 ID"和"这是 count(*)"。
2. **表达式求值器安全**：`evalPropertyRef` 需要确信输入是顶点引用，不会错误地把 `count(*)` 的 int64 列当作顶点 ID 去加载属性。
3. **零成本**：`sizeof(VertexRef) == sizeof(uint64_t)`，无内存或计算开销。仅编译期类型检查。

对应的 `BoundTypeKind::VERTEX_REF` 和 `BoundTypeKind::EDGE_KEY` 需要加入枚举，Column 系统需要支持这两种新列类型。

### SemiJoin / AntiJoin 应直接消费 VertexRef

`SemiJoinPhysicalOp` 和 `AntiJoinPhysicalOp` 只需要比较顶点 ID 做半连接过滤。它们应该直接在 `VERTEX_REF` 列上工作，不需要 VertexValue。当前设计已隐含支持——这些算子在拓扑阶段运行。

### Distinct / Union / Unwind 也可受益于列裁剪

虽然这些算子本身职责正确，但在它们之前插入列裁剪算子可以进一步减少透传列数。与 Sort/PathBuild/CrossProduct 的列裁剪同属一个 planner pass 的范畴，不需要单独处理。

### 存储层 batch API

Enricher 算子的两阶段 I/O（collect IDs → batch co_await → fill）依赖存储层提供 batch 接口。当前 `IAsyncGraphDataStore` 只有单行接口（`getVertexLabels(vid)`、`getVertexProperties(vid, lid, prop_ids)`）。需要新增：

```
co_await getVertexLabelsBatch(vector<VertexId> vids) → vector<LabelIdSet>
co_await getVertexPropertiesBatch(vector<pair<VertexId, LabelId>> queries, vector<uint16_t> prop_ids) → ...
```

### PlanContext 需携带需求映射

> 已实现：`collectPlanRequirements` 从 `BoundLogicalPlan` 根遍历，结果存于 `PlanRequirements`（包含 `vertex_reqs` / `edge_reqs` / `path_reqs`）。`dispatchProjectionExtract` 查询此映射构建 `ColumnSpec` 数组。详见 [projection-extract-design.md](projection-extract-design.md)。

---

## 与剩余 TCK 失败的兼容性分析

以下逐类评估本文架构是否能支撑修复它们。

### 不会干扰，直接兼容

| 类别 | 失败数 | 根因 | 与本架构的关系 |
|------|--------|------|---------------|
| List | 4 | Pattern comprehension 未实现 | 正交——需要 binder/evaluator 新功能，不涉及物理算子 |
| TypeConversion | 3 | 跨积查询类型推导 | 正交——binder 问题 |
| Pattern | 34 | 模式子查询 | 正交——binder/evaluator/物理算子三层，但不改变现有算子的输入输出类型 |
| Aggregation | 18 | 聚合分组语义 | 正交——binder + 物理算子协同，不影响拓扑/语义类型分离 |
| ExistentialSubqueries | 8 | EXISTS 子查询缺失 | 正交——新特性 |
| Match | 80 | 变量作用域、OPTIONAL MATCH | 正交——binder 语义问题 |
| WITH / WITH WHERE / WITH ORDER BY | 126 | 变量遮蔽、别名解析 | 正交——binder scope 问题 |
| RETURN / RETURN ORDER BY | 43 | 列名生成、投影、DISTINCT | 正交——binder 语义问题 |
| Unwind | 3 | UNWIND 类型边界 | 正交——evaluator 问题 |
| Create | 32 | 副作用计数 | 正交——TCK snapshot 机制，非算子数据正确性 |
| Set | 26 | 副作用计数 | 便利模式需 labels（已在上文冲突分析中处理），其余正交 |
| countingSubgraphMatches | 7 | useCase 步骤定义 | 正交 |
| CALL / triadicSelection | 71 | 未定义步骤 | 远期目标 |

### 会受益于本架构

| 类别 | 失败数 | 说明 |
|------|--------|------|
| MERGE | 13 | Enricher 保证属性在 MERGE 的 `comparePropertyValue` 之前已加载，消除"属性未加载导致 false negative match"的问题。`EdgeEnrich` 提供 type name，支持 `type(r)` 比较 |
| Delete | 13 | 两趟处理已在本次修复中实现。路径 DELETE 需要 `PathEnrich` 先解析路径元素 |
| MatchWhere | 9 | `VertexEnrich` 确保属性在 WHERE 求值前可用 |

### 总结

436 个剩余失败按根因分类：
- **~310 个**（Match、WITH 系列、RETURN 系列、Aggregation）：binder 语义问题，物理算子层不涉及
- **~70 个**（Create、Set、Delete 副作用）：TCK snapshot diff 机制，算子产出数据正确
- **~40 个**（List、Pattern、ExistentialSubqueries）：缺失特性，需要 binder/evaluator 扩展
- **~16 个**（MERGE、Delete、MatchWhere）：物理算子行为，与本文架构兼容或受益

**不存在需要重新设计物理算子 pipeline 的 TCK 失败。**

---

## 验证策略

### EXPLAIN 输出

EXPLAIN 已完整实现（`query/executor/query_executor.cpp`）——格式化算子树为 ASCII 图，展示每个算子的 `toString()` + 输出 schema/types。每完成一个 Phase，对代表性查询跑 `EXPLAIN`，检查算子链和列类型是否符合预期。

### 单元测试（主力验证手段）

新建 `tests/test_physical_planner.cpp`，**使用真实 WiredTiger 存储**（复用 `test_query_executor.cpp` 的 fixture 模式：SetUp 创建 WT 目录 + Sync/Async Store → 建 label/edge label → Parse → Bind → PhysicalPlan → 遍历算子树断言，不执行查询）。真实存储比 mock 更简洁（无需实现两个接口的全部纯虚函数），且能捕获 planner 构造阶段意外调用 store 方法导致的协程挂起。

### 辅助验证

- **TCK 回归**：每 Phase 完成后对比失败数变化，当前基准 436
- **debug 类型断言**：`evalPropertyRef` 遇到 `VERTEX_REF` 列时 assert fail，在开发期捕获 planner 错误

---

## 测试用例矩阵

以下 22 个场景，全部通过 mock store 验证物理计划形状。新建 `tests/test_physical_planner.cpp`。

### 断言辅助工具

```cpp
// 遍历算子树，收集所有算子类型名
std::vector<std::string> collectOpTypes(const PhysicalOperator* root);

// 查找某变量的第一个 Enricher，检查其 Requirement
const VertexEnrichPhysicalOp* findVertexEnrich(const PhysicalOperator* root,
                                                const std::string& var);

// 检查某列在指定算子位置的 BoundTypeKind
BoundTypeKind columnType(const PhysicalOperator* op, size_t col_idx);

// 检查列的 VectorForm（FLAT / CONSTANT / DICTIONARY）
VectorForm columnForm(const PhysicalOperator* op, size_t col_idx);
```

### 一、纯拓扑（零 Enricher 注入）

```
TC-01  MATCH (n) RETURN count(n)
断言:   AllNodeScan 输出 VERTEX_REF，Pipeline 中无任何 Enricher，Aggregate 直接消费 uint64

TC-02  MATCH (a)-[:knows]->(b) RETURN count(a), count(b)
断言:   Scan → Expand（输出 VERTEX_REF/EDGE_KEY/VERTEX_REF）→ Aggregate。无 Enricher。

TC-03  MATCH (a)-[:knows]->(b) RETURN id(a), id(b)
断言:   同 TC-02，Project 只取 id 列，全程 VERTEX_REF。无 Enricher。
```

### 二、ID 过滤 vs 属性过滤（Enricher 最晚位置）

```
TC-04  MATCH (n)-[:knows]->(m) WHERE id(n) = 10086 RETURN id(m)
断言:   Filter(id(n)=10086) 在 Expand 之前或之中，n 和 m 均无 PropertyExtract/Enricher。
       m 的列类型为 VERTEX_REF（不是 VERTEX），Project 直接取 id(m)。

TC-05  MATCH (n)-[:knows]->(m) WHERE n.age > 30 RETURN id(m)
断言:   VertexPropertyExtract(n, need_props={Person:[age]}) 紧邻 Filter(n.age>30) 之前。
       追加 Int64Vector 列，Filter 从此扁平列读取。m 无任何 Enricher（下游只要 id(m)）。

TC-06  MATCH (n)-[:knows]->(m) WHERE n.age > 30 AND m.city = 'Beijing' RETURN m.name
断言:   VertexPropertyExtract(n,{age}) → Filter(n.age>30) → VertexPropertyExtract(m,{city,name}) → Filter(m.city) → Project。
       两个 PropertyExtract 独立、交织。追加的扁平列被下游 Filter 直接引用。无 VertexValue 构造。
```

### 三、常量向量（Constant Vector 复用）

```
TC-07  MATCH (a)-[:post]->(p) WHERE id(a) = 999 RETURN a.name, p.title
断言:   IndexScan(id=999) 返回单行 → Expand 检测到所有输出行共享同一源行 → 源端列 a 主动提升为 CONSTANT。
       VertexPropertyExtract(a) 识别 a 为 CONSTANT → 仅发起 1 次属性查询 → 追加的扁平列保持 CONSTANT。
       VertexPropertyExtract(p) 处理密集 FLAT 列，追加扁平列。
       断言 a 的属性列在 VertexPropertyExtract 后 VectorForm == CONSTANT。
```

### 四、变长路径 + 字段级裁剪

```
TC-08  MATCH p = (a)-[:knows*1..3]->(b) WHERE id(a) = 1 RETURN p, b.city
断言:   VarLenExpand 输出 PATH_TOPOLOGY，非 PathValue。
       PathEnrich 仅对终点 b 注入 need_properties: [city]，路径中间节点需求为空。
       验证 PathRequirement.vertex_positions 仅在终点位置有非空需求。

TC-09  MATCH p = (a)-[:knows*1..3]->(b) RETURN p
断言:   PathEnrich 存在但 PathRequirement 全空（只需拓扑结构，无需属性/labels）。

TC-10  MATCH p = (a)-[:knows*1..3]->(b) RETURN nodes(p), relationships(p)
断言:   PathEnrich 需求覆盖路径中所有顶点和边，PathRequirement 中每个位置都有非空需求。
```

### 五、多变量交织过滤（Interleaved Pushdown）

```
TC-11  MATCH (a)-[:knows]->(b)-[:creates]->(c)
       WHERE a.gender = 'F' AND b.age > 18 AND c.type = 'Post'
       RETURN c.title
断言:   算子链顺序为：
         Scan(a) → VertexPropertyExtract(a,{gender}) → Filter(a.gender)
         → Expand(a→b) → VertexPropertyExtract(b,{age}) → Filter(b.age)
         → Expand(b→c) → VertexPropertyExtract(c,{type,title}) → Filter(c.type)
         → Project(c.title)
       三个 PropertyExtract 各自独立、交织。全部走扁平列，无 VertexValue。
       不允许合并为一个统一的 Enricher。
```

### 六、Labels 场景

```
TC-12  MATCH (n:Person) RETURN labels(n)
断言:   LabelScan 输出 VERTEX_REF。VertexEnrich(n, need_labels=true, need_props={})。
       labels(n) 不需要 properties，need_props 为空。

TC-13  MATCH (n:Person {name: 'Alice'}) RETURN n.name
断言:   binder 从 n:Person 解析出 (Person,name) 的 prop_id，走强模式。
       VertexEnrich(n, need_labels=false, need_props={Person:[name]})。

TC-14  MATCH (n) WHERE labels(n) CONTAINS 'Person' RETURN n.name
断言:   labels(n) 在 Filter 中引用 → need_labels=true。
       n.name 在 RETURN 中 → need_props 非空。
       BoundDynamicPropertyRef 退化场景：need_labels=true + need_props 全属性。

TC-15  MATCH (n) RETURN n
断言:   `RETURN n` 触发 `need_entire_vertex = true` → 走 VertexEnricher（全量物化），非 PropertyExtract。
       VertexEnricher 将 VertexRef 列原地替换为 VertexValue 列。这是"正确的全量"——用户确实要整个顶点。

TC-16  CREATE ({name: 'x'}) RETURN n.name
断言:   匿名节点 binder 无法解析 label → 退化 BoundDynamicPropertyRef。
       VertexEnrich(n, need_labels=true)。运行时遍历 labels 找到 name。
```

### 七、Edge Type 场景

```
TC-17  MATCH ()-[r:KNOWS]->() RETURN type(r)
断言:   EdgeEnrich(r, need_type=true, need_props={})。
       EdgeKey.label_id 已含 label 信息，EdgeEnrich 将 label_id 映射为 type name。

TC-18  MATCH ()-[r:KNOWS]->() RETURN type(r), r.since
断言:   EdgeEnrich(r, need_type=true, need_props={KNOWS:[since]})。

TC-19  MATCH ()-[r]->() WHERE type(r) = 'KNOWS' RETURN r.since
断言:   EdgeEnrich(r, need_type=true) → Filter(type(r)='KNOWS')。
       无 label 的边属性访问退化弱模式，EdgeEnrich 需 need_type=true + need_props 全量。
```

### 八、就地截断（Demand Truncation）

```
TC-20  MATCH (a)-[:knows]->(b) RETURN b.name
断言:   Expand(a→b) 消费 b 的 {need_props:[name]} 需求后，从 ctx 中擦除 b。
       a 的 Scan 下方无任何 Enricher（a 无需求）。b 的 VertexEnrich 仅在 Expand 上方。
       断言 Scan(a) 的 output_types 不包含 VERTEX（全程 VERTEX_REF）。

TC-21  MATCH (a)-[:knows]->(b)-[:creates]->(c) RETURN c.title
断言:   Expand(b→c) 消费 c 的需求 → Expand(a→b) 消费 b 的拓扑依赖但 b 本身无需求 → Scan(a) 无需求。
       仅 c 有 VertexEnrich，a 和 b 的列保持 VERTEX_REF。
```

### 九、额外场景

```
TC-22  MATCH (a), (b) WHERE a.name = b.name RETURN a, b
断言:   CrossProduct 左右输入均为 VERTEX_REF。两侧各有一个 VertexEnrich 在 CrossProduct 之前的各自分支中。
       CrossProduct 内部只有 2 列轻量 uint64 做交叉积。

TC-23  MATCH (n:Person) WHERE exists((n)-[:KNOWS]->()) RETURN n.name
断言:   SemiJoin 右支 Expand 输出 VERTEX_REF，不触发 Enricher。
       左支 VertexEnrich(n, need_props={Person:[name]})。
       SemiJoin 直接在 VERTEX_REF 上比较 ID。

TC-24  MATCH (n:Person) WHERE n.age > 30 RETURN n.name ORDER BY n.name
断言:   Scan → VertexPropertyExtract(n,{age,name}) → Filter(n.age>30) → Sort(n.name) → Project。
       只有一个 PropertyExtract（n 的需求合并为 {age, name}），追加两列扁平 Vector。
       Filter 和 Sort 直接从扁平列读取，无 VertexValue。
```

### 十、设计盲区验证

```
TC-25  MATCH (a)-[:knows]->(b), (a)-[:works_at]->(c)
       WHERE a.age > 30 AND b.city = 'NY' AND c.name = 'Google'
       RETURN a.name
断言:   a 在两个分支（knows、works_at）中共享。Planner 必须只注入**一个** VertexEnrich(a)，
       在两个分支的汇聚点（Join 之前）公共位置放置。不允许 a 出现两个 Enricher。
       验证 enriched_vars 去重机制生效。

TC-26  MATCH p = (a)-[:knows*1..5]->(b) WHERE a.age > 30 RETURN p
断言:   变长扩展可能产生大量路径。Planner 应在 VarLenExpand 之上、RETURN 之前的适当位置
       放置 PathEnrich（可能配合早期截断规则）。验证 PathEnrich 存在且位于 VarLenExpand 之上。
       此 TC 验证基数截断保护逻辑不会错误地完全消除 PathEnrich（在无确切统计时使用默认行为）。

TC-27  MATCH (a)-[:knows]->(b)
       WHERE a.type = 'NormalUser' AND b.is_terrorist = true
       RETURN a, b
断言:   Planner 基于选择率排序 Enricher+Filter 对。
       `b.is_terrorist = true`（等值，sel=0.01）选择率远低于 `a.type = 'NormalUser'`（等值，sel=0.01 相同）。
       在无统计信息时两者选择率默认相同，按拓扑顺序排列即可。
       此 TC 验证选择率估算框架存在且被调用——具体顺序可在有统计信息后进一步断言。

TC-28  MATCH (a)-[:knows]->(b)
       WHERE id(b) = a.target_neighbor_id
       RETURN a, b
断言:   Filter 中 a.target_neighbor_id 是语义属性，id(b) 依赖 Expand 产出 b。
       Planner 检测到跨变量属性依赖 → 将 VertexEnrich(a) 强制提升到 Expand 之前（依赖提升）。
       断言算子链为：VertexEnrich(a, {target_neighbor_id}) → Expand(a→b) → Filter。
       不允许 VertexEnrich(a) 出现在 Expand 之后。

TC-29  MATCH (a)-[:knows]->(b)-[:works_at]->(c),
             (a)-[:likes]->(d)-[:manages]->(c)
       WHERE b.city = 'NY' AND d.level > 5
       RETURN c.title
断言:   菱形查询在 Join(b,d→c) 处汇聚。反向传播时各分支对共享变量分别收集需求：
       分支1 收集 c{title}, b{city}；分支2 收集 c{title}, d{level}。
       Join 节点合并共享变量 c 的需求：{title} ∪ {title} = {title}。
       共享变量 a 两分支均无需求，合并后为空，不注入 Enricher(a)。
       断言 Join 之下只有一个 VertexEnrich(c, {title})，a 全程 VERTEX_REF。
```

---

## 已有优化与后续演进

以下基于对代码库的完整审查，评估 Gemini 建议的落地情况。

### 已实现，无需再改

| 优化 | 现状 | 关键文件 |
|------|------|---------|
| **SelectionVector 零拷贝过滤** | 已完整实现。Filter 通过 DICTIONARY 列共享 `ColumnBuffer`（`shared_ptr`），只改 SelectionVector 下标，不拷贝数据。Expand、SemiJoin 同样使用此模式。 | `data_chunk.hpp:254-310`、`filter_physical_op.cpp:31-47` |
| **边按源顶点聚簇存储** | 已实现。`edge_index` 表 key 为 `{vertex_id, direction, label_id, neighbor_id, seq}`，源顶点 ID 为 leading key，B-tree 中同源顶点的边物理相邻。 | `key_codec.hpp:34-50` |

### 部分存在，本次重构扩展

| 优化 | 现状 | 本次动作 |
|------|------|---------|
| **CONSTANT 向量主动提升** | Expand/VarLenExpand 仅被动传播 CONSTANT（如果上游已产生），从不主动检查"所有行是否共享同一源顶点"来创建 CONSTANT 列。IndexScan 返回单行后 Expand，源端列仍是 DICTIONARY，每条输出行通过 SelectionVector 间接访问 | P4 与输出类型改造一起做：Expand/VarLenExpand 检测到所有输出行共享同一源行时，源端列改用 `Column::constant()`，O(1) 访问、零内存分配 |
| **变长路径中间裁剪** | 仅支持边属性预计算等值过滤（`varlen_expand_physical_op.cpp:155-177`），不支持通用谓词下推到 DFS 内部 | P5 需求模型扩展：planner 识别路径中间节点的属性引用，在 VarLenExpand 后插入 VertexEnrich+Filter。本质是交织下推在变长路径场景的应用 |

### 未实现，纳入本次重构

| 优化 | 难度 | 方案 |
|------|------|------|
| **算子内存复用（Zero-Allocation）** | 低 | PhysicalOperator 基类增加 `output_chunk_` 成员，首次 allocate 后每次 next() 原地覆写复用。已列为 P2.5 |

### 有价值但延后处理

| 优化 | 理由 |
|------|------|
| **I/O 调度器 io_uring + 排序合并** | 工程量大，需改造整个 IO 栈。当前协程+线程池已够用，等 batch API 落地后 profiling 发现瓶颈再动 |
| **向量化 Hash Join（Radix Partitioning）** | 全新算子，正交于当前拓扑/语义分离重构。CrossProduct/SemiJoin 在列裁剪后输入列极窄 |
| **顶点属性与边存储在同一 Page** | 当前 `vprop_{id}` 和 `edge_index` 是独立 WT 表，修改 layout 涉及 KV 编码重构和迁移，风险高 |
| **通用 ExpressionVisitor 提取** | binder 中多处复制 visitor 模板（`hasAggregate`、`expressionReferencesVariable`、`walkAndReplaceAggCalls`、`collectColumnNames`）。正交于拓扑/语义分离 |
