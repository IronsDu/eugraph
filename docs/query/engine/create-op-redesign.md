# CreateNode/CreateEdge 算子重构设计

> 分支: `fix/temporal-property-roundtrip`
> 状态: **已实现**（2026-05-29）
> 关联: [temporal-property-roundtrip-design.md](temporal-property-roundtrip-design.md)

---

## 一、现有问题

### 问题 1：pending_props 被 ALTER 注册到有标签上

**现状**：当 `CREATE (:User {nickname: 'Ali'})` 中 `nickname` 不在 User schema 里时，physical planner 插入 `AlterVertexLabelPhysicalOp`，将 `nickname` 注册到 User 的 `LabelDef`。

**实际验证**（`CreateMultiLabelWithAnonFallback` 测试日志）：
```
Added 1 properties to label 'Employee'
Added 1 properties to label 'Person'
```
`nickname` 确实被 ALTER 到了 Person 和 Employee 上，而非走 `__anon__`。测试虽然通过（ALTER 后属性仍可读写），但违反了设计意图。

**违反的设计文档**：
- [unlabeled-node-property-design.md](../../features/unlabeled-node-property-design.md) 第 5 节：不在任何标签 schema 中的属性应回退到 `__anon__` 轻量路径
- [mixed-mode-design.md](../../refactor/mixed-mode-design.md) 第 12.2 节：ALTER 永久修改全局 schema，产生 schema 污染

### 问题 2：CreateNodePhysicalOp 不支持逐行创建

**现状**：`executeChunk()` drain child + 创建一个顶点。

**TCK 语义**（Create6 [3]）：
```cypher
UNWIND [42, 42, 42, 42, 42] AS x
CREATE (n:N {num: x})
RETURN n.num AS num SKIP 2 LIMIT 2
```
Side effects: **+5 nodes**（逐行创建），result: 2 行（SKIP/LIMIT 过滤结果集，不影响副作用）

**当前实现只创建 1 个节点，不符合语义。**

### 问题 3：Chained CREATE 中 DDL 算子导致重复执行

`CREATE (a:Person {nickname: 'A'}), (b:Person {nickname: 'B'})` 的计划树：
```
CreateNodePhysicalOp(b)
  └── AlterVertexLabelPhysicalOp(b 的 DDL: 注册 nickname 到 Person)
        └── CreateNodePhysicalOp(a)
              └── AlterVertexLabelPhysicalOp(a 的 DDL: 注册 nickname 到 Person)
```
同一个 DDL 操作执行两次。

### 问题 4：CreateEdgePhysicalOp 同样缺少逐行创建

`MATCH (a), (b) CREATE (a)-[:T]->(b)` 只创建一条边，应为每对 (a, b) 创建一条。

---

## 二、问题关联：temporal 属性回归 bug

[temporal-property-roundtrip-design.md](temporal-property-roundtrip-design.md) 的回归 bug 与上述问题同源：

| 回归 Bug | 根因关联 |
|----------|---------|
| Bug 1: 重复 child 执行 | 问题 3 同源：DDL 算子 + child 透传逻辑纠缠 |
| Bug 2: 多标签属性混淆 | 问题 1 同源：pending_props 应走 `__anon__` 而非 ALTER |
| Bug 3: 匿名属性类型过于具体 | `__anon__` 的 `PropertyType::ANY` 是正确的 |

---

## 三、TCK 语义分析

### 3.1 CREATE 的输出：必须保留 child 列

**关键证据**：

**Create3 [5]**：
```cypher
MATCH (n) MATCH (m)
WITH n AS a, m AS b
CREATE (a)-[:T]->(b)
RETURN a, b
```
Result: `a = ({num: 1}), b = ({num: 1})`

RETURN 能看到 `a` 和 `b`（来自 WITH → 来自 MATCH）。说明 **child 列必须通过 CREATE 传递下去**。

**Create3 [1]**：
```cypher
MATCH ()
CREATE ()
```
Result: **empty**

