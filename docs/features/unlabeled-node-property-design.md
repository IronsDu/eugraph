# 无标签节点属性访问设计

## 问题

当前 binder 对 `CREATE ({name: 'foo'})` 产生的无标签节点，后续 `RETURN n.name` 时报错：
```
Property 'name' not found on any label for variable 'n'
```

因为 binder 的属性查找链路是 `变量 → label → property`，无标签节点没有 label，查找失败。TCK 分类3，影响 444 个场景。

## 方案：内部匿名标签 `__anon__` + 动态属性查找

### 核心思路

两件事分开处理：

1. **存储**：每个 Graph 预留一个内部标签 `__anon__`，无标签节点的属性全部存储在这个标签下。存储层零改动。
2. **属性访问**：新增 `BoundDynamicPropertyRef` 表达式——和 `BoundPropertyRef` 不同，它不维护 `(label_id, prop_id)` 的 candidates 列表，而是只保存**属性名字符串**。运行时遍历顶点所有标签的所有属性定义，按名字匹配找到值。

用动态查找替代编译期 prop_id 解析，避免了同一条语句中 CREATE 属性还没注册到 catalog 就 RETURN 的问题。

### 为什么不沿用边类型的 pending_props 延迟解析

边类型有两阶段解析（binder 标记 `pending_props` → 物理层 DDL 算子注册 → 写入时按名解析）：

| | 边类型 | 顶点标签 |
|------|------|------|
| CREATE 侧 | `BoundCreateEdgeOp.pending_props` 按名字暂存 | — |
| 物理层 DDL | `CreateEdgeLabelPhysicalOp` / `AlterEdgeLabelPhysicalOp` | **未实现** |
| 访问侧 | 不涉及时（CREATE 后的 RETURN 不会引用边属性） | `BoundPropertyRef` 必须知道编译期 `prop_id` |

**根因**：边的属性只在 CREATE 时出现，CREATE 后的 RETURN 不会引用边属性——所以延迟解析到执行时没问题。但顶点的属性在 CREATE 和 MATCH/RETURN 中都会出现。同一个语句 `CREATE ({name: 'foo'}) RETURN n.name` —— binder 处理 RETURN 时属性还不存在于 catalog，`BoundPropertyRef` 又必须知道 `prop_id`，走不通。

但如果换成**运行时按名字查找**，就不需要预注册属性了。

---

## 详细设计

### 1. Metadata 初始化

每个 Graph 创建时（`GraphManager::createGraph`），自动创建 `__anon__` 标签：

```
createLabel("__anon__")
  → label_id = next_label_id (e.g., 1)
  → LabelDef { name="__anon__", properties={} }   // 空属性集，不预注册任何属性
```

`__anon__` 标记为内部标签：
- 不在 `Catalog::labelNameToId()` 中暴露（用户无法 `MATCH (n:__anon__)`）
- 但保留在 `Catalog::labelDefs()` 中（`AllNodeScanPhysicalOp` 能扫到，`allLabels()` 包含它供弱类型查找）

多图隔离：每个 graph 独立的 `__anon__`，label_id 在不同 graph 中可能不同，通过 `Catalog::getAnonLabelId()` 获取。

### 2. BoundDynamicPropertyRef（新表达式）

```cpp
struct BoundDynamicPropertyRef {
    BoundExpression object;     // 被访问的对象（通常 BoundColumnRef）
    std::string property;       // 属性名字符串
    BoundType result_type = BoundType::Any();  // 运行时决定实际类型
};
```

添加到 `BoundExpression` variant。

**求值逻辑**（VectorizedEvaluator 新增）：

```cpp
Value evalDynamicPropertyRef(const BoundDynamicPropertyRef& ref, const VertexValue& vv,
                             const EvalContext& ctx) {
    for (const auto& [label_id, props_vec] : vv.properties) {
        auto* ldef = ctx.catalog.lookupLabel(label_id);
        if (!ldef) continue;
        for (const auto& pd : ldef->properties) {
            if (pd.name == ref.property) {
                if (pd.id < props_vec.size()) {
                    const auto& pv = props_vec[pd.id];
                    if (pv.has_value()) return propertyValueToValue(*pv);
                }
            }
        }
    }
    return Value{};  // null
}
```

