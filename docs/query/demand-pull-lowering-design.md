# Demand-Pull + Expression Lowering 重构方案

> 本文档描述 ProjectionExtract 物化路径的**目标架构**。核心契约：SlotId 是纯粹的列标识符（不带任何 mode 语义），ProjectionExtract 只追加列不修改已有列，所有图语义分析在物理优化器之前的前置 Pass 一次性完成。

## 一、与现行方案的关系

| 维度 | 现行方案（projection-extract-op + slot-id-refactor 过渡态） | 本方案（目标终态） |
|------|----|----|
| 列标识 | 两套并行：`column_index`（runtime）+ `slot_id`（compile-time） | 单一：`slot_id` |
| 列偏移 | 靠 `base_col` / `ProjectResetMap` / `LeftJoinColMap` 修补 | 不存在——layout 变化时 SlotId 不变 |
| `in_schema_changing_op` 分叉 | 必需（区别 Project/Aggregate 与 Filter/Sort） | 消除——前置 Pass 按变量需求一次性决策 |
| PE 对已有列的处理 | 可能改变列内容（LIGHT_REF ↔ FULL_OBJECT） | **只追加新列，永不修改已有列** |
| 物理算子看到的表达式 | 含图语义（`PropertyEval(VariableRef(n), ...)`） | `BoundColumnRef` 或 `BoundPropertyRef(BoundColumnRef, prop)`——无变量名 |
| 复杂度分布 | 散落在 4 个 pass | 收拢在一个前置 Pass |

**本质**：本方案不是推倒重来，而是给当前 slot-id-refactor 的收尾（移除 `column_rewrite` BoundColumnRef 路径）一个清晰的目标架构叙事。

## 二、核心契约

### 2.1 SlotId：纯标识符

`SlotId` 是 binder 为算子输出列分配的标识符。**一旦分配，永不变更，也不携带任何 mode 语义**——它只是一个数字 ID，用来在表达式里引用某一列。

例如 `MATCH (n:Person)` 中 LabelScan 为 `n` 分配 `SLOT_N=1`，那么 `SLOT_N` 在整个计划里永远是"LabelScan 输出的那一列"。这一列装什么是 LabelScan 自己的事（VertexRef），下游不能改。

### 2.2 ProjectionExtract：只追加，不修改

**PE 的输出 = 上游列原样透传 + 按下游需要追加的新列**。

- 上游列：原封不动 re-emit，SlotId 和列内容都不变。
- 新增列：PE 根据下游需求分配新 SlotId，决定装什么（扁平属性 / 完整对象 / labels / ...）。

> 这一契约的关键：PE 不会"改变某列装载的内容"。SLOT_N 由 LabelScan 定义为 VertexRef 列，PE 不能把它改成 VertexValue 列。如果下游需要完整 VertexValue，PE **新增一列**（新 SlotId，比如 SLOT_N_OBJ）来装载。

### 2.3 三阶段流水线（batch 处理）

前置 Pass 严格按三阶段顺序，每阶段内部完整完成才进入下一阶段：

```
阶段 1：收集（Collect）
  遍历整个下游表达式树，把所有对每个变量的需求汇总到一个数据结构里。
  只记录"需要什么"：哪个变量需要完整对象、哪个变量的哪些属性被访问、
  哪个变量的 labels 被访问，等等。不做任何决策，不分配任何 SlotId，
  也不思考 PE 会怎么响应——那是阶段 2 的事。

阶段 2：决策（Decide）
  基于汇总后的需求（不是单条表达式），统一决定 PE 要新增哪些列：
  - 哪些变量需要完整对象列
  - 哪些属性需要扁平列（且未被完整对象列覆盖）
  - 哪些变量需要 labels 列
  为每个新列分配 SlotId。

阶段 3：改写（Lower）
  PE 的输出 layout 已确定，遍历下游表达式树，按 PE 的最终输出
  把 VariableRef / PropertyEval 替换为 BoundColumnRef 或 BoundPropertyRef。
```

> **阶段 1 的边界**：它只声明"我（下游）需要什么"，例如"Filter 需要 n 的 name 属性"、"Project 需要 n 的完整对象"。它**不关心** PE 会怎么满足——是新增完整对象列、还是新增扁平列、还是什么都不做。这些是阶段 2 看完所有需求后才决定的。
>
> **为什么必须 batch**：若逐表达式增量处理，先看到 `n.name` 就分配 SLOT_NAME 加扁平列，再看到 `RETURN n` 又分配 SLOT_N_OBJ——结果 SLOT_NAME 冗余（属性已在 SLOT_N_OBJ 里）。batch 处理让决策看到全貌，避免冗余列。

### 2.4 TupleSlotLayout

每个物理算子维护 `SlotId → 物理列下标` 映射：

```cpp
class TupleSlotLayout {
    std::vector<SlotId> slots_;  // 下标即物理列位置
public:
    void append(SlotId id);
    int  getColumnIndex(SlotId id) const;       // O(n) 查找，编译期一次
    void merge(const TupleSlotLayout& right);   // Join 拼接
    TupleSlotLayout project(const std::vector<SlotId>& output_slots) const;  // WITH 裁剪
};
```

## 三、阶段 2 决策规则

阶段 1 收集到的"变量需求"形如：

```cpp
struct VariableDemand {
    bool needs_full_object = false;       // 是否有 VariableRef(X) 等直接引用
    std::set<PropertyId> props;            // 被访问的属性集合
    bool needs_labels = false;             // labels(n) / type(r)
    // ... 等
};
```

阶段 2 按 X 的 `VariableDemand` 做决策：

| 需求字段 | 决策 |
|---|---|
| `needs_full_object = true` | 新增 `SLOT_X_OBJ` 列装完整 VertexValue。**`props` 集合被覆盖**——属性已在对象内，不再追加扁平列。 |
| `needs_full_object = false` 但 `props` 非空 | 为每个属性 p 新增 `SLOT_X_P` 列装扁平属性值。 |
| `needs_labels = true` 且未被 full_object 覆盖 | 新增 `SLOT_X_LABELS` 列装 List\<String\>。 |

**覆盖原则**：`needs_full_object` 一旦为 true，同变量的所有 props/labels 需求被吸收——它们没有独立列，运行时从完整对象提取。

## 四、阶段 3 改写规则

PE 输出 layout 确定后，下游表达式按以下规则改写：

| 原表达式 | X 的需求情况 | 改写结果 |
|---|---|---|
| `VariableRef(X)` | — | `BoundColumnRef(SLOT_X_OBJ)`（若有对象列）否则 `BoundColumnRef(SLOT_X)` |
| `PropertyEval(X, "p")` | X 有对象列（`SLOT_X_OBJ`） | `BoundPropertyRef(BoundColumnRef(SLOT_X_OBJ), "p")` |
| `PropertyEval(X, "p")` | X 无对象列，有扁平列 `SLOT_X_P` | `BoundColumnRef(SLOT_X_P)` |
| `PropertyEval(X, "p")` | 都没有 | 编译错误（阶段 2 漏决策） |

> **变量名彻底消失**：所有改写结果都基于 SlotId，不再含 `VariableRef`。这是图语义被消除的标志。

## 五、运行时擦除

物理算子 `init()` 阶段，用 `TupleSlotLayout::getColumnIndex(slot_id)` 把 SlotId 固化为物理列下标：

```cpp
// init()
int phys_idx_age = input_layout.getColumnIndex(SLOT_AGE);
this->evaluators_.push_back(std::make_unique<PhysicalColumnRef>(phys_idx_age));

// execute() hot loop —— 纯数组寻址，零 map 查找
auto* age_vector = in_chunk.getColumn(phys_idx_age)->raw_data<int64_t>();
```

> `BoundPropertyRef` 路径同理：init 时把内部 `BoundColumnRef(SLOT_X_OBJ)` 固化为物理下标，execute 时从 VertexValue 列按 property id 提取。

---

## 六、示例 1：`MATCH (n:Person) WHERE n.name = 'Alice' RETURN n, n.age`

**特征**：`RETURN n` 直接引用变量，触发 `SLOT_N_OBJ` 新增。属性访问被对象列覆盖，不再追加扁平列。PE 共新增 **1 列**。

### 初始 Bound 树

```
Project(items=[VariableRef(n), PropertyEval(n, "age")])
  └─ Filter(pred=PropertyEval(n, "name") == 'Alice')
       └─ LabelScan(n : Person)
```

LabelScan 输出 `[SLOT_N=1 (VertexRef)]`。

### Step 1：默认透传插入 PE

PE 初始输出 `[SLOT_N=1 (VertexRef)]`（原样透传）。

### Step 2：阶段 1 收集需求

遍历整棵下游表达式树，按变量汇总：

```
reqs["n"] = {
    needs_full_object: true,          // Project items 有 VariableRef(n)
    props: {name, age},                // Filter n.name + Project n.age
    needs_labels: false,
}
```

### Step 3：阶段 2 决策

按决策表：

- `needs_full_object=true` → 新增 `SLOT_N_OBJ=2`（VertexValue）
- `props={name, age}` → 被 `SLOT_N_OBJ` 覆盖，**不追加扁平列**

PE 最终输出：

```
[SLOT_N=1 (VertexRef, 原样透传), SLOT_N_OBJ=2 (VertexValue, 新增)]
```

物理动作：批量读 Person 的 labels + 全属性，构造 VertexValue 写入 column 1（SLOT_N_OBJ 对应物理列）。

### Step 4：阶段 3 改写

| 算子 | 原表达式 | 改写后 |
|---|---|---|
| Filter | `PropertyEval(VariableRef(n), "name") == 'Alice'` | `BoundPropertyRef(BoundColumnRef(SLOT_N_OBJ), "name") == 'Alice'` |
| Project items[0] | `VariableRef(n)` | `BoundColumnRef(SLOT_N_OBJ)` |
| Project items[1] | `PropertyEval(VariableRef(n), "age")` | `BoundPropertyRef(BoundColumnRef(SLOT_N_OBJ), "age")` |

> SLOT_N 在下游无人引用（都引用 SLOT_N_OBJ），可由后续优化器剪除，但 PE 不主动消除。

### 运行时擦除

PE 输出 layout `[SLOT_N, SLOT_N_OBJ]` → 物理下标 `[0, 1]`。

- Filter.init: `getColumnIndex(SLOT_N_OBJ) = 1`，固化为 `BoundPropertyRef(phys_col=1, prop_id=name)`
- Project.init: items[0] 固化为 `PhysicalColumnRef(1)`；items[1] 固化为 `BoundPropertyRef(phys_col=1, prop_id=age)`

### 运行时

```cpp
// Filter: 从 column 1 的 VertexValue vector 取出每行的 VertexValue，
//         按 name_prop_id 取得 STRING，与 'Alice' 比较
// Project: items[0] 直接整列发 VertexValue vector；
//          items[1] 按 age_prop_id 从 VertexValue 取得 INT64
//         没有任何变量名解析（prop_id 已在 binder 阶段固定）
```

---

## 七、示例 2：`MATCH (n:Person) WHERE n.name = 'Alice' RETURN n.age`

**特征**：无直接变量引用，`needs_full_object=false`。每个属性各自追加扁平列。PE 共新增 **2 列**。

### 初始 Bound 树

```
Project(items=[PropertyEval(n, "age")])
  └─ Filter(pred=PropertyEval(n, "name") == 'Alice')
       └─ LabelScan(n : Person)
```

LabelScan 输出 `[SLOT_N=1 (VertexRef)]`。

### Step 1：默认透传插入 PE

PE 初始输出 `[SLOT_N=1 (VertexRef)]`。

### Step 2：阶段 1 收集需求

```
reqs["n"] = {
    needs_full_object: false,        // 没有 VariableRef(n)
    props: {name, age},               // Filter n.name + Project n.age
    needs_labels: false,
}
```

### Step 3：阶段 2 决策

按决策表：