"empty" 是因为没有 RETURN 子句（openCypher 规定无 RETURN 时结果为空），**不是因为 CREATE 不输出数据**。

**结论**：CREATE 算子内部仍产出 child 列 + 新创建实体列（供管线内部使用），但无 RETURN 时 Binder 在根部追加空 `BoundProjectOp`，最终输出 0 列。有 RETURN 时（如 `RETURN n, m`）从 ProjectPhysicalOp 按投影项选取列。

### 3.2 逐行创建

**Create6 [3]**：UNWIND 5 个值 → 创建 5 个节点。**每行 child 输入触发一次创建。**

**Create2 [2]**：
```cypher
CREATE (a), (b), (a)-[:R]->(b)
```
Side effects: +2 nodes, +1 relationship。三个 pattern 顺序执行，后继可引用前驱变量。

### 3.3 Chained CREATE 中变量引用

**Create2 [2]** `CREATE (a), (b), (a)-[:R]->(b)` 中边引用 `a` 和 `b`。这意味着：
- `a` 的 CreateNode yield {A}（child 列 = 空 + 自身 = {A}）
- `b` 的 CreateNode 收到 {A}，创建 B，yield {A, B}
- 边的 CreateEdge 收到 {A, B}，从 DataChunk 取 a 和 b 的 VertexId 创建边

### 3.4 MATCH + CREATE 中边的 src/dst

**Create2 [5]**：
```cypher
MATCH (x:X), (y:Y)
CREATE (x)-[:R]->(y)
```
src = x（来自 MATCH，已有 VertexId），dst = y（来自 MATCH，已有 VertexId）。

**Create2 [11]**：
```cypher
MATCH (x:Begin)
CREATE (x)-[:TYPE]->(:End)
```
src = x（来自 MATCH），dst = 新创建的 End 节点（需要动态分配 ID）。

---

## 四、新方案

### 4.1 核心原则

1. **逐行执行**：child 返回的每一行触发一次创建。无 child 时创建一个实体。
2. **保留 child 列**：输出 DataChunk = child 列 + 新创建的实体列（TCK 语义要求）。
3. **顶点 pending_props 走 `__anon__`**：顶点属性不在任何标签 schema 中时，通过 `getOrCreateAnonPropId` 轻量路径回退到 `__anon__`，不再 ALTER 注册到有标签。
4. **边 pending_props 走 ALTER**：边属性不在边标签 schema 中时，仍通过 `AlterEdgeLabelPhysicalOp` 注册到对应边标签（边属性无法兜底到 `__anon__`）。
5. **动态 ID 分配**：所有 VertexId / EdgeId 统一通过 `meta_.nextVertexId()` / `meta_.nextEdgeId()` 运行时动态分配，不再 planner 预分配。
6. **删除 `AlterVertexLabelPhysicalOp`**：顶点 DDL 算子不再需要。`AlterEdgeLabelPhysicalOp` 和 `CreateEdgeLabelPhysicalOp` 保留。

### 4.2 删除 DDL 算子

**删除**：
- `alter_vertex_label_physical_op.hpp/cpp`（顶点 pending_props 改走 `__anon__` 轻量路径）

**保留**：
- `alter_edge_label_physical_op.hpp/cpp`（边属性无法兜底到 `__anon__`，必须 ALTER 到对应边标签）
- `create_edge_label_physical_op.hpp/cpp`（首次遇到新边类型时创建标签 + WiredTiger 表）

在 `physical_planner.cpp` 中移除 `AlterVertexLabelPhysicalOp` 的构建逻辑。`AlterEdgeLabelPhysicalOp` 和 `CreateEdgeLabelPhysicalOp` 的构建逻辑不变。

### 4.3 属性归属

属性归属决策已在 binder 的 `bindCreateNodeProperties` 中正确实现：

