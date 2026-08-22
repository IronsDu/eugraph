# Pattern Join Planner 与作用域 Slot 重构设计

> 本文是剩余 TCK 失败的重构方案，替代此前逐场景修补方式。
> 目标：把 MATCH / OPTIONAL MATCH / WHERE 中的 pattern 变量复用，
> 统一建模为“关系式 join”，并用来源作用域稳定的 Slot 解析消除重复名歧义。

## 〇、外部评审与采纳结论

本文 v4 初稿经过外部评审，以下是逐条结论；**不采纳项也记录在此，防止未来再次争论**。

| # | 评审建议 | 结论 | 说明 |
|---|----------|------|------|
| 1 | 四层身份模型 `name → VariableId → VariableBinding → SlotId → column_index` | **部分采纳** | 概念层采纳；实现层不立即新增第三个 ID 分配器，理由见下方“不采纳项” |
| 2 | `PatternPartConnection { CARTESIAN, CORRELATED }` 显式区分 | 采纳 | `MATCH (a),(b)` 是 Cartesian；`MATCH (a)-->(b)` 是 Correlated |
| 3 | `JoinEqualityBuilder` 只生成 equality，不推断“谁和谁 join” | 采纳 | 推断只保留为 MATCH-after-WITH 的便捷入口 |
| 4 | OPTIONAL predicate 语义 invariant | 采纳 | predicate 必须属于右子计划语义域，禁止提升到 LeftJoin 之后 |
| 5 | varlen 关系列表精确语义 | 采纳 | 定义为 **sequence equality**（顺序敏感，非 set/multiset） |
| 6 | LabelOrder 不进入 `VertexValue` | 采纳 | 标签顺序是 presentation metadata，放 Projection/formatter |
| 7 | `__eq_left/right` 降级为 lowering 兼容机制 | 采纳 | 不进入 `PatternJoinPlan` IR |
| 8 | 先定义身份模型，再实现 ScopedSlotResolver | 采纳 | 迁移顺序已按此调整 |
| 9 | 最终架构图 | 采纳 | 见“五、数据流与调用关系” |
| 10 | 身份 invariants 写入 AGENTS.md | 采纳 | 见仓库根目录 AGENTS.md“Binder 身份模型不变量” |

### 不采纳项：新增独立 `VariableId` 分配器

评审建议：

```text
name
  ↓
VariableId
  ↓
VariableBinding
  ↓
SlotId
```

其中 `VariableId` 是独立的 semantic identity，不应等于 `ScopeId + name`，也不应等于 `SlotId`。

**不采纳及理由：**

1. 现有 `SlotId` 已经具备 VariableId 所需的三个性质：
   - Binder 层每次绑定调用 `SlotAllocator::next()`，返回 query-global 唯一值；
   - `SlotId` 在 query 生命周期内不可变；
   - `BoundColumnRef` / `ColumnInfo` 已经携带 `slot_id`。
2. 若再引入独立 `VariableId`，会产生四套编号并存：
   `nextSlotId / nextInternalSlot / name_to_slot / VariableId`，
   每个表达式、每个 ColumnInfo、每个 layout 都要增加一个字段并保持一致。
3. 当前真正的缺陷不是“缺少 VariableId”，而是：
   - `BindContext` 没有 `ScopeId`；
   - `all_symbols` 是全局 last-write，跨作用域同名会覆盖；
   - `save/restore` 不恢复 scope 栈，导致“谁在哪个 scope 可见”不可查询。
4. 因此本设计采用等价但更小的方案：
   - 新增 `ScopeId`（BindContext 单调递增）；
   - 解析键为 `(ScopeId, name)`；
   - **绑定身份仍复用 `SlotId`**，并定义为：
     `using VariableId = SlotId;`（语义身份 = Binder 分配的绑定槽）；
   - 每个新绑定强制分配新 SlotId，禁止跨 scope 复用同名 slot。
5. 若未来出现需要区分“两个绑定但同一语义实体”的场景，再把 `VariableId` 升级为独立强类型；
   该升级路径已预留（所有新代码通过 `VariableId` alias 引用，而不是直接写 `SlotId`）。

### 第二轮评审结论（已做工程取舍）