查找开销 O(标签数 × 属性数)，对于无标签节点（通常单标签、少属性）可忽略。

### 3. Binder 属性访问路径

**弱类型 `n.name`**（binder.cpp 约 1513 行）：

```
bindExpression(PropertyAccess(n, "name")):
  // 1. 先走现有路径：查找 catalog 中所有标签
  candidates = lookupPropertyAcrossLabels(all_labels, "name")

  // 2. 如果找到 → 创建 BoundPropertyRef（不变）
  if candidates 非空:
    return BoundPropertyRef(candidates)

  // 3. 如果找不到 → 创建 BoundDynamicPropertyRef（新路径）
  return BoundDynamicPropertyRef(n, "name", BoundType::Any())
```

`all_labels` 包含 `__anon__`（因为 `allLabels()` 返回完整 `label_defs_`），所以如果 `__anon__` 注册了属性，会命中路径 2。没注册则走路径 3。两步互相兜底。

**强类型 `n::Person.name`**：不变。强类型直接指定 `label_id`，属性不存在就报错。不走动态路径。

### 4. CREATE 路径

**当前实现**（bind_mutation.cpp）：

```cpp
if (start_labels.empty()) {
    create_node->label_ids.push_back(catalog_.getAnonLabelId());
} else {
    create_node->label_ids = start_labels;
}
```

当 node pattern 有 `labels=[]` 时，使用 `__anon__` 标签。多标签模式下，属性按标签解析（单个命中→该标签，无命中→`pending_props`/`__anon__`，多个命中→歧义报错）。

### 5. 物理层

**AllNodeScanPhysicalOp**：`label_map_` 来自 `PlanContext::label_name_to_id`（从 meta store 加载）。`__anon__` 在 meta store 中存在，因此已在 `label_map_` 中，无需额外注入。对 `MATCH (n)` 扫描自动覆盖无标签节点。

**LabelScanPhysicalOp**：只扫描用户可见标签，不扫描 `__anon__`（因为 `__anon__` 不在 `Catalog::labelNameToId()` 中，用户无法写 `MATCH (n:__anon__)`）。

**CreateNodePhysicalOp**：`pending_props` 字段支持按名暂存未注册属性。物理层执行时，`pending_props` 统一走 `__anon__` 轻量路径（不再区分 anon_mode / 非 anon_mode）：

```
executeChunk():
  // Phase 1b: __anon__ 属性轻量注册（仅首次执行）
  if (!anon_registered_ && !pending_props_.empty()):
    // 自动获取或创建 __anon__ 标签
    LabelId anon_lid = getAnonLabelId(label_defs_)
    if (anon_lid == INVALID_LABEL_ID):
      anon_lid = co_await meta_.createLabel("__anon__", {})
      co_await store_.createLabel(anon_lid)
    for each (prop_name, expr) in pending_props_:
      pid = co_await meta_.getOrCreateAnonPropId(prop_name, ANY)
      resolved_pending_.emplace_back(anon_lid, pid, std::move(expr))
    anon_registered_ = true

  // Phase 2: 逐行创建
  for each row from child (or standalone):
    VertexId vid = co_await meta_.nextVertexId()  // 动态分配
    auto label_props = buildLabelProps(chunk, row) // 含 __anon__ 属性
    co_await insertVertex(vid, label_props)
    // 输出 = child 列 + 新 VertexValue 列
```

`AlterVertexLabelPhysicalOp` 已删除。所有顶点的 `pending_props` 无论目标标签是什么，都通过 `__anon__` 轻量路径处理。

**`__anon__` 轻量级 prop_id 分配**（替代 ALTER）：
- 通过 `IAsyncGraphMetaStore::getOrCreateAnonPropId()` 接口实现
- 内部使用 `std::mutex` + `std::unordered_map<string, uint16_t>` 缓存
- 首次遇到新属性名时，分配 `prop_id = properties.size()`，追加到 `__anon__` 的 `LabelDef`，异步持久化
- 不需要 `AlterVertexLabelPhysicalOp`（已删除），不需要同步 DDL 事务
- 所有顶点 pending_props 统一走 `__anon__` 轻量路径

**labels 过滤**：所有返回 `VertexValue.labels` 给用户的位置，过滤掉 `__anon__id`：
- `AllNodeScanPhysicalOp` 构造 `VertexValue` 时
- `LabelScanPhysicalOp` 构造 `VertexValue` 时
- `labels()` 函数求值时