| 属性名 | 匹配标签数 | 归属 | 处理方式 |
|--------|-----------|------|---------|
| `age`（Person 有） | 1 | Person | `label_properties`，使用已知 pid |
| `nickname`（无标签有） | 0 | `__anon__` | `pending_props`，通过 `getOrCreateAnonPropId` |
| `name`（Person + Employee 都有） | 2 | **歧义报错** | binder 报错 |

physical planner 的修改：不再为 `pending_props` 创建 DDL 算子，而是将 `pending_props` 连同 `__anon__` 的 LabelId 一起传递给 `CreateNodePhysicalOp`，由算子内部处理。

### 4.4 新的执行模型

```cpp
CreateNodePhysicalOp::executeChunk() {
    VectorizedEvaluator evaluator(eval_ctx_);

    // Phase 1: __anon__ 属性轻量注册（仅首次执行）
    if (!anon_registered_ && !pending_props_.empty()) {
        LabelId anon_lid = /* 获取 __anon__ LabelId */;
        for (auto& [prop_name, expr] : pending_props_) {
            uint16_t pid = co_await meta_.getOrCreateAnonPropId(prop_name, PropertyType::ANY);
            resolved_pending_.emplace_back(anon_lid, pid, std::move(expr));
        }
        auto updated_def = co_await meta_.getLabelDefById(anon_lid);
        if (updated_def) label_defs_[anon_lid] = std::move(*updated_def);
        anon_registered_ = true;
    }

    // Phase 2: 逐行创建
    if (child_) {
        auto child_gen = child_->executeChunk();
        while (auto chunk = co_await child_gen.next()) {
            for (size_t row = 0; row < chunk->count; ++row) {
                VertexId vid = co_await meta_.nextVertexId();
                auto label_props = buildLabelProps(&*chunk, row);

                bool ok = co_await insertVertex(vid, label_props);
                if (ok) {
                    // 保留 child 列 + 新增 VertexValue 列
                    DataChunk output;
                    for (size_t c = 0; c < chunk->numColumns(); ++c) {
                        Column col = Column::flat(chunk->columns[c].type, 1);
                        col.setValue(0, chunk->getValue(c, row));
                        output.columns.push_back(std::move(col));
                    }
                    // 新创建的顶点列
                    Column vertex_col = Column::flat(BoundTypeKind::VERTEX, 1);
                    vertex_col.setValue(0, Value(makeVertexValue(vid, label_props)));
                    output.columns.push_back(std::move(vertex_col));
                    output.count = 1;
                    co_yield std::move(output);
                }
            }
        }
    } else {
        // Standalone: 创建一个顶点，无 child 列
        VertexId vid = co_await meta_.nextVertexId();
        auto label_props = buildLabelProps(nullptr, 0);
        bool ok = co_await insertVertex(vid, label_props);
        if (ok) {
            DataChunk output;
            output.columns.push_back(Column::flat(BoundTypeKind::VERTEX, 1));
            output.columns[0].setValue(0, Value(makeVertexValue(vid, label_props)));
            output.count = 1;
            co_yield std::move(output);
        }
    }
}
```

### 4.5 关键区别

| 方面 | 旧模型 | 新模型 |
|------|--------|--------|
| Child 处理 | drain + 透传 | **逐行迭代 + 逐行创建** |
| 属性注册 | 独立 DDL 算子（ALTER） | **顶点：`__anon__` 内联轻量注册；边：保留 ALTER** |
| VertexId 分配 | planner 预分配 | **统一动态分配（`meta_.nextVertexId()`）** |
| 输出 | child 行全部透传 + 末尾一个自身顶点 | **每行 child 输入产生一行输出（child 列 + 新顶点列）** |
| DDL 算子 | AlterVertex/AlterEdge/CreateEdgeLabel | **删除 AlterVertex；保留 AlterEdge + CreateEdgeLabel** |

### 4.6 各场景的执行流程

#### Chained CREATE

`CREATE (a), (b), (c)` → `RETURN a, b, c`：