- `needs_full_object=false` → **不新增**对象列
- `props={name, age}` → 各自追加扁平列：`SLOT_NAME=2`, `SLOT_AGE=3`

PE 最终输出：

```
[SLOT_N=1 (VertexRef, 原样透传), SLOT_NAME=2 (STRING, 新增), SLOT_AGE=3 (INT64, 新增)]
```

物理动作：批量读 name 和 age 属性，作为独立 vector 写入对应物理列。**不构造 VertexValue**。

### Step 4：阶段 3 改写

| 算子 | 原表达式 | 改写后 |
|---|---|---|
| Filter | `PropertyEval(VariableRef(n), "name") == 'Alice'` | `BoundColumnRef(SLOT_NAME) == 'Alice'` |
| Project items[0] | `PropertyEval(VariableRef(n), "age")` | `BoundColumnRef(SLOT_AGE)` |

> 这里**没有** `BoundPropertyRef`——属性已在扁平列，直接引用即可。

### 运行时擦除

PE 输出 layout `[SLOT_N, SLOT_NAME, SLOT_AGE]` → 物理下标 `[0, 1, 2]`。

- Filter.init: `getColumnIndex(SLOT_NAME) = 1`
- Project.init: `getColumnIndex(SLOT_AGE) = 2`

### 运行时

```cpp
// Filter: column 1 STRING vector 与 'Alice' 直接比较
// Project: column 2 INT64 vector 直接输出，零字典查找，零对象解包
```

---

## 八、两例对比

| 项 | 示例 1（RETURN n, n.age） | 示例 2（RETURN n.age） |
|----|----|----|
| `needs_full_object` | true | false |
| PE 新增列 | 1 列（SLOT_N_OBJ） | 2 列（SLOT_NAME, SLOT_AGE） |
| PE 输出 layout | `[SLOT_N, SLOT_N_OBJ]` | `[SLOT_N, SLOT_NAME, SLOT_AGE]` |
| `VariableRef(n)` 改写 | `BoundColumnRef(SLOT_N_OBJ)` | （不出现） |
| `PropertyEval(n, p)` 改写 | `BoundPropertyRef(BoundColumnRef(SLOT_N_OBJ), p)` | `BoundColumnRef(SLOT_<p>)` |
| SLOT_N 列 | 仍 VertexRef（无人引用，可剪除） | 仍 VertexRef（无人引用，可剪除） |
| 运行时属性访问 | 按 prop_id 从 VertexValue 取值 | 按 prop_id 从扁平列取值 |
| KV 读 | 全属性一次批量加载 | name + age 两次批量加载 |

> **关于运行时性能**：两条路径都是 O(1) 的 prop_id 取值。`BoundPropertyRef` 比 `BoundColumnRef` 多一次间接（先取 VertexValue，再按 prop_id 取属性），扁平列则在缓存局部性上略胜（属性值连续存储）。对绝大多数工作负载这个差异远小于 IO / 对象构造开销——PE 一次批量加载就已经填满了所有需求。

**关键结论**：

1. **SLOT_N 在两例里完全相同**——永远是 LabelScan 输出的 VertexRef 列，PE 不动它。差异在 PE **新增了什么列**。
2. **示例 1 新增 1 列**（完整对象）；**示例 2 新增 2 列**（扁平属性）。决策由阶段 2 一次性做出，基于完整需求汇总而非单条表达式。
3. **覆盖原则的体现**：示例 1 中 `needs_full_object=true` 吸收了 props 需求，避免了冗余扁平列。若不 batch 处理，会先看到 `n.name` 加 SLOT_NAME，再看到 `RETURN n` 加 SLOT_N_OBJ，导致 SLOT_NAME 冗余。
4. **两种改写形式都不含变量名**——整棵树移交物理优化器时，已无任何 `VariableRef` 图语义。

**架构收益**：SlotId 的纯粹性（无 mode 语义）+ PE 的只追加契约（不修改已有列），让 layout 演化路径单调——只增长，不变更。所有决策集中在阶段 2，下游改写是机械的查表替换。现行方案的 `in_schema_changing_op` / `base_col` / `ProjectResetMap` 等机制在本方案中**全部消失**。

## 九、落地路径

### 9.1 与当前 slot-id-refactor 的衔接

当前 slot-id-refactor 已实现：
- `SlotAllocator` + `slot_id` 字段（binder 层）✅
- `TupleSlotLayout` + `compileOperatorTree` + `ExpressionCompiler` ✅
- `dispatchProjectionExtract` + `makeSlotLayout` ✅

待完成（本方案落地 = 这几项的收尾）：
- 移除 `column_rewrite` 的 BoundColumnRef base_col 路径
- 移除 `PropertyExtractionInfo::base_col` / `input_base_col`
- 移除 `ProjectResetMap` / `LeftJoinColMap`
- 移除 `in_schema_changing_op` 分叉（`collectExprReqs` 简化为单一逻辑）
- 改造 `dispatchProjectionExtract`：PE 输出从"可能改变列内容"改为"只追加新列"
- 引入 `VariableDemand` 替换 `VariableRequirement`（语义上更贴近本方案）

### 9.2 风险

- 阻塞测试 `WithThenMatchExpandSingleHop`：当前因两套系统并存而难以修复，纯 SlotId 路径下应自然消失——需验证。
- Aggregate 输出 schema 重置：需要 `TupleSlotLayout::project(output_slots)` 配合，group-key 变量 SlotId 保留，其余失效。
- Join 右子树 SlotId 冲突：query-global SlotAllocator 应足够，但需测试自关系和 nested pattern。
- PE 输出列数变多（因为不再"就地替换"）：可由后续 dead-column elimination 优化器阶段剪除无人引用的 SLOT_N 等。
- PathElementPropertyRead：本方案不解决也不恶化，保留独立算子。

## 十一、实施中的设计决策（2026-07-12）

在落地过程中发现四个具体的设计差距。本节记录每个差距的现象、根因与决策。

### 11.1 差距 A：Lower 阶段对 source 类型一无所知

**现象**：`CREATE (n {name: 'foo'}) RETURN n.name AS p` 返回 null。CreateNode 输出的 `n` 列已经是完整 VertexValue，但下游 PE 看到 `need_whole_vertex` 后又 emit 了一次 ConstructVertex；ConstructVertex 运行时通过 `getVertexLabels(vid)` + `getVertexProperties(vid, lid)` 重建 VertexValue，对于无 label 的 CREATE（属性挂在 `__anon__`）会丢失属性。

**根因**：Decide 阶段（`buildExtractionInfo`）只看 PlanRequirements，不知道 source 列已经是 VERTEX/EDGE（来自 CreateNode / CreateEdge / Merge）还是 VERTEX_REF/EDGE_KEY（来自 LabelScan / Expand）。结果对已是完整对象的 source 仍然分配 `$obj` slot 并触发 Construct。

**决策**：新增 `collectSourceTypes(root) → {var → BoundTypeKind}`，遍历 bound plan 记录每个变量的产出算子类型。`buildExtractionInfo` 接受 source_types 作为输入，当 source 已是 VERTEX/EDGE 时：
- `pi.object_slot_id = pi.source_slot_id`（不分配新 slot，alias 到 source）
- Lower 阶段（`rewriteExpr`）已有的"object_slot_id 不为 INVALID 则路由到该 slot"逻辑自动生效，表达式正确指向 source 列

`dispatchProjectionExtract` 同步检查 source 类型，跳过 Construct emit。两层都基于同一信号（source 类型）决策，保持一致。

> **核心原则**：PE 拿到下游需求后决定追加哪些列时，必须先看 source 已有的列能否满足——而不是机械地按需求追加。CreateNode 的输出列本身就是一个完整的点，下游要完整点时直接用即可。

### 11.2 差距 B：`in_schema_changing_op` 启发式

**现象**：`collectExprReqs` 通过 `in_schema_changing_op` 布尔参数区分当前是否在 schema-changing op（Project / Aggregate / Set / Remove）内，借此强制 Set/Remove 走 `need_whole_vertex` 路径。

**根因**：这是历史遗留的启发式——Set/Remove 需要 mutate VertexValue，所以"推测"它们需要完整对象。但需求声明应该是消费者自己的职责，不该由 collector 启发式决定。

**决策**：移除 `in_schema_changing_op` 参数与对应分支。Set/Remove 的 demand 在 `collectOpReqs` 的 op 级别已经显式声明 `need_whole_vertex = true`（见现有代码），所以移除启发式不影响行为，只让 collector 更纯净。

### 11.3 差距 C：Decide 阶段分裂

**现象**：Decide 散落在两处：
- `column_rewrite.cpp::buildExtractionInfo`：负责 slot 分配，视野只有 PlanRequirements
- `physical_planner.cpp::dispatchProjectionExtract`：负责 ColumnSpec emit，视野有 PlanRequirements + child schema + source types + existing_cols

两者通过**字符串命名约定**（`var$obj`、`var$v.<lid>.<pid>` 等）同步 slot_id。

**根因**：缺失一份显式的"PE 计划"中间结构。决策没集中导致：
- 命名错配 silently 产生 slot 分歧（已发生过 `p<pid>` vs `<pid>` 的 bug）
- dedup（existing_cols）只在第二处做，第一处分配的 slot 可能指向不存在的列
- source 类型判断要在两处分别做（差距 A 修复时必须双线修改）

**决策**：引入 `PEPlan` 结构，`buildExtractionInfo` 成为唯一决策点，输出结构化列计划（source_slot_id / object_slot_id / prop_slot_ids / label_slot_id / construct_kind 等字段）；`dispatchProjectionExtract` 退化为纯消费者，遍历 PEPlan emit ColumnSpec。彻底斩断字符串命名约定。**与差距 D 一并落地**（见 11.4）。

### 11.4 差距 D：变量重命名（别名）破坏需求收集

**现象**：`MATCH (n:Person) WITH n AS m RETURN m.name` 返回 null。LabelScan 产出 `SLOT_N (n, VertexRef)`，WITH Project 改名为 `m`，下游 `RETURN m.name` 的需求归到 `m` 名下，与 `n` 分裂。Decide 为 `m` 分配 `SLOT_M_NAME`，但 PE 在 LabelScan 下游、输入 schema 只有 `n`，跳过 `m`。运行时 slot 未填充。

**根因**：

1. `PlanRequirements` 以**变量名字符串**为键，同一变量的不同名字分裂为多条需求。
2. `collectSourceTypes` 只看 source 算子（LabelScan/Expand/Create*），不认识 Project 输出，别名变量的 source 类型始终为 UNKNOWN。
3. PE 只在 source 算子下游插入，别名变量名不在任何 PE 的输入 schema 里。

**决策（一次性落地，不再分批）**：三件事一并实施——

- **数据结构**：`PlanRequirements` 的 key 从 `string` 升级为 `SlotId`。新增 `collectAliasSlotMap: SlotId → SlotId`（alias_slot → canonical_slot），`getCanonicalSlot(slot)` 递归解析。所有 collector / Decide / Lower 以 SlotId 为键。
- **决策集中（差距 C）**：引入 `PEPlan` 中间结构，`buildExtractionInfo` 成为唯一决策点，`dispatchProjectionExtract` 退化为纯消费者。彻底斩断字符串命名约定。
- **透传 Pass**：新增 `LowerAliasPassthroughPass`，在 Lower 阶段前置运行。对每个形如 `BoundVariableRef(X) AS alias` 的 Project item，把 X 在 PEPlan 中的所有派生 slot 作为新增 item 追加进该 Project（每个新 item 的 `output_slot` 直接设为对应派生 slot，如 `SLOT_N_NAME` → 透传后变成 `SLOT_M_NAME`）。Project 物理算子保持纯粹——只按 `items[].output_slot` 裁切/重排 layout。

#### 例子 1：变量别名 + 属性访问（核心场景）

```cypher
MATCH (n:Person) WITH n AS m RETURN m.name
```

**Bound 树与 Slot 分配**：