第二轮评审整体批准进入实现，并提出三个“实现前必须补”的点。逐条结论如下：

| # | 意见 | 结论 | 说明 |
|---|------|------|------|
| 1 | 不再坚持独立 `VariableId` | 采纳 | 与本文不采纳项一致 |
| 2 | `PatternPartConnection` 描述**任意两个 part** 的关联，而不是仅相邻 part | **部分采纳** | IR 保留任意 part 对；实现阶段仍按 source order 生成 left-deep join tree，原因见下方“不采纳项” |
| 3 | 明确 `PatternJoinPlan` 是 Binder 内部的 **bound planning representation**，不是纯 semantic IR | 采纳 | 已写入 4.1 |
| 4 | 区分 **pattern predicate** 与 **clause filter** 两类 WHERE | 采纳 | 已写入 4.2 |
| 5 | `ScopeSlotKey` 是解析键，不是 identity | 采纳 | 已写入 4.4 |
| 6 | varlen sequence equality 写成 invariant 并加单元测试 | 采纳 | 已写入 4.6 与测试清单 |
| 7 | `PatternLegalityAnalyzer` 职责扩展为 legality + variable classification | 采纳 | 已写入 4.2；不新增独立 Resolver 模块 |
| 8 | 不再增加 Binder abstraction | 采纳 | 核心链固定为 5 个组件，其余是辅助设施 |
| 9 | 增加 Join Graph / Join Order 说明 | **部分采纳** | 记录 join graph 与未来 reorder 边界；本期不实现 reorder |

#### 不采纳项：本期实现任意 part 连接 / join reorder

评审建议 `PatternPartConnection` 不限于相邻 part，并引入 join order。

**不采纳及理由：**

1. 当前执行计划是 left-deep 树，且 CrossProduct / LeftJoin 的物理算子只支持二叉输入；
   任意 part 图会立刻要求 join-order 决策与 bushy-tree 执行支持，超出本次 TCK 修复范围。
2. IR 层仍会记录完整的 `(left_part, right_part, kind, equalities)` 关系，
   避免把 AST 顺序写死；实现阶段由 `PatternJoinPlanner` 按 source order 稳定地构造 left-deep tree。
3. 未来接入 cardinality 或成本模型时，可在 `PatternJoinPlanner` 与 `BoundLogicalPlan` 之间
   增加 join-order 阶段，IR 无需再改。

## 一、当前失败清单（14 个）

| 场景 | 查询要点 | 根因分类 |
|------|----------|----------|
| Match7[9] | `OPTIONAL MATCH (a)-->(b)-->(c)`，a、c 已绑定 | A |
| Match7[11] | bound undirected rel + optional rel，SyntaxError | A |
| Match7[17] | optional named path 输出方向错误 | A |
| MatchWhere6[5] | `OPTIONAL MATCH (a2)<-[r]-(b2) WHERE a1 = a2` | A |
| MatchWhere6[8] | 两个链式 OPTIONAL MATCH + WHERE `x.val < z.val` | A |
| MatchWhere4[2] | WHERE 内 pattern 谓词 + OR | A |
| WithWhere4[2] | 同上 | A |
| Match5[27] | 混合方向多 pattern 复用变量 | A |
| Match8[2] | MATCH/MERGE/OPTIONAL 行数 | A/D |
| Match4[7] | `[*0..1]-()-[r]-()-[*0..1]`，r 已绑定 | B |
| Match9[6] | `MATCH (first)-[rs*]->(second)`，rs 为关系列表 | B |
| Match3[7] | 多标签输出顺序 | C |
| Create3[3] | MATCH-CREATE-WITH-CREATE 副作用 | D |
| Unwind1[6] | UNWIND 参数列表建节点顺序 | D |

分类：
- **A**：pattern 变量复用 / 关联没有统一 join 模型。
- **B**：varlen 缺少绑定关系过滤。
- **C**：标签顺序元数据缺失。
- **D**：副作用 / 行序语义独立缺陷。

---

## 二、现状与根因

### 2.1 三条绑定路径各自为政

今天的实现有三个互相不一致的绑定路径：

1. `bindMatch`：pattern part 线性展开，`pi>0` 用 CrossProduct 拼接；
2. `bindOptionalMatch`：只处理“起点绑定”和“单一非起点绑定”；
3. WHERE 中的 pattern 谓词：走 `bindWhere`/pattern expression 的独立路径。