**投影下推（Watch Point）**：`BoundDynamicPropertyRef` 不携带具体的 `label_id` 和 `prop_id`，因此 `addPropertyRequirement` 无法添加精确的属性需求。scan 算子不会主动加载该属性——但求值时需要该属性存在于 `VertexValue.properties` 中。

解决方式：在 `bindExpression` 创建 `BoundDynamicPropertyRef` 时，额外为 `__anon__` 标签注册一个特殊标记，让 pushdown 知道需要加载 `__anon__` 标签下的**全部属性**：

```cpp
// 伪码
if (created DynamicPropertyRef) {
    // 标记需要加载 __anon__ 的所有存储属性
    ctx_.addPropertyRequirement(var_name, __anon__id, ALL_PROPERTIES);
}
```

或者简化：`addAllPropertiesForVariable` 扩展到对 `__anon__` 也添加需求（即使 schema 为空），物理扫描时对 `__anon__` 做全属性加载。

### 6. SET/REMOVE 路径

**SET n:Person**：
```
起点: labels=[], props={__anon__: {name: 'foo'}}
SET n:Person → labels=[__anon__, Person]
用户可见: labels=[Person]
```

`MATCH (n:Person) RETURN n.name`：
- `label_defs_` 包含 Person 和 `__anon__`
- `lookupPropertyAcrossLabels` 在 Person 找 `name`，找不到（Person 没注册 name）→ 走 `BoundDynamicPropertyRef("name")`
- 运行时遍历 `vv.properties`：Person 为空，`__anon__` 有 name → 返回 "foo"

**SET n.name = 'bar'**（混合模式下有两种模式）：

**强模式** `SET n::Label.name = 'bar'`：
- Binder 阶段校验 Label 是否有 `name` 属性，不存在则报错
- 运行时直接写入指定 Label 的 prop_id 存储

**便捷模式** `SET n.name = 'bar'`（无 `::`）：
- 运行时根据节点实际持有的标签推断归属
- 遍历节点实际标签的 schema，查找哪些标签定义了 `name`
- 恰好一个标签命中 → 写入该标签
- 无标签命中 → 写入 `__anon__`（通过 `getOrCreateAnonPropId` 轻量分配）
- 多个标签命中 → 运行时报错，要求使用 `::` 显式指定

**REMOVE n:Person**：回退为无标签节点，`__anon__` 保留。

**DELETE n**：物理层级联删除，`__anon__` 属性随顶点删除。

### 7. 表达式求值汇总