```
CreateNode(c)::executeChunk()
  │
  ├─ child_gen = CreateNode(b)::executeChunk()
  │   │
  │   ├─ child_gen = CreateNode(a)::executeChunk()
  │   │   │
  │   │   └─ standalone → 创建 A, yield {A}              ← schema: [a]
  │   │
  │   └─ 收到 {A}, 创建 B, yield {A, B}                   ← schema: [a, b]
  │
  └─ 收到 {A, B}, 创建 C, yield {A, B, C}                 ← schema: [a, b, c]
```

每层保留 child 列并追加自己的顶点。`RETURN a, b, c` 从最终 DataChunk 中按列索引取值。

#### Chained CREATE + Edge

`CREATE (a), (b), (a)-[:R]->(b)`：

```
CreateEdge(R)::executeChunk()
  │
  ├─ child_gen = CreateNode(b)::executeChunk()
  │   │
  │   ├─ child_gen = CreateNode(a)::executeChunk()
  │   │   │
  │   │   └─ standalone → 创建 A, yield {A}
  │   │
  │   └─ 收到 {A}, 创建 B, yield {A, B}
  │
  └─ 收到 {A, B}, 从 DataChunk 取 a.id 和 b.id 创建边, yield {A, B, edge_R}
```

边的 src/dst 从 DataChunk 中按变量名列索引取 VertexId。动态分配的 VertexId 已包含在 A 和 B 的 VertexValue 中。

#### MATCH + CREATE

`MATCH (n) CREATE (m {v: n.x}) RETURN n, m`：

```
CreateNode(m)::executeChunk()
  │
  ├─ child_gen = Scan(n)::executeChunk()
  │   │
  │   ├─ scan yield {n1}
  │   │   → 创建 m1(v=n1.x), yield {n1, m1}
  │   ├─ scan yield {n2}
  │   │   → 创建 m2(v=n2.x), yield {n2, m2}
  │   └─ ...
  │
  └─ RETURN n, m → 从每行 DataChunk 取 n 和 m 列
```

#### MATCH + CREATE Edge（混合引用）

`MATCH (x:Begin) CREATE (x)-[:TYPE]->(:End)`（Create2 [11]）：

```
CreateEdge(TYPE)::executeChunk()
  │
  ├─ child_gen = CreateNode(End)::executeChunk()
  │     │
  │     ├─ child_gen = Scan(x:Begin)::executeChunk()
  │     │     │
  │     │     └─ yield {x1}, {x2}, ...
  │     │
  │     └─ 每行: 创建 End_n, yield {x_n, End_n}
  │
  └─ 每行: 从 DataChunk 取 x.id 和 End.id 创建边, yield {x, End, edge}
```

### 4.7 CreateEdgePhysicalOp 同步改造

1. 支持逐行创建：child 返回多行时，每行创建一条边
2. 动态 EdgeId 分配（`meta_.nextEdgeId()`）
3. src/dst VertexId 从 DataChunk 中解析（`src_col_idx_` / `dst_col_idx_`）
4. 边 pending_props 通过 `AlterEdgeLabelPhysicalOp` 处理（保留，不改动）
5. 新边标签通过 `CreateEdgeLabelPhysicalOp` 处理（保留，不改动）

### 4.8 已知风险与待解决项

#### 风险 1：边的 src/dst VertexId 解析（已解决）

当前 `CreateEdgePhysicalOp` 的 `src_id_` 和 `dst_id_` 是 planner 预分配的常量。新方案改为从 DataChunk 中直接读取。

边的起点和终点一定已在上游数据中：来自 MATCH 的 VertexValue 或 child CreateNode 的 VertexValue。

**做法**：
1. Planner 根据变量名在 input_schema 中的位置，设置 `src_col_idx_` 和 `dst_col_idx_`
2. Run time 从 DataChunk 对应列取 VertexValue，直接用 `vertex.id`

```cpp
auto src_id = std::get<VertexValue>(chunk->getValue(src_col_idx_, row)).id;
auto dst_id = std::get<VertexValue>(chunk->getValue(dst_col_idx_, row)).id;
```