例子（Match7[9]）：

```cypher
MATCH (a:Single), (c:C)
OPTIONAL MATCH (a)-->(b)-->(c)
RETURN b
```

- `a` 是起点，能走 `CorrelatedSource`；
- `c` 是终点，依赖 Expand 的 `dst_bound`；
- 链式 hop + PE 对象列会让 `c` 的物理列被解析到对象槽，返回 null。

例子（MatchWhere6[8]）：

```cypher
MATCH (x:X)
OPTIONAL MATCH (x)-[:E1]->(y:Y)
OPTIONAL MATCH (y)-[:E2]->(z:Z)
WHERE x.val < z.val
```

第二个 OPTIONAL 的起点 `y` 来自第一个 OPTIONAL 的右输出；当前 `bindOptionalMatch` 的 left 作用域无法干净表达“可选右列作为下一可选输入”。

根因：**没有统一描述“变量来自哪个作用域、在哪个 part 中是新绑定还是复用、用什么 join key 连接”的中间表示。**

### 2.2 Slot/PE 仍是 name-based

- `var_slots` 是 `name → slot` 的 last-write 映射；
- PEPlan 也按变量名查找；
- 两个作用域同名 `b` 时，左右列会被解析到同一 slot 或同一 `__pe` 对象槽；
- 我们已经用 `__eq_left__/__eq_right__` 标记绕过等值谓词，但链式 pattern、命名 path、WHERE pattern 仍受影响。

例子（调试实证）：

```
OPTIONAL MATCH (x)-->(b)   -- b 在外层已绑定
filter-vals c0=VertexValue(0) c4=VertexRef(3)
```

`VertexValue(0)` 就是 PE 把 raw 拓扑列替换成对象槽后未正确构造的结果。

根因：**SlotId 虽然全局唯一，但解析入口仍以 name 为 key，丢失了来源作用域。**

### 2.3 varlen 没有绑定边过滤抽象

- 固定 Expand 有 `dst_bound / edge_bound / dst_col_idx / edge_col_idx` 布尔成员；
- VarLenExpand 只有 `dst_bound / dst_col_idx`；
- 没有“这条边必须等于绑定边 / 必须属于绑定关系列表”的统一输入。

例子（Match9[6]）：

```cypher
MATCH (a)-[r1]->()-[r2]->(b)
WITH [r1, r2] AS rs, a AS first, b AS second LIMIT 1
MATCH (first)-[rs*]->(second)
```

`rs` 是关系列表，varlen 路径必须恰好由 `rs` 中的关系组成；当前实现无法表达。

### 2.4 标签顺序丢失

`VertexValue.labels` 是 `LabelIdSet`（无序哈希集合）。查询 `(n:A:B:...:M)` 的展示顺序由集合迭代决定，而非 query 顺序。

例子（Match3[7]）：

```cypher
MATCH (n:A:B:C:D:E:F:G:H:I:J:K:L:M)-[:T]->(m:Z:Y:X:W:V:U)
RETURN n, m
```

期望 m 为 `Z,Y,X,W,V,U`；实际无序。

根因：**标签顺序只在 AST 中存在，进入 Binder 后没有随变量传播。**

### 2.5 副作用与行序独立问题

Create3[3]、Unwind1[6] 与 pattern join 无关，属于 CREATE/UNWIND 的副作用计数与顺序语义，需要单独复现和修复。

---

## 三、设计原则

1. **单一中间表示**：所有 pattern 绑定（MATCH / OPTIONAL / WHERE pattern）都先转换成 `PatternJoinPlan`，再生成逻辑算子。
2. **作用域显式**：变量归属 `ScopeId`；同名变量不再依赖全局 last-write。
3. **统一 join key**：变量复用 = 等值谓词，统一由 `JoinEqualityBuilder` 生成。
4. **物理层只做映射**：物理算子和 DPL 不感知 Cypher 语义，只按 slot/schema 映射列。
5. **模块单向依赖**：
   `PatternGraph → PatternJoinPlanner → BindContext/BoundLogicalPlan → DPL → PhysicalPlan`

---

## 四、新模块