| 场景 | AST | BoundExpression | Evaluator | 变化 |
|------|-----|----------------|-----------|------|
| `n.name` 弱类型, 属性已注册 | `PropertyAccess(Variable(n), "name")` | `BoundPropertyRef { candidates=[(Person, 3, String), ..., (__anon__, 1, ANY)] }` | 按 `(label_id, prop_id)` 索引 candidates | `__anon__` 无条件加入 candidates |
| `n.name` 弱类型, 属性未注册 | 同上 | `BoundDynamicPropertyRef { property="name" }` | 遍历 `vv.properties` 的所有 label 的所有属性定义，按名匹配 | **新表达式** |
| `n::Person.name` 强类型 | `PropertyAccess(LabelCastExpr(n, "Person"), "name")` | `BoundPropertyRef { candidates=[(Person, 3, String)] }` | 按 `(label_id, prop_id)` 索引 | **无变化** |
| `n.age > 30` (WHERE) | `BinaryOp(GT, PropertyAccess(n, "age"), Literal(30))` | `BoundBinaryOp(GT, BoundPropertyRef/DynamicPropertyRef, BoundLiteral(30))` | 先求值 PropertyRef，再比较 | Binder 成功即无变化 |
| `RETURN n` | `Variable(n)` | `BoundColumnRef(n, Vertex)` | 返回 VertexValue，labels 过滤 | labels 列表过滤 `__anon__` |
| `RETURN n.name` | `PropertyAccess(Variable(n), "name")` | 同弱类型 | 同弱类型 | Binder 成功即无变化 |
| `labels(n)` 函数 | `FunctionCall("labels", [n])` | `BoundFunctionCall(labels_fn, [BoundColumnRef(n)])` | `getVertexLabels(vid)` → 已过滤的列表 | **无变化**（labels 已过滤） |
| `CREATE ({name:'foo'})` | `CreateClause { NodePattern { labels=[], props={name:'foo'} } }` | `BoundCreateNodeOp { label_ids=[__anon__id], pending_props=[("name", lit)] }` | 物理层按名写入 `__anon__` | labels 为空 → `label_ids`=[__anon__id] |
| `CREATE (n:Person {name:'foo'})` | `CreateClause { NodePattern { labels=[Person], props=...} }` | `BoundCreateNodeOp { label_ids=[Person_id], label_properties=[(pid, lit)] }` | 不变 | **无变化** |
| `MATCH (n)` | `MatchClause { NodePattern { labels=[] } }` | `BoundScanOp` → `AllNodeScanPhysicalOp` | 扫描所有顶点（含 `__anon__` 节点） | labels 过滤 + 注入 `__anon__` |
| `MATCH (n:Person)` | `MatchClause { NodePattern { labels=[Person] } }` | `BoundLabelScanOp(Person)` → `LabelScanPhysicalOp` | 仅扫描 Person 标签节点 | **无变化** |
| `SET n:Person` on 无标签节点 | `SetClause { SET_LABELS, target=n, labels=[Person] }` | `BoundSetOp { SET_LABELS, var_name=n, label=Person }` | `addVertexLabel(vid, Person_id)` | __anon 保留 |
| `SET n.name = 'bar'` | `SetClause { SET_PROPERTY, target=PropertyAccess(n, name), expr='bar' }` | `BoundSetOp { SET_PROPERTY, var_name=n, prop_name="name", strong_mode=false }` | 便捷模式：运行时推断写入目标标签 | 根据 `source_label` 判定目标标签 |
| `SET n::Person.name = 'bar'` | `SetClause { SET_PROPERTY, target=PropertyAccess(LabelCastExpr(n, "Person"), name), expr='bar' }` | `BoundSetOp { SET_PROPERTY, var_name=n, prop_name="name", strong_mode=true, label_id=Person_id, prop_id=... }` | 强模式：编译期校验，直接写入 Person | 属性不存在于 Person → binder 报错 |

### 8. 多标签交互

**场景 A: 无标签 → SET n:Person → 查询属性**

```
起点: labels=[], props={__anon__: {name: 'foo'}}
SET n:Person
  → labels=[__anon__, Person], props={__anon__: {name: 'foo'}, Person: {}}
用户看到: labels=[Person]

MATCH (n:Person) RETURN n.name
  → weak-type lookup: catalog 中 Person 没有 'name' 属性
  → 创建 BoundDynamicPropertyRef("name")
  → eval: 遍历 vv.properties → Person 为空 → __anon__ 匹配 name → 返回 "foo"
```

**场景 B: SET n.prop = value 在混合标签状态下**

```
起点: labels=[__anon__, Person], props={__anon__: {name: 'foo'}, Person: {}}

SET n.name = 'bar'
  → source_label 取决于变量来源
  → 若来自 MATCH (n) 无标签: source_label = __anon__id → 覆盖 __anon__: {name: 'bar'}
  → 若来自 MATCH (n:Person): source_label = Person_id → 写入 Person: {name: 'bar'}

MATCH (n:Person) RETURN n.name
  → BoundDynamicPropertyRef("name")
  → eval: trials → Person 匹配 name → 返回 "bar" ✅ (Person 先于 __anon__)
```

**场景 C: REMOVE n:Person 后回退**

```
起点: labels=[__anon__, Person], props={__anon__: {name: 'foo'}, Person: {name: 'bar'}}
REMOVE n:Person
  → labels=[__anon__]
  → Person 标签的属性数据仍保留在 vertex.properties[Person_id]

MATCH (n) RETURN n.name
  → weak-type lookup: catalog 中 Person 仍有 'name' (schema 未删除)
  → BoundPropertyRef { candidates=[(Person, ..., String), (__anon__, ..., Any)] }
  → eval: Person 匹配成功，返回 "bar" ✅
```

> **真正的隐患**：`REMOVE n:Person` 后再 `DROP VERTEX LABEL Person`，Person schema 被删除，`vertex.properties[Person_id]` 数据成为孤儿。但这是极少见的 DDL + DML 组合场景，openCypher TCK 不涉及，可以暂不处理。