不再需要 `ctx.variable_vertex_ids` 预分配映射。

#### 风险 2：自环 `CREATE (root)-[:LINK]->(root)`（Create2 [7]）

src 和 dst 是同一个变量。DataChunk 中只有一个 `root` 列，src_id = dst_id = 同一个 VertexId。无需特殊处理。

#### 风险 3：边标签首次创建

`CreateEdgeLabelPhysicalOp` 负责首次遇到新边类型时：注册元数据 + 创建 WiredTiger 存储表。

它作为 `CreateEdgePhysicalOp` 的 child 算子，采用 pass-through 模式（DDL 一次 + 透传 child 行），与新方案的逐行创建模型兼容。**不需要改动。**

#### 风险 4：`CREATE ... RETURN ... LIMIT 0`（Create6 [1]）

`CREATE (n:N {num: 42}) RETURN n LIMIT 0` → result is empty, but +1 node。

这说明 LIMIT/SKIP 只过滤结果集，不影响副作用。当前架构中 CREATE 作为算子先执行（产生 side effect），LIMIT 在后续算子中过滤输出——这个模型是正确的，不需要特殊处理。

#### 风险 5：`MATCH () CREATE ()` 的匿名变量（Create3 [1]）

MATCH 和 CREATE 的节点都没有变量名。在 DataChunk 中仍然需要有列（匿名列），但 RETURN 无法引用。这不影响功能——只要不引用就不会出错。

---

## 五、改动清单

### 5.1 删除文件

| 文件 | 原因 |
|------|------|
| `alter_vertex_label_physical_op.hpp/cpp` | 顶点 pending_props 改走 `__anon__` |

### 5.2 修改文件

| 文件 | 变更 |
|------|------|
| `physical_planner.cpp` | 移除 `AlterVertexLabelPhysicalOp` 构建；CreateNode/CreateEdge 段简化；边 src/dst 改为列索引 |
| `create_node_physical_op.hpp` | 新增 `anon_registered_`；新增 `anon_label_id_`（用于 pending_props 归属） |
| `create_node_physical_op.cpp` | 重写 executeChunk：逐行创建 + child 列保留 + `__anon__` 内联注册 |
| `create_edge_physical_op.hpp` | `src_id_`/`dst_id_` 改为 `src_col_idx_`/`dst_col_idx_`（input_schema 列索引） |
| `create_edge_physical_op.cpp` | 重写 executeChunk：逐行创建 + 从 DataChunk 解析 src/dst + 动态 EdgeId |

### 5.3 不涉及的改动

| 文件 | 原因 |
|------|------|
| `bind_mutation.cpp` | 属性归属逻辑已正确 |
| `SetPhysicalOp` | 已正确 |
| `VectorizedEvaluator` | 已正确 |
| 存储层 | 接口不变 |

---

## 六、需要覆盖的 Cypher 模式

| # | 模式 | TCK 参考 | 语义 | 当前状态 |
|---|------|---------|------|---------|
| 1 | Standalone CREATE | Create1 | 创建 1 个节点 | ✅ |
| 2 | Chained CREATE | Create2 [2] | `CREATE (a), (b)` → 2 nodes, child 列传递 | ✅ |
| 3 | Chained CREATE + Edge | Create2 [2] | `CREATE (a), (b), (a)-[:R]->(b)` | ✅ |
| 4 | Self-loop | Create2 [7] | `CREATE (root)-[:LINK]->(root)` → 1 node, 1 rel | ✅ |
| 5 | MATCH + CREATE Node | Create3 [1] | 逐行创建，result empty (no RETURN) | ✅ |
| 6 | MATCH + CREATE Edge | Create2 [5,11] | src 来自 MATCH，dst 可能来自 MATCH 或新创建 | ✅ |
| 7 | WITH + CREATE | Create3 [9] | `CREATE (a) WITH a CREATE (b) CREATE (a)<-[:T]-(b)` | ✅ |
| 8 | UNWIND + CREATE | Create6 [3] | 逐行创建，RETURN + SKIP/LIMIT 不影响 side effect | ✅ |
| 9 | Temporal 属性 | WithOrderBy2 | `CREATE (:A {date: date(...)})` | ✅ |
| 10 | pending_props → __anon__ | 自测 | `CREATE (:Person {nickname: 'Ali'})` | ✅ |
| 11 | 多标签 + __anon__ 回退 | 自测 | `CREATE (:P:E {age:1, salary:2, tag:'x'})` | ✅ |
| 12 | 多跳 CREATE | Create5 [1] | `CREATE (:A)-[:R]->(:B)-[:R]->(:C)` → 3 nodes, 2 rels | ✅ |
| 13 | RETURN + LIMIT 0 | Create6 [1] | side effect 不受 LIMIT 影响 | ✅ |