```
Project items=[PropertyEval(VariableRef(m), "name")] alias="name"        // RETURN, no new slot
  └─ Project items=[VariableRef(n)] alias="m"  → allocates SLOT_M        // WITH
       └─ LabelScan(n : Person)               → allocates SLOT_N
```

| 变量 | SlotId | 产出算子 | BoundTypeKind |
|------|--------|---------|--------------|
| `n`  | SLOT_N=1 | LabelScan | VERTEX_REF |
| `m`  | SLOT_M=2 | WITH Project | (alias of SLOT_N) |

**阶段 0：collectAliasSlotMap**

```
alias_slot_map = { SLOT_M → SLOT_N }
getCanonicalSlot(SLOT_M) = SLOT_N
```

**阶段 1：collectPlanRequirements**

`PropertyEval(VariableRef(m), "name")` → `getCanonicalSlot(SLOT_M) = SLOT_N` → 需求归到 SLOT_N：

```
reqs = { SLOT_N: { need_whole_vertex=false, vertex_props={"name"} } }
```

SLOT_M 不出现在 reqs 中。

**阶段 2：buildExtractionInfo（产出 PEPlan）**

`source_types[SLOT_N] = VERTEX_REF`，`need_whole_vertex=false`，走扁平列路径：

```
PEPlan[SLOT_N] = {
    source_slot_id: SLOT_N,
    object_slot_id: INVALID,                     // 不分配对象列
    prop_slot_ids: { "name" → SLOT_N_NAME=3 },   // 新分配
    label_slot_id: INVALID,
    construct_kind: PropertyLoad,
}
```

**阶段 3a：dispatchProjectionExtract（PE emit）**

PE 是 LabelScan 下游那个，输入 schema 含 SLOT_N。从 PEPlan 取 `PEPlan[SLOT_N]`，emit 一列 PropertyLoad：物理动作 = 从 SLOT_N 的 VertexRef 读出 labels，按 lid 找 Person 表，按 pid=name 读 STRING，写入新列 SLOT_N_NAME。

PE 输出 layout：`[SLOT_N (VertexRef), SLOT_N_NAME (STRING)]`

**阶段 3b：LowerAliasPassthroughPass（透传到 WITH 下游）**

WITH Project 的 items[0] = `BoundVariableRef(n) AS m`。Pass 识别为别名 item：
- 查 PEPlan[SLOT_N]，列出所有派生 slot：`{ SLOT_N_NAME }`
- 为每个派生 slot 在 WITH 输出 schema 里追加别名 slot：`SLOT_M_NAME=4`
- 设置 WITH items 的 output_slot：`items[0].output_slot=SLOT_M`，追加 item `__fwd_m_v_*` 的 `output_slot=SLOT_M_NAME`
- 在 alias_slot_map 里记录 `SLOT_M_NAME → SLOT_N_NAME`

**阶段 3c：表达式改写（Lower）**

`PropertyEval(VariableRef(m), "name")` → `BoundColumnRef(SLOT_M_NAME)`。`SLOT_M_NAME` 经过 alias_slot_map 解析到 `SLOT_N_NAME`，最终物理下标相同。

**运行时**

PE 输出 layout `[SLOT_N, SLOT_N_NAME]` → 物理下标 `[0, 1]`。
WITH Project 输出 layout `[SLOT_M, SLOT_M_NAME]`。`SLOT_M` 复用 SLOT_N 的物理下标 0（VariableRef 直接 alias），`SLOT_M_NAME` 复用 SLOT_N_NAME 的物理下标 1。**WITH 物理算子零拷贝**——只是 layout 重命名，不搬运数据。
RETURN Project items[0] init 时 `getColumnIndex(SLOT_M_NAME) = 1`，固化为 `PhysicalColumnRef(phys_col=1)`。

Hot loop 只做数组寻址：`out[i] = chunk[1][i]`。

#### 例子 2：变量别名 + 整体对象需求

```cypher
MATCH (n:Person) WITH n AS m RETURN m
```

**Slot 分配**：同例子 1（SLOT_N=1, SLOT_M=2）。

**阶段 1**：`RETURN VariableRef(m)` 触发 `need_whole_vertex=true`，归到 canonical：

```
reqs = { SLOT_N: { need_whole_vertex=true, vertex_props={} } }
```

**阶段 2**：`source_types[SLOT_N] = VERTEX_REF`，`need_whole_vertex=true` → 分配对象列：

```
PEPlan[SLOT_N] = {
    source_slot_id: SLOT_N,
    object_slot_id: SLOT_N_OBJ=3,            // 新分配
    prop_slot_ids: {},                        // 被对象列覆盖
    construct_kind: ConstructVertex,         // 从 SLOT_N 的 VertexRef 构造 VertexValue
}
```

**阶段 3a：PE emit** ConstructVertex（读 labels + 全属性，构造 VertexValue 写入 SLOT_N_OBJ）。PE 输出 layout `[SLOT_N, SLOT_N_OBJ]`。

**阶段 3b：透传 Pass** 同例子 1：识别 WITH 是别名 item，追加 `SLOT_M_OBJ=4` 到 WITH 输出，记录 `SLOT_M_OBJ → SLOT_N_OBJ`。

**阶段 3c：改写** `RETURN VariableRef(m)` → `BoundColumnRef(SLOT_M_OBJ)`。

**运行时** WITH 物理算子零拷贝；RETURN 直接读 column 1（SLOT_N_OBJ 的物理下标）。

#### 例子 3：多层别名链

```cypher
MATCH (n:Person) WITH n AS m WITH m AS k RETURN k.name
```

**Bound 树**：三层 Project，分别分配 SLOT_M=2、SLOT_K=3。

**阶段 0**：

```
alias_slot_map = { SLOT_M → SLOT_N, SLOT_K → SLOT_M }
getCanonicalSlot(SLOT_K) = SLOT_N   // 两跳递归
```

**阶段 1**：reqs 归到 SLOT_N。

**阶段 2**：PEPlan[SLOT_N] 同例子 1，emit SLOT_N_NAME。

**阶段 3b：透传 Pass 自下而上逐层处理**：

```
WITH 1 (n→m): items += __fwd_m_v_*(output_slot=SLOT_M_NAME → SLOT_N_NAME)
WITH 2 (m→k): items += __fwd_k_v_*(output_slot=SLOT_K_NAME → SLOT_M_NAME → SLOT_N_NAME)
```

每一层透传时，Pass 查上一层（m 的）透传结果，把对应的派生 slot 复制过来。最终 SLOT_K_NAME 经过两跳解析到 SLOT_N_NAME。

**运行时** 三层 Project 全部零拷贝，热路径只读 column 1。

#### 例子 4：派生列（反例，不记别名）

```cypher
MATCH (n:Person) RETURN n.name AS x
```

**Bound 树**：

```
Project items=[PropertyEval(VariableRef(n), "name")] alias="x"   // RETURN, allocates SLOT_X
  └─ LabelScan(n : Person)                                        // SLOT_N
```

**阶段 0：collectAliasSlotMap**

items[0].expr 是 `PropertyEval`，不是 `BoundVariableRef` → **不记录别名**。`alias_slot_map = {}`。

**阶段 1**：

```
reqs = { SLOT_N: { vertex_props={"name"} } }
```

**阶段 2 / 3a**：同文档第七章标准例子。PEPlan[SLOT_N].prop_slot_ids["name"] = SLOT_N_NAME，PE emit 此列。

**阶段 3b：透传 Pass** items[0].expr 不是 BoundVariableRef → **跳过**（assert 兜底）。WITH/Project 输出 schema 不追加任何派生列，只列 SLOT_X。

**阶段 3c：改写** Project items[0] = `BoundColumnRef(SLOT_N_NAME) AS x` —— Project 物理算子把 column 1 改名输出为 SLOT_X。

**关键**：x 是真正的派生列（绑定到新 slot），不是别名。透传 Pass 通过严格判断 expr 类型自动排除此场景。

#### 例子 5：属性别名（反例，不记别名）

```cypher
MATCH (n:Person) WITH n.age AS a RETURN a + 1
```

**Bound 树**：

```
Project items=[BinaryOp(+, BoundColumnRef(a), Literal(1))]                // RETURN
  └─ Project items=[PropertyEval(VariableRef(n), "age")] alias="a"        // WITH, SLOT_A
       └─ LabelScan(n : Person)                                            // SLOT_N
```

**阶段 0**：WITH items[0].expr 是 `PropertyEval` → 不记别名。

**阶段 1**：

```
reqs = { SLOT_N: { vertex_props={"age"} } }   // WITH 里的 n.age 产生
// RETURN 里 a 是 BoundColumnRef（slot），不产生 reqs
```

**阶段 2 / 3a**：PEPlan[SLOT_N].prop_slot_ids["age"] = SLOT_N_AGE，PE emit。

**阶段 3b**：透传 Pass 跳过（非 BoundVariableRef）。

**阶段 3c**：WITH Project 把 SLOT_N_AGE 改名输出为 SLOT_A（真正的派生列）；RETURN `a + 1` 直接引用 SLOT_A。

**关键**：binder 已把 `a` 处理为独立 slot，下游通过 slot 引用而非变量名。透传 Pass 不影响此场景。

#### 例子的覆盖小结

| # | 场景 | 记别名 | 透传 Pass 处理 | 输出列形态 |
|---|------|--------|---------------|----------|
| 1 | 变量别名 + 属性 | SLOT_M → SLOT_N | 追加 SLOT_M_NAME | WITH 零拷贝，layout 重命名 |
| 2 | 变量别名 + 对象 | SLOT_M → SLOT_N | 追加 SLOT_M_OBJ | 同上 |
| 3 | 多层别名链 | m→n, k→m | 逐层追加 SLOT_*_NAME | 三层 Project 全零拷贝 |
| 4 | 派生列名（PropertyEval AS x） | 否（expr 非 VariableRef） | 跳过 | Project 把派生列改名 |
| 5 | 属性别名（n.age AS a） | 否（同上） | 跳过 | 同上 |

#### 实施计划

1. **数据结构改造**：`PlanRequirements` key 从 `string` → `SlotId`；`VariableRequirement::slot_id` 替换 `var_name`；所有 collector / Decide / Lower 函数签名同步更新。
2. **新增 `collectAliasSlotMap`**：visitor 遍历 BoundProjectOp + BoundAggregateOp.group_keys，仅当 item.expr 是 `BoundVariableRef` 时记录 `alias_slot → source_slot`。带环检测。
3. **新增 `getCanonicalSlot(slot)`**：递归解析到根。
4. **改造 `collectPlanRequirements` / `collectSourceTypes`**：以 SlotId 为键；找不到时 fallback 到 `getCanonicalSlot`。
5. **引入 PEPlan（差距 C）**：`buildExtractionInfo` 成为唯一决策点，输出结构化 PEPlan；`dispatchProjectionExtract` 退化为纯消费者，遍历 PEPlan emit ColumnSpec。彻底移除字符串命名约定。
6. **新增 `LowerAliasPassthroughPass`**：输入 alias_slot_map + PEPlan。遍历 bound tree，对每个 Project/Aggregate 的 alias item（assert item.expr 是 BoundVariableRef），查 PEPlan[source_slot] 列出所有派生 slot（object / props / labels），为每个派生 slot 在该 Project 追加一个 passthrough item 并把其 `output_slot` 设为对应派生 slot。别名链：自下而上逐层处理，递归解析。
7. **TupleSlotLayout 增强**：`project()` 支持两 slot 指向同一物理下标（alias slot 复用 source 的物理列）。
8. **Join SlotId 融合**：`compileOperatorTree` 加 assert，Join 算子 init() 必须用 merge 后的 layout。
9. **TCK 验证**：以 `WITH ... AS` 相关 feature 为主，跑 catD 全量对比。