### 4.1 `PatternJoinPlanner`

文件：`src/query/planner/binder/pattern/pattern_join_planner.{hpp,cpp}`

输入：
- `MatchPatternGraph`；
- `BindContext` 作用域快照（外层可见变量）；
- 是否为 OPTIONAL。

核心 IR：

```cpp
using VariableId = SlotId; // 语义身份 = Binder 分配的绑定槽

struct PatternVariableClass {
    enum Kind { NEW, OUTER, REUSED };
    std::string name;            // 仅用于错误信息/调试
    VariableId id;               // 语义身份
    BoundType type;              // 拓扑类型优先
    ScopeId source_scope;        // 来源作用域
    uint32_t source_column;      // 来源列（物理列仍由 DPL 解析）
};

struct JoinEqualitySpec {
    VariableId left;             // 不是 name，也不由同名推断
    VariableId right;
};

struct PatternPartConnection {
    enum Kind { CARTESIAN, CORRELATED };
    size_t left_part;             // 任意两个 part 的索引，不限于相邻
    size_t right_part;
    Kind kind;
    std::vector<JoinEqualitySpec> equalities;
};

struct PatternPartPlan {
    std::vector<PatternVariableClass> outputs;
    BoundLogicalOperator op;     // 本 part 局部计划
};

struct PatternJoinPlan {
    bool optional;
    std::vector<PatternVariableClass> correlations;
    std::vector<PatternPartPlan> parts;
    std::vector<PatternPartConnection> connections;
};
```

算法：
1. 遍历 `MatchPatternGraph`，按作用域和绑定位置确定每个变量的 NEW/OUTER/REUSED，并分配 `VariableId`；
2. 每个 part 在独立子作用域中绑定为局部算子；
3. 构造 **part connection graph**（任意两个 part 的 CARTESIAN/CORRELATED 关系）；
4. 当前实现按 source order 把 connection graph 降成 left-deep join tree：
   - `CARTESIAN`：纯 `CrossProduct`；
   - `CORRELATED`：`CrossProduct + JoinEqualitySpec` 生成的 equality Filter；
5. OPTIONAL：外层变量打包成 `CorrelatedSource`，与 part 序列 CrossProduct + 等值；
6. 生成 `BoundLeftJoinOp`（correlation 用 `VariableId + ScopeId` 记录）。

**PatternJoinPlan 的定位**：它是 Binder 内部的 **bound planning representation**，
不是 AST-level 纯语义 IR；`PatternPartPlan.op` 已经是 `BoundLogicalOperator`。
未来 join reorder 可在此 representation 与最终 `BoundLogicalPlan` 之间插入。

**OPTIONAL predicate invariant**：
与 OPTIONAL MATCH 关联的 pattern predicate 必须在 optional 右子计划语义域内求值
（即在 `BoundLeftJoinOp.right` 内部），不得提升到 LeftJoin 之后；
LeftJoin 之后的 WHERE 才属于外层查询语义。

职责边界：
- 不生成 WHERE 之外的其他算子；
- 不做合法性检查（由 `PatternLegalityAnalyzer` 负责，见下）。

### 4.2 `PatternLegalityAnalyzer`

文件：`src/query/planner/binder/pattern/pattern_legality_analyzer.{hpp,cpp}`

职责 = **合法性检查 + 变量绑定分类**，不新增独立的 Resolver 模块：

- 名字解析与变量分类：输出每个变量的 `NEW / OUTER / REUSED`；
- 同一 part 内变量复用冲突（node/rel/path）；
- 跨 part 类型冲突；
- 输出 `VariableTypeConflict` / `VariableAlreadyBound`。

这使绑定阶段可以安全地使用独立作用域，不再依赖“复用 ctx 时顺带发现错误”。

**WHERE 的两类语义（必须区分）**：

| 类别 | 出现位置 | 处理 |
|------|----------|------|
| Pattern predicate | `MATCH ... WHERE ...`、`OPTIONAL MATCH ... WHERE ...` | 属于 pattern 描述，进入 `PatternJoinPlanner` / OPTIONAL 右子计划 |
| Clause filter | 独立 `WHERE ...`（WITH 之后） | 生成普通 `BoundFilterOp` |

禁止把 pattern predicate 无条件提升为 post-MATCH/clause filter。