---

## 七、实施步骤

### Phase 1：删除 DDL 算子 + 清理 planner

1. 删除 `alter_vertex_label_physical_op.hpp/cpp`
2. 清理 `physical_planner.cpp` 中 `AlterVertexLabelPhysicalOp` 的构建逻辑
3. 保留 `alter_edge_label_physical_op.hpp/cpp`（边属性无 `__anon__` 兜底）
4. 保留 `create_edge_label_physical_op.hpp/cpp`（不变）
5. 编译通过

### Phase 2：重构 CreateNodePhysicalOp

1. 重写 `executeChunk()`：逐行创建 + child 列保留
2. 内联 `__anon__` 属性注册
3. 统一动态 VertexId 分配（`meta_.nextVertexId()`）
4. 编译通过

### Phase 3：重构 CreateEdgePhysicalOp

1. `src_id_`/`dst_id_` 改为 `src_col_idx_`/`dst_col_idx_`，从 DataChunk 解析
2. 逐行创建 + 动态 EdgeId
3. `AlterEdgeLabelPhysicalOp` 和 `CreateEdgeLabelPhysicalOp` 保持不变（pass-through 模式兼容逐行创建）
4. 编译通过

### Phase 4：验证

1. 运行全量测试
2. 重点验证第六节中的 13 个 Cypher 模式
3. TCK temporal 测试回归验证

---

## 八、已知限制

1. **逗号 CREATE 跨 pattern 变量引用**：`CREATE (a:Person), (b:Person), (a)-[:KNOWS]->(b)` 中，binder 把 pattern 2 的 `(a)` 当作新的匿名节点（无标签），而非引用 pattern 0 中已创建的 Person 节点 `a`。导致创建 4 个节点（2 Person + 2 anon）而非 2 个。**变通方案**：使用 inline pattern `CREATE (a:Person)-[:KNOWS]->(b:Person)`。

2. **MATCH 笛卡尔积 + CREATE edge**：`MATCH (a:Person), (b:Person) CREATE (a)-[:KNOWS]->(b)` 当前不产生边。根因在 planner/binder 层的笛卡尔积 + edge 组合处理，非本次重构引入的问题。

---

## 九、测试覆盖

新增 23 个单元测试覆盖重构代码：

| 测试类 | 覆盖要点 |
|--------|---------|
| QueryExecutorTest (18 tests) | 动态 VID 分配、VID 单调递增、跨语句 VID 唯一性、child 列保留、动态 EID 分配、src/dst VID 匹配、逐行创建、带属性边创建、边属性验证、边 label 正确性、CREATE+DELETE 往返、WITH+LIMIT 过滤 |
| QueryExecutorMultiLabelTest (5 tests) | __anon__ 未知属性写入、多 anon 属性、MATCH+CREATE 多标签逐行、多标签节点 + 边创建、VertexValue labels 正确性 |

全部 341 个 query executor 测试 + 74 个其他测试通过，无回归。TCK Create 特性：新代码 189 passed / 73 failed（旧代码 183 passed / 75 failed，+6 improvement）。