> 编码顺序：步骤 1-4 是"换 key 类型"，机械但量大；步骤 5 是差距 C 重构，工作量最大；步骤 6 依赖 5（PEPlan）和 1-4（SlotId key）；步骤 7-8 是物理层配套；步骤 9 是验证。

## 十、相关文件

| 文件 | 角色 |
|------|------|
| `column_rewrite.cpp` | 阶段 1 收集 + 别名映射（差距 D）；输出 PEPlan（差距 C） |
| `physical_planner.cpp:dispatchProjectionExtract` | 退化为纯消费者，遍历 PEPlan emit ColumnSpec |
| `physical_planner.cpp:compileOperatorTree` | 运行时擦除（SlotId → column_index），加 Join layout assert |
| `physical_plan/expression_compiler.hpp` | 阶段 3 改写的运行时编译 |
| `physical_plan/slot_layout.hpp` | `TupleSlotLayout` 强化（merge / project 支持 alias slot） |
| `planner/slot_id.hpp` | `SlotAllocator` + `VariableDemand` 定义 |

## 十二、落地实况（2026-07-12 实施记录）

实际实现相较 §11.4 计划有若干调整，记录如下（防止后续回退）：

### 12.1 用 pre-pass 预分配 slot，绕开改 binder

`allocateAllSlots(root, name_to_slot, alloc)` 在物理规划入口遍历 bound tree，为**每个变量名（含 Project/Aggregate 别名）**分配 slot 写入 `name_to_slot`。这样无需改 binder 的 WITH scope reset 逻辑即可让别名有 slot。所有 collector / Decide / Lower 以 `name_to_slot` + `alias_map` 把变量名解析为 canonical slot。

### 12.2 别名识别覆盖 BoundColumnRef

binder 把 `WITH n`（同名转发）绑为 **BoundColumnRef**、`WITH n AS m`（改名）绑为 **BoundVariableRef**。`collectAliasSlotMap` 与 forward 识别**两者都要处理**，否则同名转发的整对象需求分裂、改名的属性需求不合并。

### 12.3 `ProjectItem::output_slot`（取代 §11.4 的"追加别名 slot"叙述；2026-07-14 重构）