### 4.3 `JoinEqualityBuilder`（保留并收敛）

文件：`src/query/planner/binder/join_equality_builder.{hpp,cpp}`

- 输入：左右逻辑算子 + `std::vector<JoinEqualitySpec>`；
- 输出：`Filter(CrossProduct)`；
- **不根据左右 scope 的同名变量自行推断连接关系**；
- 等值谓词 lowering 仍使用 `__eq_left__name / __eq_right__name` 标记（兼容机制，见 4.5）；
- MATCH-after-WITH 保留一个便捷函数 `specsFromSameName(left_scope, right_scope)`，
  该函数只表达“WITH 投影同名即同一绑定”这一种已被 TCK 验证的语义，不复用为通用推断。

### 4.4 `ScopedSlotResolver`

文件：`src/query/optimizer/scoped_slot_resolver.{hpp,cpp}`

身份模型（与 AGENTS.md 的硬性不变量一致）：

```cpp
using VariableId = SlotId;   // 语义身份 = Binder 分配的绑定槽

struct ScopeSlotKey {
    ScopeId scope;            // 可见性作用域（BindContext 新增，单调递增）
    std::string name;         // 仅作为作用域内解析键
};
```

`ScopeSlotKey` 只回答“这个 scope 里名字 x 指向谁”；**变量身份**由
`VariableId(=SlotId)` 回答。两者禁止混用。

职责：
- `BindContext` 新增 `ScopeId current_scope` 与 `ScopeId next_scope()`；
- `ensureSlot(ScopeSlotKey)`：新绑定强制分配新 SlotId；跨作用域同名禁止复用；
- `canonicalSlot(slot)`：对象列提升时返回 canonical slot；
- `planFor(ScopeSlotKey)`：PEPlan 按作用域键查找，而不是全局 name；
- 删除 `all_symbols` last-write 语义，改为 `scoped_bindings: unordered_map<ScopeId, unordered_map<string, SlotId>>`。

DPL 接入：
- `ensureSlotsInExpr`、`allocateSlotsInOp`、`rewriteExpr` 改用 resolver；
- `__eq_left/right` 内部引用仍走 side-specific 解析（见下）。

### 4.5 `PhysicalCrossRefResolver`（保留并封装）

文件：`src/query/physical_plan/cross_ref_resolver.{hpp,cpp}`

- `__eq_left__var` → 左子 layout/schema；
- `__eq_right__var` → 右子 layout/schema + 左物理列数；
- 只做列号映射，不构造谓词；
- 该机制是 DPL/physical pipeline 的 **lowering 兼容层**，不进入 `PatternJoinPlan` IR。

### 4.6 `BoundEdgeFilter`

文件：`src/query/planner/logical_plan/operator/bound_edge_filter.hpp`

```cpp
struct BoundEdgeFilter {
    // 固定 Expand
    std::optional<BoundColumnRef> bound_edge;      // 单条绑定边
    // VarLenExpand
    std::optional<BoundExpression> bound_edge_list; // 关系列表
    // 目标节点（已有 dst_bound 合并到这里）
    std::optional<BoundColumnRef> bound_dst;
};
```

- `BoundExpandOp` / `BoundVarLenExpandOp` 都持有该结构（替换现有布尔成员）；
- 物理 `ExpandPhysicalOp` / `VarLenExpandPhysicalOp` 在编译期解析引用，执行期过滤；
- **关系列表语义**：`MATCH (first)-[rs*]->(second)` 中 `rs` 是关系列表时，
  候选路径的 relationship 必须与 `rs` 做 **sequence-level equality**：
  - 按路径遍历顺序逐元素比较 EdgeId；
  - 顺序敏感、长度必须相等、重复关系必须按重复次数匹配；
  - 不是 set equality，也不是 multiset equality；
  - 方向与路径遍历方向一致，不额外尝试反转列表。

必须实现的单元测试例子：

```
[r1,r2]     == [r1,r2]      true
[r1,r2]     == [r2,r1]      false
[r1,r1,r2]  == [r1,r2]      false
[r1,r2,r3]  == [r1,r2]      false
[]          == [r1]         false（长度敏感）
```

### 4.7 `LabelOrderContext`