### 9. 改动清单

| 文件 | 改动 |
|------|------|
| `src/query/planner/bound_expression/bound_dynamic_property_ref.hpp` | **新文件**：BoundDynamicPropertyRef 结构体 |
| `src/query/planner/bound_expression/bound_expression_fwd.hpp` | variant 加入 `std::unique_ptr<BoundDynamicPropertyRef>` |
| `src/query/catalog/catalog.hpp` | 新增 `getAnonLabelId()` 方法，`load()` 中识别并保存 |
| `src/storage/graph_manager.cpp` | `createGraph` 时自动创建 `__anon__` 标签 |
| `src/query/planner/binder.cpp` | (1) 弱类型属性查找失败时创建 `BoundDynamicPropertyRef` (2) `bindCreate`：`INVALID_LABEL_ID` → `__anon__id` (3) `applyProjectionPushdown`：跳过动态属性 |
| `src/query/executor/vectorized_evaluator.cpp` | 新增 `evalDynamicPropertyRef` |
| `src/query/planner/column_resolver.cpp` | 新增 `BoundDynamicPropertyRef` 分支 |
| `src/query/physical_plan/operator/all_node_scan_physical_op.cpp` | `label_map_` 注入 `__anon__`；构造 `VertexValue` 时过滤 labels |
| `src/query/physical_plan/operator/label_scan_physical_op.cpp` | 构造 `VertexValue` 时过滤 labels |
| `tests/test_query_executor.cpp` | 新增无标签节点属性测试 |

### 10. 测试用例

#### 弱类型：无标签节点属性访问

```
TCK: Return2.feature [2] "Returning property on an unlabeled node"
  CREATE ({num: 1})
  MATCH (a) RETURN a.num                            // BoundDynamicPropertyRef("num")
  期望: 1

TCK: Match1.feature [4] "Use properties on all nodes"
  CREATE ({name: 'bar'}), ({name: 'monkey'}), ({firstname: 'bar'})
  MATCH (n {name: 'bar'}) RETURN n                   // inline filter: name='bar'
  期望: ({name: 'bar'})

TCK: Aggregation1.feature [1] "Counting properties on unlabeled nodes"
  CREATE ({name: 'a', num: 33}), ({name: 'a'}), ({name: 'b', num: 42})
  MATCH (n) RETURN n.name, count(n.num)
  期望: ('a', 1), ('b', 1)
```

#### 弱类型：有标签节点属性访问（不变）
```
TEST LabeledWeakTypeNotAffected:
  CREATE (n:Person {name: 'Alice'})
  MATCH (n:Person) RETURN n.name                      // BoundPropertyRef(candidates)
  期望: "Alice"

TEST LabeledMultiCandidate:
  // 假设 Person 和 Employee 都有 'name' 属性...
  // n.name candidates = [(Person, String), (Employee, String)]
  // 走 BoundPropertyRef，多 candidate 类型一致 → result_type = String
```

#### 强类型：LabelCast 属性访问（不变）
```
TEST StrongTypedProperty:
  CREATE (n:Person {name: 'Bob'})
  RETURN n::Person.name                               // BoundPropertyRef(single candidate)
  期望: "Bob"
```

#### 无标签 → 加标签 → 属性访问
```
TEST UnlabeledToLabeled:
  CREATE ({name: 'foo'})
  SET n:Person
  MATCH (n:Person) RETURN n.name                      // BoundDynamicPropertyRef fallback
  期望: "foo"

TEST UnlabeledToLabeledSetProp:
  CREATE ({name: 'foo'})
  SET n:Person
  SET n.name = 'bar'
  MATCH (n) RETURN n.name                             // Person 标签优先
  期望: "bar"
```

#### 无标签节点返回完整顶点
```
TEST ReturnUnlabeledVertex:
  CREATE ({name: 'anon'})
  MATCH (n) RETURN n                                  // labels=[]
```

#### Filter/WHERE 混合
```
TEST UnlabeledWhereFilter:
  CREATE ({age: 42}), ({age: 99})
  MATCH (n) WHERE n.age > 50 RETURN n.age
  期望: 99
```