Project 的物理输出 layout 不再靠追加 `__alias_*` item + 名字查找，而是由 pass 预计算每个 item 的 `output_slot`（`SlotId`，默认 `INVALID_SLOT_ID`），physical planner 直接据此建 layout：
- **forward item**（expr 是裸变量/列引用且类型为图实体）：`items[i].output_slot` = PEPlan.object_slot（覆盖态）或 canonical source（扁平态）或别名自身 slot（无 PEPlan），与 rewrite 对该 item 的提升保持一致。
- **派生列**（计算式 / 属性别名）：用 binder 的 alias slot。
- **类型门控**：只有图实体（VERTEX/EDGE/*_REF/*_KEY）才做 PEPlan 提升，避免 `WITH a.name AS a` 这类标量遮蔽变量时错误复用旧节点的列（binder 会复用被遮蔽变量的 slot）。
- 扁平转发场景才追加 `__fwd_*` 列携带 prop/labels/type slot（每个追加 item 的 `output_slot` 直接等于其派生 slot）。
- 物理规划器 per-item fallback：任一 item 的 `output_slot == INVALID_SLOT_ID` 时按名查 `var_slots` 或新分配——**不能**改成"任一 INVALID 则全部走 makeSlotLayout"的 all-or-nothing，否则带 PEPlan 提升的 graph 列会丢失 object_slot（详见 §13.2-D 的陷阱记录）。

历史上此字段曾外挂在 `BoundProjectOp::output_slots`（与 `items` 平行的 vector），2026-07-14 合并进 `ProjectItem::output_slot`，消除"items 改动需手动同步 output_slots"的并行数组风险。

### 12.4 求值顺序 UB 修复

`PlanOperatorResult{std::move(op), std::move(output_schema), ..., makeSlotLayout(output_schema, ctx)}` 中，brace-init 从左到右求值，`makeSlotLayout` 会读到已被 move 空的 schema。CrossProduct/LeftJoin 因此 layout 缺列，`WITH <expr> AS x MATCH (d) RETURN d.v` 返回 null。修复：所有 join 的 layout 先在**局部变量**里构建再 move。

### 12.5 mutation-op 集成（append-only 契约的配套）

PE 只追加后，SET/REMOVE/DELETE/MERGE 若仍按变量名取列会拿到 VertexRef（源列）而非构造出的 VertexValue（追加列）→ no-op。修复：
- 三个 op 的 target/item 加 `int object_col`；`resolveMutationColumn(ctx, child_layout, var)` 在规划期解析 PEPlan.object_slot → 列下标（无对象列回退变量自身 slot，覆盖 CREATE 后 SET）。
- **`lowerAliasPassthroughOp` 与 `rewriteOp` 必须递归进 Set/Remove/Delete/Merge 的 child**，否则 `WITH n SET ...` 里 WITH Project 的 items[].output_slot/提升不生效，object 列解析不到。

### 12.6 TCK 结果

catD 全量（3897 scenarios）：**3364 passed / 462 failed**（step 级 14925 passed / 462 failed）。
对比 SlotId 重构前基线（gap-abc 988 passed）：净 +2400+ 场景修复。SlotId 重构后无场景级回归——详见 §13.1 加固项与 §13.3 已修复项。剩余 462 failed 为 WIP 分支既有问题（参见 §13.3 待调查项 + 已知非 DPL 引入的 unit test 失败）。


## 十三、技术债与未解决项（2026-07-12）

本节记录落地实况（§12）之后仍未消解的结构性缺陷。**核心契约成立**——SlotId 纯标识、PE 仅追加、图语义前置；下列各项是"契约之外的实现脆弱性"，按是否影响日常开发排序。

### 13.1 已加固（本日修复）

- **求值顺序 UB（§12.4）**：除 PE/CrossProduct/LeftJoin/Project 外，physical_planner.cpp 还有 7 处 `PlanOperatorResult{..., makeSlotLayout(schema, ctx)}` 与 `std::move(schema)` 同处 braced-init 的反模式（Singleton / CorrelatedSource / Aggregate / CreateNode / CreateEdge / Unwind / Merge）。已全部改为先局部构造 layout。这些是潜在地雷——schema 大小一变就突然显形。
- **`resolveMutationColumn` 静默回退**：原实现无 PEPlan 时静默回退到变量自身 slot，掩盖 mutation 集成 bug。已加 spdlog::warn 覆盖两个 bug-signal 路径（PEPlan 存在但 object_slot_id INVALID；解析 slot 不在 child layout 中），让回归可观测。
- **Pass 递归策略分散**：`collectOpReqs` / `collectSourceTypesOp` / `lowerAliasPassthroughOp` / `rewriteOp` 各自维护"哪些 op 递归 child"——已撞过墙（Set/Remove/Delete 漏递归导致 mutation 不生效）。已抽取 `forEachChild(op, visit)` 模板统一处理，递归点收敛到每个 Pass 末尾一次调用。新增 op 现在只需在 `forEachChild` 加一处。
- **lowerAliasPassthroughOp 从 bottom-up 改为 top-down**：配合 forEachChild 重构，Project 处理顺序从"先 child 后 self"改为"先 self 后 child"。分析：Project 的 items[].output_slot 计算和 flat columns 追加依赖全局 plans / name_to_slot（已由 allocateAllSlots 预分配），不依赖 child Project 的 Pass 结果；top-down 与 bottom-up 行为等价。验证：TCK step-level PASSED/FAILED 与改动前完全一致（14907/462）。

### 13.2 暂未处理（按风险排序）

#### A. 变量遮蔽根因——binder 不分配新 SlotId

**现象**：`WITH a.name AS a` 让 binder 复用 `a` 的 SlotId，但语义已从 VERTEX 变为 NAME 标量。Pass 收到的是"同一个 slot 现在是标量"。

**当前补丁**：`lowerAliasPassthroughOp` 对 forward item 加 type gate——只有 `VERTEX/EDGE/VERTEX_REF/EDGE_KEY` 才走 PEPlan 提升路径。这掩盖了根因。

**根本修复**：让 binder 在绑定 alias / 遮蔽时分配新 SlotId（旧 slot 不复用）。这是 binder 改动，超本重构范围。

**风险**：低。type gate 已经覆盖已知 case；只在出现新的遮蔽模式（binder 发出非图类型 forward）时才会漏。

#### B. `__fwd_*` 与 `__pe_*` 列命名空间未隔离（已修复 2026-07-15）

**现象**：PE 追加列命名 `__pe_<slot>`，passthrough 追加列命名 `__fwd_<tag>_<kind>_<lid>_<pid>`。下游必须靠前缀忽略。命名前缀是约定，不强制；user 变量名理论上可能与内部列名碰撞（虽然实际很难发生）。

**修复**：在 `SlotId` 上做区间划分——`slot_id.hpp` 引入 `kInternalSlotFlag = 0x80000000U`（bit 31）。用户 slot ∈ `[1, 2^31)`，内部 slot ∈ `[2^32, 2^32)`。`SlotAllocator` 分两条流：`next()` 给用户、`nextInternal()` 给内部。`isUserSlot(s)` / `isInternalSlot(s)` 提供结构判定。

`buildExtractionInfo` 中 5 处 PE 列 slot 分配（`object_slot_id` / `prop_slot_ids` / `edge_prop_slot_ids` / `labels_slot_id` / `type_slot_id`）全部改用 `nextInternal()`。其他 `alloc.next()` 调用（`ensureSlot` / `lowerAliasPassthroughOp` 的 derived-column alias / `makeSlotLayout` fallback / Project per-item fallback）保持不变——它们分配的是用户可见变量名对应的 slot。

**当前尚未利用的能力**：partition 是结构性保证，但 read 路径（RETURN *、`findColumn`）暂未消费 `isUserSlot()` —— RETURN * 走 binder 的 `symbols`（lowering 之前已经定型，不包含内部列）；mutation op 的 `findColumn` 按用户变量名查找（与内部列名 `__pe_*` 不会碰撞）。后续若出现需要按"是否内部"过滤的场景，可以直接用 `isInternalSlot(slot_id)` 判断，无需依赖命名前缀。

412/412 QueryExecutorTest（含新增 `SlotAllocatorPartition` 用例）、84/84 OptimizerTest 通过。

#### C. `var_slots` (NameSlotMap) 与 `alias_map` 双重事实来源（已修复 2026-07-15）

**现象**：mutation-op 按名解析需 `var_slots`（name→slot）+ `alias_map`（alias→canonical）+ `getCanonicalSlot()` 三者协同。任一处漏更新（如别名透传后 canonical 链断裂），下游静默拿到错列。修复前 `collectExprReqs` / `collectOpReqs` / `collectSourceTypesOp` / `rewriteExpr` / `lowerAliasPassthroughOp` 各有自己的 `canonicalOfName` lambda 重复组合 `slotForName + getCanonicalSlot`，新增 Pass 复制该 pattern 时若漏掉 canonical 步骤就会在 alias slot 上 keying 而不是 root。

**修复**：抽取 `SlotResolver` 只读视图（持有 `NameSlotMap const&` 与 `AliasSlotMap const&`），对外暴露 `slotForName(name)` / `canonicalOf(slot)` / `canonicalForName(name)`。所有 read 路径（含 Pass 内部 helper）改走 `resolver.canonicalForName(...)`，map 的直接访问权收回给三个 writer：`allocateAllSlots` / `lowerAliasPassthrough` / `collectAliasSlotMap`（这三个是"lookup-or-allocate" / "alias-map 写入"语义，必须直接读写 map）。`PlanContext::resolver()` 提供 `(var_slots, alias_map) → SlotResolver` 的便捷封装。

**关键不变量**：`alias_map` 的 value 必须出现在 `name_to_slot` 的 value 集合中（即 alias 指向的 canonical 必须是一个真实存在的 slot）。这条规则以前散落在各调用点的注释里，现在通过 SlotResolver 的"读取必经之路"性质从结构上保证——新增 read 路径无法绕过 canonical 步骤。

411/411 QueryExecutorTest、84/84 OptimizerTest 通过。

#### D. `BoundProjectOp.output_slots` 与 `items` 是 parallel array（已修复 2026-07-14）

**现象**：output_slots 是 ProjectItem 之外外挂的 vector，任何 items 变动都需要手动同步 output_slots，否则 layout 错位。

**修复**：合并到 `ProjectItem::output_slot`（`SlotId`，默认 `INVALID_SLOT_ID`）。删除 `BoundProjectOp::output_slots` parallel array。

- `lowerAliasPassthroughOp`（column_rewrite.cpp）：原 `out_slots.push_back(...)` 改为直接写 `v->items[i].output_slot = ...`；appendAliasPassthroughItem 内部设置 `item.output_slot = source_slot`。
- `physical_planner.cpp`：从 `v.items[i].output_slot` 读取（保持原 per-item fallback 语义——`INVALID_SLOT_ID` 时按名查 `var_slots` 或新分配）。
- `memo.cpp::copyIn`：克隆 ProjectItem 时拷贝 `output_slot` 字段。

**关键陷阱（曾引入 65 个 TCK 回归）**：物理规划器初版改写把原"per-item fallback"误改成"all-or-nothing"——任一 item 的 output_slot 为 INVALID 就对全部 item 走 makeSlotLayout，导致带 PEPlan 提升的 graph 变量列被强制按名重分配、丢失 object_slot。修复回到 per-item fallback：每个 item 独立判断 INVALID 后回退，与其余 item 解耦。

314/314 QueryExecutorTest、10/10 MemoTest 通过。TCK 因测试基础设施抖动（同一 binary 两次运行 158 翻转），scenario-level 对比无意义；step-level 与设计意图一致。

#### E. `allocateAllSlots` 是 binder 之后的补丁

**现象**：SlotId 全局唯一，但分配入口是绑定后的预 Pass（不是 binder 自己）。这意味着别名 / 遮蔽 / 派生列的 slot 都是事后推导。

**修复方向**：让 binder 在绑定时分配所有 slot（含 alias、shadowed、derived）。这会简化 collectAliasSlotMap / collectSourceTypesOp（不再需要从 BoundTree 反推）。但 binder 改动大，超本重构范围。

**风险**：低。当前补丁工作良好，只是增加了 Pass 复杂度。

#### F. Pass 内表达式 visitor 也可集中化（已修复 2026-07-15）

**现象**：`collectExprReqs` / `rewriteExpr` / `ensureSlotsInExpr` 三个表达式 visitor 各自维护"哪些 variant 要递归 child"列表，新增表达式类型时容易漏。

**修复**：抽取 `forEachSubExpr(expr, visit)` 模板，统一覆盖 BinaryOp / UnaryOp / FunctionCall / List / Map / Case / LabelCast / Subscript / Slice / ListComprehension / All / Any / None / Single 等"具备一致 child-递归语义"的 variant。三个 visitor 都改走 `forEachSubExpr` 做 child 递归，std::visit 只保留 per-variant 的 leaf 工作。

**未纳入 `forEachSubExpr` 的两个 variant**：`BoundPropertyRef` 与 `BoundDynamicPropertyRef`——它们对 `object` 子节点的处理在不同 visitor 中语义不一致：`collectExprReqs` 对 PropertyRef 必须 **不**递归 object（否则会在已经收集过 prop req 的变量上又叠加 `need_whole_vertex`），而 `rewriteExpr` / `ensureSlotsInExpr` 则按 covering/flat 情形在 leaf 中内联处理 object。两者由各 visitor 显式处理。

**`collectExprReqs` 的 loop_var_skip**：list comprehension family（ListComp / All / Any / None / Single）会引入新作用域变量。原实现把 `ptr->variable` 作为 skip 参数显式向下递归。重构后：std::visit 在该 variant 分支里更新局部 `current_skip = ptr->variable`，随后 `forEachSubExpr` 的 lambda 按 ref 捕获 `current_skip`，自然获得更新后的值。语义不变。

412/412 QueryExecutorTest、84/84 OptimizerTest 通过。

### 13.3 待调查项

- **MERGE list 属性回归（已修复 2026-07-12）**：`MERGE (a)-[r:T {numbers:[42,43]}]->(b)` 建重复边。**根因**：`create_edge_physical_op.cpp::valueToPropertyValue` 缺少 `ListValue` 分支（与 `create_node_physical_op.cpp` / `merge_physical_op.cpp` 中同名函数相比），导致 CREATE EDGE 时 list 属性被存为 `PropertyValue::monostate`。MERGE 读取时拿到 monostate 与 ListValue 比较失败，判定为"无匹配边"→ 触发重复创建。**修复**：在 `create_edge_physical_op.cpp::valueToPropertyValue` 补齐 ListValue → `vector<int64_t>/<double>/<string>` 转换（与另外两处一致）。Merge5.feature scenario [15] 通过；314→315 passed / 20→19 failed。
- **26 个 unit test 失败**：集中在 Merge / PathPredicate / MultiLabel / UnlabeledNode 区域。经 TCK 对比（这些区域净 0 回归）确认为 WIP 分支既有问题，非 SlotId 重构引入。

### 13.4 已知技术债：valueToPropertyValue 三处重复（已修复 2026-07-14）

`create_edge_physical_op.cpp` / `create_node_physical_op.cpp` / `merge_physical_op.cpp` 此前各自在匿名 namespace 维护一份 `valueToPropertyValue(const Value&) -> PropertyValue`，逻辑高度重复（仅 create_edge 此前漏了 ListValue 分支，导致 13.3 第 1 项 bug）。

**修复**：合并到 `query/physical_plan/operator/property_value_convert.hpp`，与已有的 `propertyValueToValue`（反向转换）共置一处。三处 cpp 删除本地副本，改 `#include` 复用。以 `create_node` 的版本（最完整，含 DateTime/Time/Duration list 分支）为基线，**顺带修复了** create_edge / merge 此前对 temporal list 静默落盘 monostate 的 bug。

817/817 非 TCK 单元测试全部通过。

### 13.5 裸 CREATE () 不可见 bug（已修复 2026-07-16）

**现象**：`CREATE ()`（无标签、无属性）执行后，`MATCH (n) RETURN n` 与 `MATCH (n) RETURN count(n)` 都看不到该节点（前者 0 行、后者 0）。但 `CREATE ({x:42})`（带属性）能被正常找到。

**根因**：binder 在 `bind_mutation.cpp` 对无标签 pattern 尝试把 `__anon__` 标签 id 压进 `label_ids`，但如果 catalog 此时还没有 `__anon__`（即本次会话还没建过任何 anon 属性），`getAnonLabelId()` 返回 `INVALID_LABEL_ID`，于是 `label_ids` 留空。CreateNode 物理算子的 Phase 1b（auto-register __anon__）仅在 `pending_props_` 非空时触发；裸 CREATE 的 `pending_props_` 为空，因此 __anon__ 不会被自动创建，`label_ids_` 也保持空。后续 `buildLabelProps` 返回空向量，`insertVertex` 内的 for 循环不执行，**没有任何 label-reverse / label-fwd key 被写入**——节点在存储层根本不存在，AllNodeScan 自然扫不到。

**修复**：CreateNode 的 Phase 1b 触发条件由 `!pending_props_.empty()` 放宽为 `!pending_props_.empty() || label_ids_.empty()`；进入分支后，若 `label_ids_` 仍为空则把 anon_lid 压进去（同时 `store_.createLabel(anon_lid)` 保证数据表存在）。这样裸 CREATE 也会产出 `(anon_lid, Properties{})` 的 label_props，`insertVertex` 写入 label-reverse key，节点即可被 AllNodeScan / getVertexLabels 发现。

注意：`label_name_to_id`（传给 AllNodeScan 的 `label_map_`）由 `QueryExecutor` 直接从 `listLabels()` 构造，**不**像 catalog 那样剔除 `__anon__`，所以 __anon__ 节点对 AllNodeScan 可见。

413/413 单元测试通过（含新增 `BareUnlabeledNodeCreateAndMatchReturn`、`WithAliasCreateUsesBoundNode`）。

### 13.6 CreateNode/CreateEdge 丢失上游 slot 重映射（已修复 2026-07-16）

**现象**：`MATCH (n) WITH n AS a CREATE (a)-[:T]->() RETURN a` 在 RETURN 时报 `BoundColumnRef name='a' idx=2147483649 but input has 3 columns`，结果行返回 monostate（非 VertexValue）。CI 报告该 scenario (TCK Create3 [6]) 在 SlotId 重构后回归。

**根因**：`WITH n AS a` 的 Project pass 把 `items[i].output_slot` 设为 `pi.object_slot_id`（一个内部 slot，如 `0x80000001`），同时**故意不**写回 `name_to_slot["a"]`（注释在 `column_rewrite.cpp:1097-1102`，理由是 PE dispatch 依赖 base 变量稳定的 name→slot）。Project 物理算子在 `physical_planner.cpp:1327-1336` 正确地用 `items[i].output_slot` 构造输出 layout。

但下游 CreateNode (line 1514) 和 CreateEdge (line 1586) 的输出 layout 调用 `makeSlotLayout(node_schema, ctx)`，**按名字**重新查 `ctx.var_slots`。`var_slots["a"]` 仍是原始 user slot，于是 CreateNode 输出 layout 中 `a` 的 slot 与上游 Project 输出不一致。后续 CreateEdge、顶层 Project 继承这个错误 layout，ExpressionCompiler 查 slot `0x80000001` 找不到 → 保留 bind-time 的 column_index（= slot_id 值本身）→ 求值时越界。

**修复**：CreateNode / CreateEdge 不再 `makeSlotLayout`，改为**继承 child 的 slot_layout**、仅 append 新增列的 slot。这样 passthrough 列保留上游 Project 设定的 slot，下游 ExpressionCompiler 才能查到。

415/415 单元测试通过（含新增 `TckCreate3Scenario6WithLabel`）。

### 13.7 BoundDynamicPropertyRef 错误触发 whole-object PEPlan（已修复 2026-07-16）

**现象**：`UNWIND $props AS prop MERGE (p:Person {login: prop.login}) SET p.name = prop.name RETURN p.name, p.login` 报 `BoundColumnRef name='prop' idx=2147483649 but input has 2 columns`，p.name 全部返回 null（TCK Unwind1 [14] 回归）。

**根因**：binder 对 ANY 类型的对象（包括 MAP）会创建 `BoundDynamicPropertyRef`（`bind_expression.cpp:497-505`），让 runtime evaluator 处理 VertexValue/EdgeValue/MapValue 三种情况。但 `collectExprReqs` 在 `column_rewrite.cpp:441-453` 看到该 ref 时，**仅凭 result_type 不是 EDGE** 就武断设 `need_whole_vertex = true`。这给 `prop`（一个 MAP 变量）分配了 PEPlan，且 `buildExtractionInfo` 因 `need_whole_vertex` 给它分配 `object_slot_id = nextInternal() = 0x80000001`。随后 `rewriteExpr` 的 BoundColumnRef 分支（line 912-922）查到该 PEPlan，把 `prop` 的 slot_id 和 column_index **都**重写成 `object_slot_id`。下游算子的 slot_layout 里没有这个内部 slot，ExpressionCompiler 查不到 → 保留 bind-time column_index（已被覆写成 slot_id 值本身）→ 求值时越界。

**修复**：`collectExprReqs` 对 BoundDynamicPropertyRef 改为按 **object 的静态类型** 判定（VERTEX/VERTEX_REF → whole_vertex；EDGE/EDGE_KEY → whole_edge；MAP/ANY 等不设）。Runtime evaluator (`eval_property.cpp:evalDynamicPropertyRef`) 已对 VertexValue/EdgeValue/MapValue 三种 variant 分别处理，非图实体无需 PE 构造。

416/416 单元测试通过（含新增 `TckUnwind1Scenario14UnwindWithMerge`）。

### 13.8 UnwindPhysicalOp 列索引未跟上 PE 追加（已修复 2026-07-16）

**现象**：`MATCH (n) UNWIND keys(n) AS x RETURN DISTINCT x` 只返回 1 行（应为 2 行），且 `x` 值为 monostate。多个 TCK 场景回归（Graph8 keys 系列、Projecting map/list、Collect and filter、Counting matches per group 等涉及 UNWIND 的场景）。

**根因**：`UnwindPhysicalOp` 接收 `output_col_index_` 来自 binder 的 `BoundUnwindOp::variable_column_index`。该索引在 bind 时按 `nextColumnIndex()` 顺序分配（例如 n=0、x=1）。但物理规划阶段，`dispatchProjectionExtract` 在 child 输出末尾**追加**了 PE 列（如 `n_object`），实际物理布局变成 `[n, n_object, x]`。bind-time 的 `variable_column_index=1` 指向了 `n_object`（VertexValue）而非新增的 x 列。结果 `output.columns[1].setValue(row, string_val)` 实际写入了类型为 VERTEX 的列（setValue 对类型不匹配的值静默丢弃），`x` 列保持 monostate。

**修复**：物理规划器用 `child_schema.size()`（push 前的大小，即新增列在输出中的物理位置）作为 unwind 变量列索引，不再使用 binder 的 stale `variable_column_index`。同一类问题（binder-time 索引 vs PE 后的物理索引）在其他 schema-changing 算子（Project、Aggregate、PathBuild、Expand）上已通过 `makeSlotLayout` 或显式 slot append 处理，Unwind 是漏网之鱼。

417/418 单元测试通过（仅 `TckTypeConversion3Scenario6FailToFloatOnNode` 期望 TypeError 尚未实现，与本次回归无关）。

### 13.9 Aggregate 顶层 PE 列泄漏到用户输出（已修复 2026-07-16）

**现象**：`MATCH (a:L)-[rel]->(b) RETURN a, count(*)` 返回 3 列（`[a:VERTEX_REF, __pe_<id>:VERTEX, count(*):INT64]`），用户期望 2 列。`a` 列还是 VERTEX_REF（topology stage），未升级为 VERTEX（semantic stage），用户实际看到内部 node id 而非 VertexValue。

**根因**：`bindReturn` 的 `has_aggregate` 分支仅当存在复杂聚合表达式（如 `count(a)+3`）时才在 `BoundAggregateOp` 上方构造 `BoundProjectOp`。对纯简单聚合（`RETURN a, count(*)`），`AggregateOp` 直接作为顶层算子，导致：

1. 物理规划器在 `AggregateOp` 之后调用 `dispatchProjectionExtract`，按 `need_whole_vertex[a]` 在输出末尾**追加** `__pe_<id>:VERTEX` 列（append-only 设计）。该列对用户不可见，应被顶层 Project 过滤掉。
2. `AggregateOp` 的 `a` 列类型元数据是 `VERTEX`（来自 `getBoundExprType(group_key)`），但运行时值仍是 VERTEX_REF（从 child 拷贝而来）。没有顶层 Project 引用 `__pe_<id>` slot，用户直接看到 VERTEX_REF。

**修复**：`bindReturn` 的 `has_aggregate` 分支**始终**在 `BoundAggregateOp` 上方构造 `BoundProjectOp`，items 包含所有用户可见的 RETURN 项（group key / 简单聚合 / 复杂聚合表达式）。这样：

- Project 的 `collectExprReqs` 正确设置 `need_whole_vertex` / `need_whole_edge`，PE 在 Aggregate 之后追加 `__pe_<id>` 列。
- Project 的 BoundColumnRef 经 column-rewrite pass（`BoundColumnRef` case）改写指向 `__pe_<id>` slot，运行时读到的就是构造好的 VertexValue。
- `__pe_<id>` 与 `__agg_*` 内部列因为没有 ProjectItem 引用而被自然过滤。

同时修了一个相关 bug：binder 给 Aggregate 输出列构造 `ColumnInfo` 时未设 `slot_id`（默认 `INVALID_SLOT_ID`），导致顶层 Project 的 passthrough `BoundColumnRef.slot_id = INVALID_SLOT_ID`，ExpressionCompiler 无法映射到列索引，运行时 fallback 到 binder-time `column_index`（stale），从错误的列读值。修复：复用已有变量的 slot（如 group key `a` 复用 MATCH 分配的 slot），新变量（如 `count(*)`）调用 `nextSlotId()` 分配。

### 13.10 ExpressionCompiler 遗漏 BoundList/BoundMap/BoundSlice（已修复 2026-07-16）

**现象**：类型转换函数（`toFloat`/`toInteger`/`toString`/`toBoolean`）在收到非法类型参数时应该抛出 `TypeError: InvalidArgumentValue`，但 list comprehension 中的转换调用正常返回（不报错）。例如：

```
MATCH p = (n)-[r:T]->() RETURN [x IN [1.0, n] | toFloat(x)] AS list
```

`toFloat(n)`（n 是节点）应抛出 TypeError，但实际返回了 `[1.0, null]`。

**根因**：`ExpressionCompiler` 的 `compileOne` 模板函数处理了 `BoundExpression` variant 的 17 种类型中的 14 种，唯独遗漏了 `BoundList`（`[1.0, n]`）、`BoundMap` 和 `BoundSlice`。列表元素 `n` 的 `BoundColumnRef.column_index` 仍保持着 column_rewrite pass 写入的 `__pe_<id>` slot（约 `0x80000001`），运行时 `evaluateInternal` 发现该 column_index 超出 input.columns 范围，放弃读取实际列值，改为创建临时的 ANY 列（默认 monostate），导致 `toFloat(null)` 返回 null 而非报错。

**修复**：
1. 在 `ExpressionCompiler::compileOne` 新增 `BoundList`、`BoundMap`、`BoundSlice` 三个 case，递归编译其子表达式。现在 variant 的所有 20 种类型均被覆盖。
2. 在 `execSync`（测试辅助函数）新增 try/catch，将运行时异常（如 `toFloatImpl` 抛出的 `std::runtime_error`）捕获并写入 `result.error`，使测试能通过 `EXPECT_FALSE(result.error.empty())` 验证错误。

### 13.11 BoundScanOp/BoundLabelScanOp 缺 slot_layout（已修复 2026-07-17）

**现象**：`MATCH () CREATE (n) DELETE n` 在基线通过，本分支回归（Delete4 [3] 场景，`no side effects` 步骤失败）。具体表现：查询执行后图里的节点数从 1 增加到 2，说明 DELETE 没有删除 CREATE 刚创建的新节点。

**根因**：`physical_planner.cpp` 中 `BoundScanOp`、`BoundLabelScanOp` 分支构造 `PlanOperatorResult` 时**未传入 slot_layout**，默认空 layout 被下游 `CreateNode`/`CreateEdge` 通过 `std::move(child_layout)` 继承，只追加了新变量本身的 slot。这导致：

- CreateNode 的物理输出是 `[anon_node_col, n_col]`（2 列）；
- 但其 `TupleSlotLayout` 只包含 `[n_slot]`（1 项，对应 layout 中 index 0）；
- 下游 `DeletePhysicalOp` 通过 `resolveMutationColumn(ctx, cr.slot_layout, "n")` 解析，`layout.getColumnIndex(n_slot)` 返回 0；
- DELETE 从 column 0 读出的是匿名 VertexRef（而非新构造的 VertexValue），被 `!std::holds_alternative<VertexValue>` 静默跳过，节点从未删除。

**修复**：在两个 scan 分支调用 `makeSlotLayout(output_schema, ctx)` 构造 layout，让 SlotId → column_index 映射与实际物理列对齐。

**回归守卫**：`QueryExecutorTest.TckCreateAndDeleteSameQuery`（tests/test_query_executor.cpp）通过对比查询前后的 `MATCH (n) RETURN count(n)` 验证净副作用为 0。

修复后 TCK step 对比 Testing/tck-after-slotid-fwdfix.json：**0 回归，268 改善**。

### 13.12 多层 WITH 别名链产生 alias_map 循环（部分已修复，2026-07-17 → 2026-07-18）

**现象**：`WITH7` scenario 1/2（多层 `WITH ... AS ...` 后再消费）返回 0 行或 null。具体表现：`MATCH (a:A)-[r:REL]->(b:B) WITH a AS b, b AS tmp WITH b AS a RETURN a` 中 `a` 返回 monostate（应为 VertexValue）。

**根因**：slot_id 在 `allocateSlotsInOp` 里按"名字"分配（设计妥协，§12.1），同一名字在不同 scope 里复用同一个 slot_id。`collectAliasSlotMapOp` top-down 遍历时，"外层 WITH2 (`b AS a`)" 与 "内层 WITH1 (`a AS b`)" 各自的别名关系都被记录到全局 `alias_map`：

```
WITH2: alias_map[SLOT_A] = SLOT_B   // 外层先处理
WITH1: alias_map[SLOT_B] = SLOT_A   // 内层后处理，key 不冲突，插入
```

形成循环 `SLOT_A ↔ SLOT_B`。`getCanonicalSlot` 的循环检测返回"起点 slot"（§13.12 流程详），导致：

- `canonicalForName("a")` 在 WITH2 的语境返回 SLOT_A（看似对）；
- `canonicalForName("b")` 在 WITH2 的语境也返回 SLOT_B（错，应是 SLOT_A，因为 `b` 是 `a` 的别名）。

`lowerAliasPassthroughOp` 处理 WITH2 的 `b AS a` 时，根据错的 canon 去查 PEPlan，找不到（plan 只在 SLOT_A 上），落到 `out_slot = alias_own = SLOT_A`。WITH2 Project 的输出 layout = `[SLOT_A]`，但它的 child WITH1 输出 layout = `[SLOT_A_OBJ, SLOT_TMP]`（PEPlan 已把 `a` 提升到 SLOT_A_OBJ）。下游 ExpressionCompiler 查 SLOT_A 找不到列，fallback 到 stale bind-time column_index，读到 monostate。

**为什么 §13.2-A 的"变量遮蔽"type gate 不能兜住这个 case**：§13.2-A 是 `WITH a.name AS a`（VERTEX → NAME 标量，类型变了），type gate 通过 `isGraphKind(fwd_kind)` 拦截。本 case 是 `WITH a AS b`（VERTEX_REF → VERTEX_REF，类型没变），type gate 放行，PEPlan 提升逻辑跑下去才暴露问题。

**完整流程**：见本仓库 commit history 中 `tests/test_query_executor.cpp::TckWith7Scenario1BoundEndpoint` 的 step-by-step debug 输出（注释里有详细 slot 分配 + alias_map + layout 流转）。

**待实施的修复方案：scope-aware alias_map**

放弃全局 `alias_map`，改为 per-Op 局部别名作用域。每个 `BoundProjectOp` 拥有自己的 `OpAliasScope{alias_slot → input_src_slot}`，描述"我的输出 scope 里 alias X 等于输入 scope 里的 src Y"。canonical 解析时按需自顶向下（或自底向上）遍历 Project 链，逐层应用局部映射，天然隔离 scope。

```cpp
struct OpAliasScope {
    std::unordered_map<SlotId, SlotId> output_alias_to_input_src;
};
std::unordered_map<const BoundLogicalOperator*, OpAliasScope> op_alias_scopes_;

// 解析：从某个 op 起，沿 child 链向下，逐层套用 OpAliasScope
SlotId resolveCanonicalAtOp(const BoundLogicalOperator* op, SlotId slot) {
    SlotId cur = slot;
    while (op) {
        auto it = op_alias_scopes_.find(op);
        if (it != op_alias_scopes_.end()) {
            auto a = it->second.output_alias_to_input_src.find(cur);
            if (a != it->second.output_alias_to_input_src.end())
                cur = a->second;
        }
        op = getLinearChild(op);  // 跟随单 child 链
    }
    return cur;
}
```

**实施成本**：§13.1 已经把递归集中到 `forEachChild`，改 `collectAliasSlotMapOp` / `lowerAliasPassthroughOp` / `collectOpReqs` / `collectSourceTypesOp` / `rewriteOp` 五个 visitor 的 signature（增加"当前 op"参数或预计算 composed map 自底向上传），工程量"低-中"。

**当前未修复原因**：
- 本 case 涉及的设计契约是"slot_id 全局唯一性"（§2.1）。彻底修复需要要么改 slot 分配（§13.2-A 标注超范围），要么改 alias_map 数据结构（本节方案）。
- 用户决定：本节方案记录为设计 doc，**先修其他 TCK 回归**，本 case 留待后续 phase。

**临时 workaround**：暂无。受影响场景：所有"多层 WITH rename 后再消费"的查询（`WITH7`、部分 `Merge5` 别名场景）。预计 18+ TCK 步受影响。

**风险**：低。当前实现不会 crash，只是返回 monostate/0 行。修复方案明确，不影响其他场景。

**相关文件**：`src/query/optimizer/column_rewrite.cpp`、`src/query/physical_plan/physical_planner.cpp`、`src/query/physical_plan/slot_layout.hpp`

### 13.12.1 别名循环根因再审视 + 两方案对比（2026-07-17）

本节最初提出"per-Op scope"方案（方案 B）。在落地前再审视代码，发现**根因不在 alias_map 数据结构，而在 slot 分配**。下面是两个候选方案的对比，选其一实施。

#### 病根定位

`allocateSlotsInOp` 的 BoundProjectOp 分支（`column_rewrite.cpp:282-290`）：

```cpp
for (auto& item : v->items) {
    ensureSlotsInExpr(item.expr, name_to_slot, alloc);
    if (!item.alias.empty())
        ensureSlot(name_to_slot, alloc, item.alias);   // ← 同名 alias 复用现有 slot
}
```

`ensureSlot` 幂等——同名返回旧 slot。`WITH a AS b`（输入已有 b）时，输出 `b` 拿到输入 `b` 的 slot。但 Cypher 语义下两者是不同 scope 的不同实例。两个 Project 共享 slot 才产生 `slot_a ↔ slot_b` 循环。

**alias_map 循环是症状，slot 复用是病根。**

#### 方案 B：per-Op scope（原方案）

**思路**：保留 slot 分配现状，改 `alias_map` 数据结构——每个 Project 持有自己的 `OpAliasScope{output_slot → input_src_slot}`，visitor 解析时按需自顶向下沿 child 链应用局部映射。

**实施成本**：改 5 个 visitor（`collectAliasSlotMapOp`/`lowerAliasPassthroughOp`/`collectOpReqs`/`collectSourceTypesOp`/`rewriteOp`）的签名，新增"composed scope"传播机制。需要明确哪些 op 透传 scope（Project / Filter / Sort / Aggregate / Set / Remove / Delete / Merge / Create / Unwind / Expand / PathBuild / Skip / Limit / Distinct）、哪些打断（无；只有 Project 自身定义新 scope）。

**优点**：
- 把每个 Project 的别名关系局部化，scope 边界清晰
- 不改 slot 分配契约

**缺点**：
- 工程量大（5 visitor × 多个 op variant × 输入/输出两侧 scope）
- 必须明确"我现在在 Project 输入侧还是输出侧"——`collectOpReqs` 在处理 `item.expr` 时用输入 scope、处理 `item.alias` 时用输出 scope，容易出错
- 把"slot 分配错了"的病根绕过去，治症不治本
- With7[1] 同名重绑定（`WITH a AS b` 输入 b 与输出 b 同名）需要 OpAliasScope 用 slot 级映射，但 `NameSlotMap` 仍是单一全局映射，需连带改

#### 方案 B'：修 slot 分配（推荐）

**思路**：保留全局 `alias_map` 不变，只改 `allocateSlotsInOp` 的 BoundProjectOp 分支——给每个 Project 的输出 alias 强制分配新 slot，覆盖 `name_to_slot[alias]`。

```cpp
} else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
    if (v) {
        allocateSlotsInOp(v->child, name_to_slot, alloc);   // 先 child（输入 scope）
        for (auto& item : v->items) {
            ensureSlotsInExpr(item.expr, name_to_slot, alloc);  // item.expr 在输入 scope
            if (!item.alias.empty()) {
                binder::SlotId fresh = alloc.next();            // ← 强制新 slot
                name_to_slot[item.alias] = fresh;               // ← 输出 scope 覆盖
            }
        }
    }
}
```

**为什么这能根治循环**：每个 Project 输出 alias 拿独立 slot，`alias_map[dst]=src` 链天然有向无环。With7[1] 的两层 WITH 链变成：

```
MATCH:        slot_a_in  (a)        slot_b_in  (b)        slot_r_in (r)
WITH1:        slot_b_out (b, alias) → src slot_a_in
              slot_tmp_out (tmp)     → src slot_b_in
              slot_r_out  (r, alias) → src slot_r_in
WITH2:        slot_a_out (a, alias) → src slot_b_out
              slot_r2_out (r, alias) → src slot_r_out
```

canonical(slot_a_out) = canonical(slot_b_out) = canonical(slot_a_in) = slot_a_in（无环）。每个 Project 在物理执行时，从 `item.expr` 的 BoundColumnRef.slot_id 出发，按输入 scope layout 取列；输出写到 `item.output_slot`（强制新 slot）。

**优点**：
- 改动面小（单函数单分支，约 5 行）
- 不动 5 个 visitor 签名
- alias_map 保持全局，canonical 解析语义不变
- 治本——直接消除循环可能

**风险与待验证项**：
1. **slot 一致性契约**：binder 阶段 `ColumnInfo.slot_id = nextSlotId()`（`bind_pattern.cpp:144`）已给 alias 分配 slot。optimizer 的 `allocateSlotsInOp` 是否覆盖了 binder 的分配？如果是，本方案生效；如果不是，需同时改 binder。
2. **下游依赖**：`BoundProjectOp.items[i].output_slot`（由 `lowerAliasPassthroughOp` 计算）依赖 `name_to_slot[alias]`。强制新 slot 后，`lowerAliasPassthroughOp` 的 `out_slot` 计算仍能正确指向新 slot（其逻辑就是从 `slotForName(alias)` 取，名字解析到新 slot 即可）。
3. **`item.expr` 里的 BoundColumnRef("b")**：在 `WITH a AS b` 的 Project 中，item.expr 里若引用了"输入 scope 的 b"（来自 child 的输出），其 `BoundColumnRef.slot_id` 应是 child 的 slot_b_in，而非新分配的 slot_b_out。当前 binder 设置 slot_id 的位置需审计。
4. **物理 Project 的列布局**：Project 物理执行时，对每个 input 列（child 输出）+ 每个 output 列（item.output_slot）建 layout。强制新 slot 后，input layout 仍是 child 的 layout（含 slot_b_in），output layout 包含 slot_b_out。两者通过 alias_map 关联。

#### 选择建议

倾向**方案 B'**：

- 治本（消除循环可能），不绕过病根
- 改动面小、风险可控
- 若验证发现 binder 阶段也分配了 slot 且无法绕过，可退化为方案 B（per-Op scope）

但需要先做**第 0 步：审计 binder 的 slot 分配**，确认 optimizer 的 `allocateSlotsInOp` 是否覆盖了 binder 的 `ColumnInfo.slot_id`，以决定是否需要同时改 binder。

#### 实施步骤（选定方案后）

1. **审计（最小代码改动）**：grep `slot_id =` / `nextSlotId()` 全文件，列出所有 slot 分配点。确认 optimizer `allocateSlotsInOp` 的覆盖关系。
2. **写 8 个失败场景的单元测试**到 `tests/test_query_executor.cpp`，作为回归守卫。
3. **实施方案 B'**（或 B）：单点修改 `allocateSlotsInOp`（或 5 个 visitor）。
4. **跑 unit test + 全量 TCK**，确认 8 个场景修复且无新回归。
5. **更新本文档**，把 §13.12 从"待修复"改为"已修复"，补充根因和方案对比。

### 13.12.2 方案 B' 实施结果（2026-07-18）

按 §13.12.1 选定的方案 B'（修 slot 分配）实施。本节记录实际落地代码、设计反思与回归结果。

#### 落地代码

**改动 1：`ProjectItem` 加 `input_slot` / `alias_slot` 两字段**（`bound_project_op.hpp`）

```cpp
struct ProjectItem {
    BoundExpression expr;
    std::string alias;
    BoundType result_type;
    SlotId output_slot = INVALID_SLOT_ID;  // 已有：物理 Project 输出槽（PE 重新定向）
    SlotId input_slot  = INVALID_SLOT_ID;  // 新增：src 在输入 scope 的槽
    SlotId alias_slot  = INVALID_SLOT_ID;  // 新增：本 Project 为 alias 分配的槽
};
```

**改动 2：`allocateSlotsInOp` ProjectOp 分支**（`column_rewrite.cpp`）

```cpp
if (v) {
    allocateSlotsInOp(v->child, name_to_slot, alloc);    // 先 child（输入 scope）
    // Pass 1: 补全输入 scope + 捕获 input_slot（任何覆写之前）
    for (auto& item : v->items) {
        ensureSlotsInExpr(item.expr, name_to_slot, alloc);
        std::string src_name = forwardRefName(item.expr);
        item.input_slot =
            src_name.empty() ? binder::INVALID_SLOT_ID : slotForName(name_to_slot, src_name);
    }
    // Pass 2: 分配 alias_slot（透传项复用 input_slot）
    for (auto& item : v->items) {
        if (item.alias.empty())
            continue;
        std::string src_name = forwardRefName(item.expr);
        if (!src_name.empty() && item.alias == src_name && item.input_slot != binder::INVALID_SLOT_ID) {
            item.alias_slot = item.input_slot;   // 透传：复用 src 槽
            continue;
        }
        binder::SlotId fresh = alloc.next();
        name_to_slot[item.alias] = fresh;
        item.alias_slot = fresh;
    }
}
```

**改动 3：`collectAliasSlotMapOp` ProjectOp 分支**——改读 item 字段

```cpp
std::string src_name = forwardRefName(item.expr);
if (src_name.empty() || item.alias.empty() || item.alias == src_name)
    continue;
if (item.input_slot == INVALID_SLOT_ID || item.alias_slot == INVALID_SLOT_ID)
    continue;
alias_map.emplace(item.alias_slot, item.input_slot);
```

**改动 4：`lowerAliasPassthroughOp`**——`alias_own` / `alias_slot` 改读 `item.alias_slot`

#### 关键设计反思（落地才发现的边界）

**单纯"给 alias 分配新 slot"是不够的**。最初版的方案 B' 假设"只要每个 alias 拿独立 slot，alias_map 链就天然有向无环"。但 `collectAliasSlotMapOp` 与 `lowerAliasPassthroughOp` 仍用 `slotForName(name)` 从**全局** `name_to_slot` 取槽——而该 map 在外层 Project 处理时已被覆写，内层 Project 再查 `slotForName("a")` 拿到的是外层 alias 的 slot，仍会形成 `slot_a_out ↔ slot_b_out` 的环。把 `(input_slot, alias_slot)` **直接挂在 ProjectItem 上**，两个 visitor 改读 item 字段，等于把 scope 信息局部化到每条 item——这才是真正等效于 per-Op scope 的最小改动。

**Pass 1 / Pass 2 拆分的关键性**。`WITH a AS b, b AS tmp` 中第二条 `b AS tmp` 的 `b` 在 Cypher 语义下指向**输入 scope** 的 `b`（同一 Project 内所有 item.expr 共享输入 scope）。若不拆 pass，第一条 `a AS b` 覆写 `name_to_slot["b"]` 之后，第二条捕获的 `input_slot` 就成了本 Project 自己刚分配的 `b_out`，错。Pass 1 在任何覆写之前完成所有 `input_slot` 捕获，Pass 2 才统一分配 `alias_slot`。

**透传复用的关键性**。`RETURN friend_count`（隐式 `AS friend_count`）的 alias 名等于 src 名。若一律分配新 slot，则 `name_to_slot["friend_count"]` 在 RETURN 处被覆写为新槽，而 RETURN 物理算子期望从 Aggregate 输出（原槽）读列——找不到，回退到 stale 列号，整条链断裂。透传时复用 `input_slot` 保持名字→槽映射稳定。这个边界曾让初版方案 B' 引入 7 个新回归（WithAggregate / TckLabelsOnAnyType / ReturnPathMixedWithNode 等），加上透传分支后才彻底归零。

#### 回归结果（unit tests）

| 类别 | 通过 | 失败 |
|------|------|------|
| 基线（Plan B' 前） | 425 | 7（§13.12 系列失败） |
| Plan B' 后 | 432 | 5（其中 0 个新回归） |

**已修复**：
- `TckWith7Scenario1BoundEndpoint`（多层 WITH 别名链 + bound endpoint）
- `TckMerge5Scenario16AliasingExistingNodes1`（单层 WITH 别名 + MERGE 直接 RETURN 拓扑）

**未修复（根因不同）**：
- `TckWith7Scenario2MultipleWiths`、`TckMerge5Scenario17/18/19AliasingExistingNodes*`、`TckList12Scenario1CollectAndExtract`
- 现象：`BoundColumnRef idx=2147483650 but input has N columns`（idx = 0x80000002 = PE 内部属性槽）
- 根因：PEPlan 为 RETURN 的属性访问分配的内部槽位没传播到 MERGE / UNWIND / SET 等 mutation 算子的 layout。这是 **PE 槽传播**问题，不是别名循环问题。
- 留作后续 phase 处理。

#### 两方案实际差异（落地后回看）

- **方案 B**（5 visitor 签名链改造）= 完整 per-Op scope 模型，工程量大
- **方案 B'**（ProjectItem 加两字段 + 2 visitor 改读字段）= 把 scope 信息局部化到 item，等效语义，不动 visitor 链

落地代码量：~60 行（含文档化注释），单文件单分支修改。



### 13.13 CREATE/MERGE 已解析属性表达式绕过 DPL 三阶段（已修复 2026-07-17）

**现象**：TCK Match5 [25][26][28][29]（"mixed relationship patterns" 系列）返回 16 个 null 而非期望的 name 字符串。最小复现：

```cypher
MATCH (c1:C) CREATE (x:B {name: c1.name + '0'})
```

创建出的 `x.name` 为 null —— `c1.name` 在 CREATE 属性表达式内求值为 monostate。Merge1 [11]（"MERGE using property of bound node"）同根因。

**根因**：属性表达式在 Bound 算子里分两种存放：

- **已解析**（binder 确定了 label/prop id）：`BoundCreateNodeOp::label_properties`、`BoundCreateEdgeOp::properties`、`BoundMergeOp::{start,edge,end}_prop_filters`；
- **未解析**（按名字延迟解析）：各算子的 `pending_props`。

DPL 三阶段（`allocateSlotsInOp` / `collectOpReqs` / `rewriteOp`）**只遍历了 pending_props**，已解析列表整体缺席：

1. Collect 缺席 → `c1.name` 的 demand 未记录 → 不下发 ProjectionExtract（`MATCH (d:D) CREATE (...d.name...)` 单查询时）；
2. Rewrite 缺席 → 即使因其他消费者存在 PEPlan，CREATE 表达式内的 `BoundPropertyRef` / 引用也未指向 extract slot；
3. 物理层 `CreateNodePhysicalOp::compileExpressions` / `CreateEdgePhysicalOp::compileExpressions` 同样只编译 `pending_props_`，`label_prop_exprs_` / `prop_exprs_` 内的 `BoundColumnRef.slot_id` 从未映射到物理列（运行时告警 `BoundColumnRef idx=2147483649 but input has 2 columns`）。

**修复**（`column_rewrite.cpp` 三个 visitor + 两个物理算子）：

- `allocateSlotsInOp` / `collectOpReqs` / `rewriteOp` 补齐对 `label_properties` / `properties` / `*_prop_filters` 的遍历（Merge 的 `*_pending_props` 同时补进 rewriteOp，此前 collect 了但从未 rewrite）；
- `CreateNodePhysicalOp::compileExpressions` 补编译 `label_prop_exprs_`；`CreateEdgePhysicalOp::compileExpressions` 补编译 `prop_exprs_`（MergePhysicalOp 原本就编译齐全，无需改）。

**回归守卫**：`QueryExecutorTest.TckMatch5Scenario25VarLenChainName`（MATCH+CREATE 属性引用 + var-len chain 结果非 null）。

修复后 TCK：Match5 [25][26][28][29] 通过（剩余 [11][12][13][27] 为基线既有失败）；Merge1 [11] 通过。

### 13.14 keys()/properties() 对 MAP 变量误标 need_whole_vertex（已修复 2026-07-17）

**现象**：TCK Map3 [5] 回归，`WITH {exists: 42} AS map RETURN keys(map)` 返回 null。

**根因**：`collectExprReqs` 对 `keys(x)` / `properties(x)` 的处理是"非 EDGE 即标 `need_whole_vertex`"。当 `x` 是 MAP 类型（WITH 别名的字面量 map）时，Decide 阶段照样生成 ConstructVertex PEPlan，Rewrite 把 `map` 的引用重定向到 object slot —— 而 ProjectionExtract 对着 MapValue 列构造 vertex 得到 null，`keys(null)` → null。

**修复**：读取参数的静态类型 kind；`EDGE → need_whole_edge`，`MAP → 不标任何 demand`（map 列本身即可求值），其余（VERTEX/VERTEX_REF/ANY 等）维持 `need_whole_vertex`。

**回归守卫**：`QueryExecutorTest.TckMap3Scenario5KeysInMap`。修复后 Map3 11/11、Graph8 8/8（keys(n) 于顶点的场景不受影响）。

### 13.15 Plan B' 扩展：PE 槽传播到 mutation 算子 + scope 保护（2026-07-18）

前面 §13.12 的 Plan B' 修复了 `alias_map` 环问题，使 `With7Scenario1` 和 `Merge5Scenario16` 通过。但仍有 3 个 DPL 回归测试失败，根因是 PE 构造列没传播到 MERGE/SET/Remove/Delete 算子。

本阶段在 Plan B' 基础上做了四项补充修复，最终将 6/8 DPL 回归测试变绿（2 个既有失败不在本次修复范围内）。

#### 修复清单

**Fix 1：`collectSourceTypesOp` —— MERGE 对 pre-bound 变量不要标记为 VERTEX**

```cypher
MATCH (n) MATCH (m)
WITH n AS a, m AS b
MERGE (a)-[r:T]->(b)
RETURN a.id AS a, b.id AS b
```

`a` 是 pre-bound 变量（从 WITH 传入，非 MERGE 创建）。旧代码无条件把 `setKind(start_var, VERTEX)`，覆盖了 Scan 分配的 `VERTEX_REF`。`buildExtractionInfo` 看到 `source_already_object=true` 就跳过 `ConstructVertex` 分配。MERGE 收到 `VertexRef` 而非 `VertexValue`，运行时报 `BoundColumnRef idx=... but input has N columns`。

修复：仅对 `!pre_bound` 的变量标记 VERTEX。

**Fix 2：`allocateSlotsInOp` —— Project 非 forward alias 不覆盖 name_to_slot**

```cypher
MATCH (n) MATCH (m)
WITH n AS a, m AS b          -- Project1: src='n'→alias='a', 覆盖 name_to_slot['a']
MERGE (a)-[:T]->(b)
RETURN a.id AS a, b.id AS b  -- Project3: src=''(非forward)→alias='a', 不应覆盖!
```

Project3 的 `a.id AS a` 是计算表达式（BoundPropertyRef），非 forward。旧代码无条件 `name_to_slot[alias] = fresh`，把 Project1 分配的 slot 覆写了。下游 `canonicalForName('a')` 解析到 Project3 的 slot 而非 Project1 的，PE plan 生成在错误的 canonical slot 上。

修复：非 forward alias（`src_name.empty()`）只写 `item.alias_slot`，不更新 `name_to_slot`。Forward rename（`WITH n AS a`，src_name 非空且 ≠ alias）照常更新。

**Fix 3：`collectAliasSlotMapOp` / `lowerAliasPassthroughOp` —— 读取 per-item slot 而非全局 name_to_slot**

配合 Fix 2。这两个 visitor 原先用 `slotForName(name_to_slot, alias)` 从全局 map 查 slot，在 scope 被覆写时拿到错误值。改为读 `item.input_slot` / `item.alias_slot`（在 `allocateSlotsInOp` 中捕获），scope 信息局部化。

**Fix 4：physical planner —— MERGE / SET / Remove / Delete 调用 `dispatchProjectionExtract`**

这些 mutation 算子原先不调用 `dispatchProjectionExtract`，导致 ConstructVertex/Edge 列从未插入子 plan。下游 `resolveMutationColumn` 从 PEPlan 拿到 object slot 但 child layout 里找不到。修复：在四个算子的 physical plan 中，对 child result 调用 `dispatchProjectionExtract` 后再构造算子。

#### 回归结果

| 测试 | 状态 |
|------|------|
| TckWith7Scenario1BoundEndpoint | ✅ 通过（已在 Plan B' 修复） |
| TckMerge5Scenario16AliasingExistingNodes1 | ✅ 通过（本次修复） |
| TckMerge5Scenario17AliasingExistingNodes2 | ✅ 通过 |
| TckMerge5Scenario18DoubleAliasing1 | ✅ 通过（本次修复） |
| TckMerge5Scenario19DoubleAliasing2 | ❌ 既有失败：binder `VariableAlreadyBound: 'x'` |
| TckList12Scenario1CollectAndExtract | ✅ 通过（本次修复） |
| TckList12Scenario2CollectAndFilter | ✅ 通过 |
| TckWith7Scenario2MultipleWiths | ❌ 既有失败：`count(*)` 返回 0 而非 1（Aggregate+Filter 链路） |

全量：435/437 通过，0 个新回归。2 个失败均为此分支 HEAD 既有。

#### 关键调试经历

**死胡同 #1：去掉 binder symbol 预填充**。`query_executor.cpp:168-171` 把 `binder.ctx().symbols` 拷贝到 `plan_ctx.var_slots`，让 optimizer 的 `ensureSlot` 全变成 no-op。去掉后 DPL 测试通过，但 17 个 Aggregate 测试挂掉——因为 Aggregate 的 `output_names` 在首次入栈时依赖预填充。最终保留了预填充，改为在 `allocateSlotsInOp` 内通过 bottom-up 递归建立 scope 而非依赖预填充的 binder slot。

**死胡同 #2：全局 scope override 问题**。`allocateSlotsInOp` 原先先处理当前 Project 的 items（`ensureSlot` 写入 `name_to_slot`）再递归进入子节点。外层 RETURN 的 alias 覆写了内层 WITH 的 entry。改为 bottom-up（先递归，再捕获 `input_slot`，最后分配 `alias_slot`），配合 Fix 2 的非 forward 保护，解决了 scope 覆盖。

**死胡同 #3：`planForSlot` 与 binder/optimizer slot 空间不匹配**。`BoundColumnRef.slot_id` 是 binder 分配的，但 PE plan 的 key 是 optimizer 的 canonical slot（经 `alias_map` 做 canonicalization）。`canonicalOf(binder_slot)` 在 `alias_map` 中找不到（alias_map key 是 optimizer 的 `alias_slot`），导致 plan 查不到。planForSlot 的 fallback 到 `planForName` 用名字重新查，绕过了这个问题。