文件：`src/query/planner/binder/label_order_context.hpp`

- Binder 在绑定节点 pattern 时写入 `变量名 → 有序标签名列表`；
- 该信息作为 **Projection / Result metadata** 传播到 formatter；
- **不修改 `VertexValue` / 存储编码**；`LabelIdSet` 继续表达存储语义的无序集合；
- 只有输出格式化使用有序元数据，运行值对象不携带 query-specific metadata。

### 4.8 副作用审计（D 类）

先单独用 TCK 最小复现 Create3[3]、Unwind1[6]，定位是副作用计数、行序还是 batched write 合并问题，再决定是否新增 `SideEffectAccounting` 模块。不在本设计第一阶段实施。

### 4.9 Join Graph 与 Join Order

- `PatternPartConnection` 构成 **join graph**，不把 AST 顺序固化到 IR；
- 当前实现阶段：`PatternJoinPlanner` 按 source order 将 join graph 降成 left-deep join tree；
- 未来阶段：可在 `PatternJoinPlan` 与最终 `BoundLogicalPlan` 之间增加 join-order 决策，
  基于 `PatternPartConnection` 和 cardinality 重排，IR 无需变化；
- 本期**不实现** join reorder / bushy-tree 执行。

---

## 五、数据流与调用关系

```
MatchClause / WherePattern
        │
        ▼
PatternLegalityAnalyzer ──► 错误报告
        │
        ▼
PatternJoinPlanner ──► PatternJoinPlan
        │
        ▼
JoinEqualityBuilder ──► BoundLogicalPlan
        │
        ▼
DPL (ScopedSlotResolver) ──► PEPlan / rewriteColumnIndices
        │
        ▼
PhysicalPlanner + PhysicalCrossRefResolver ──► PhysicalPlan
```

不变量：
- `BoundColumnRef` 永远带 `scope + slot`；
- 同名变量在不同作用域不会共享 layout 槽；
- `__eq_*` 引用不会被 DPL 按名提升；
- 每个 CrossProduct 的右列只通过等值谓词影响左输出。

---

## 六、迁移计划

1. **阶段 0**：TCK / executor 回归基线，记录当前 14 失败快照；
2. **阶段 1**：明确身份模型 `name / VariableId(=SlotId) / ScopeId / column_index`，加入 `BindContext::ScopeId`；
3. **阶段 2**：实现 `ScopedSlotResolver`，删除 `all_symbols` last-write，回归全部现有绿测；
4. **阶段 3**：抽取 `JoinEqualityBuilder`（显式 `JoinEqualitySpec`）+ `PhysicalCrossRefResolver`；
5. **阶段 4**：实现 `PatternLegalityAnalyzer`；
6. **阶段 5**：实现 `PatternJoinPlanner`，迁移 MATCH / OPTIONAL / WHERE pattern；
7. **阶段 6**：`BoundEdgeFilter` 统一 Expand / VarLen（sequence semantics）；
8. **阶段 7**：`LabelOrderContext`；
9. **阶段 8**：D 类副作用；
10. **阶段 9**：全量 TCK + 报告。

每个阶段结束跑：
`With1/With6/Match2/Match6` + 该阶段目标 feature + `query_executor_tests`。

---

## 七、不做的事

- 不重写存储层编码；
- 不改变 Bolt 结果格式；
- 不引入新的运行时 Join 算子；
- 不在物理执行热路径按名字符串比较；
- 不保留旧的三条绑定路径（完成阶段 3 后删除）。

---

## 八、相关文件

| 模块 | 文件 |
|------|------|
| PatternJoinPlanner | `src/query/planner/binder/pattern/pattern_join_planner.*` |
| PatternLegalityAnalyzer | `src/query/planner/binder/pattern/pattern_legality_analyzer.*` |
| JoinEqualityBuilder | `src/query/planner/binder/join_equality_builder.*` |
| ScopedSlotResolver | `src/query/optimizer/scoped_slot_resolver.*` |
| PhysicalCrossRefResolver | `src/query/physical_plan/cross_ref_resolver.*` |
| BoundEdgeFilter | `src/query/planner/logical_plan/operator/bound_edge_filter.hpp` |
| LabelOrderContext | `src/query/planner/binder/label_order_context.*` |
