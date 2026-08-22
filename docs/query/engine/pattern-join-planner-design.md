# Pattern Join Planner 与作用域 Slot 重构设计

> 本文是剩余 TCK 失败的重构方案，替代此前逐场景修补方式。
> 目标：把 MATCH / OPTIONAL MATCH / WHERE 中的 pattern 变量复用，
> 统一建模为“关系式 join”，并用来源作用域稳定的 Slot 解析消除重复名歧义。

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

输出：
```cpp
struct PatternVariableClass {
    enum Kind { NEW, OUTER, REUSED };
    std::string name;
    BoundType type;             // 拓扑类型优先
    Kind kind;
    ScopeId source_scope;       // 变量来源作用域
    uint32_t source_column;     // 来源列
    SlotId slot;
};

struct PatternPartPlan {
    // 本 part 输出的变量列表与列
    std::vector<PatternVariableClass> outputs;
    BoundLogicalOperator op;    // 本 part 的局部逻辑计划
};

struct PatternJoinPlan {
    bool optional;
    std::vector<PatternVariableClass> correlations; // CorrelatedSource 列
    std::vector<PatternPartPlan> parts;
    // 跨 part 等值约束：left_var == right_var
    std::vector<JoinEqualitySpec> equalities;
};
```

算法：
1. 遍历 `MatchPatternGraph` parts，确定每个变量的 NEW/OUTER/REUSED；
2. 每个 part 在独立子作用域中绑定为局部算子；
3. 相邻 part 通过 `JoinEqualityBuilder` 连接；
4. OPTIONAL：外层变量打包成 `CorrelatedSource`，与 part 序列 CrossProduct + 等值；
5. 生成 `BoundLeftJoinOp`（correlation 用 `ScopeSlot` 记录）。

职责边界：
- 不生成 WHERE 之外的其他算子；
- 不做合法性检查（由 `PatternLegalityAnalyzer` 负责，见下）。

### 4.2 `PatternLegalityAnalyzer`

文件：`src/query/planner/binder/pattern/pattern_legality_analyzer.{hpp,cpp}`

在绑定前对 `MatchPatternGraph` 做静态检查：
- 同一 part 内变量复用冲突（node/rel/path）；
- 跨 part 类型冲突；
- 输出 `VariableTypeConflict` / `VariableAlreadyBound`。

这使绑定阶段可以安全地使用独立作用域，不再依赖“复用 ctx 时顺带发现错误”。

### 4.3 `JoinEqualityBuilder`（保留并收敛）

现有 `Binder::bindCrossWithEqualities` 升级为该模块唯一入口：
- 输入左右逻辑算子 + 左右 `BindContext::Snapshot`；
- 输出 `Filter(CrossProduct)`；
- 等值谓词仍使用 `__eq_left__name / __eq_right__name` 标记。

### 4.4 `ScopedSlotResolver`

文件：`src/query/optimizer/scoped_slot_resolver.{hpp,cpp}`

核心数据：
```cpp
struct ScopeSlotKey {
    ScopeId scope;
    std::string name;
};
```

职责：
- `ensureSlot(scope, name)`：为“作用域 + 名字”分配/查找 SlotId；
- `canonicalSlot(slot)`：对象列提升时返回 canonical slot；
- `planFor(scope, name)`：PEPlan 按 `ScopeSlotKey` 查找，而不是全局 name；
- `var_slots` 的 last-write 改为 **当前作用域写入**，跨作用域同名不覆盖。

DPL 接入：
- `ensureSlotsInExpr`、`allocateSlotsInOp`、`rewriteExpr` 改用 resolver；
- `__eq_left/right` 内部引用仍走 side-specific 解析（见下）。

### 4.5 `PhysicalCrossRefResolver`（保留并封装）

文件：`src/query/physical_plan/cross_ref_resolver.{hpp,cpp}`

- `__eq_left__var` → 左子 layout/schema；
- `__eq_right__var` → 右子 layout/schema + 左物理列数；
- 只做列号映射，不构造谓词。

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
- 变长路径的边列表过滤：在 depth 达到后，比较路径边 ID 集合与列表 ID 集合。

### 4.7 `LabelOrderContext`

文件：`src/query/planner/binder/label_order_context.hpp`

- Binder 在绑定节点 pattern 时写入 `变量名 → 有序标签名列表`；
- `VertexValue` 增加 `std::vector<LabelId> ordered_labels`（或仅 ProjectionExtract 携带）；
- formatter 优先使用 ordered labels；存储编码不变。

### 4.8 副作用审计（D 类）

先单独用 TCK 最小复现 Create3[3]、Unwind1[6]，定位是副作用计数、行序还是 batched write 合并问题，再决定是否新增 `SideEffectAccounting` 模块。不在本设计第一阶段实施。

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

1. **阶段 0**：落地本设计文档 + 回归基线；
2. **阶段 1**：抽取 `JoinEqualityBuilder` / `PhysicalCrossRefResolver`（已有部分代码，整理接口）；
3. **阶段 2**：实现 `ScopedSlotResolver`，替换 DPL name-based 解析；回归全部现有绿测；
4. **阶段 3**：实现 `PatternLegalityAnalyzer` + `PatternJoinPlanner`，替换 `bindMatch` 多 part、`bindOptionalMatch`、WHERE pattern 路径；
5. **阶段 4**：`BoundEdgeFilter` 统一 Expand/VarLen；
6. **阶段 5**：`LabelOrderContext`；
7. **阶段 6**：D 类副作用；
8. **阶段 7**：全量 TCK + 报告。

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