#### 不同属性类型混合（ANY）
```
TEST UnlabeledMixedTypes:
  CREATE ({val: 'hello'}), ({val: 123})
  MATCH (n) RETURN n.val
  期望: "hello", 123  (运行时决定类型)
```

#### 逗号分隔 CREATE + 属性访问
```
TEST CommaCreateUnlabeledProps:
  CREATE ({x: 1}), ({y: 2})
  MATCH (n) WHERE n.x = 1 RETURN n.x, n.y
  期望: (1, null)
```

#### 同一条语句 CREATE + RETURN（关键场景）
```
TEST SameStmtCreateReturnUnlabeled:
  // 这个查询 binder 处理 RETURN 时属性还不存在
  CREATE ({name: 'test'}) RETURN n.name               // BoundDynamicPropertyRef
  期望: "test"
```

### 11. 设计讨论记录

- **为何不用边的 pending_props 延迟注册模式**：边的属性只在 CREATE 阶段出现，CREATE 后的 RETURN 不引用边属性。顶点的属性在 MATCH/RETURN 中被引用，而 `BoundPropertyRef` 需要编译期 `prop_id`——如果属性还未注册，就无法创建 `BoundPropertyRef`。
- **为何不在 binder 阶段即时注册属性**：binder 应该是纯语义分析，修改 schema 有副作用的做法不干净。第一次讨论倾向即时注册，但后来决定用运行时动态查找替代。
- **`BoundDynamicPropertyRef` 与 `BoundPropertyRef` 的关系**：前者是后者的兜底——catalog 能找到属性就走 `BoundPropertyRef`（编译期索引，零开销），找不到就走 `BoundDynamicPropertyRef`（运行时按名匹配）。两者共存，不是替换。
- **多标签下的候选优先级**：`BoundPropertyRef` 的 candidates 收集无条件包含 `__anon__`，作为最低优先级兜底。`BoundDynamicPropertyRef` 遍历时也是最后尝试 `__anon__`。
- **Evaluator 依赖 EvalContext**：`evalDynamicPropertyRef` 需要访问 catalog（通过 `EvalContext`）才能拿到 label→property 的 name→pid 映射。当前 `EvalContext` 已传递到所有 PhysicalOperator，Evaluator 可通过 `eval_ctx_.catalog` 访问。
- **ColumnResolver 处理**：`BoundDynamicPropertyRef` 的 `object` 字段是 `BoundExpression`，递归解析其中的 `BoundVariableRef` → `BoundColumnRef`。`property` 字符串不需要解析。
- **`BoundDynamicPropertyRef` ≠ 跳过属性注册**：`Vev.properties` 是 `map<LabelId, vector<(prop_id, PropertyValue)>>`，只有 `(pid, value)` 没有名字。`evalDynamicPropertyRef` 通过 `ldef.properties` 做 name→pid 映射——这要求属性已在 metadata 中注册。对于 `__anon__` 标签，注册由 `CreateNodePhysicalOp` 内部的 `getOrCreateAnonPropId()` 轻量完成。`AlterVertexLabelPhysicalOp` 已删除，所有顶点 pending_props 统一走 `__anon__` 轻量路径。
- **同语句 CREATE+RETURN 的完整链路**：`CREATE ({name: 'foo'}) RETURN n.name` → Binder: `bindCreate` 标记 `pending_props`，`bindReturn` 创建 `BoundDynamicPropertyRef("name")`（此时 catalog 无 name）→ PhysicalPlanner: 不再插入 `AlterVertexLabelPhysicalOp`（`__anon__` 跳过）→ CreateNodePhysicalOp 通过 `getOrCreateAnonPropId()` 轻量分配 prop_id → Evaluator: `evalDynamicPropertyRef` 用已注册的 `ldef.properties` 做 name→pid 映射，读到值 ✅
- **跨语句 MATCH+RETURN 的链路**：`CREATE (n:Person {name: 'Alice', nickname: 'Ali'})` → `nickname` 通过 `getOrCreateAnonPropId()` 分配到 `__anon__` → 后续 `MATCH (n:Person) RETURN n.nickname` → Binder 创建 `BoundDynamicPropertyRef("nickname")` → 运行时 `evalDynamicPropertyRef` 遍历 `vertex.properties`（含 `__anon__`），按名匹配找到值 ✅
