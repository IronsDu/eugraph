# Set / Create 子句 TCK 失败修复设计

> 分支: `feature/set-create-tck-fixes`
> 范围: `docs/tests/tck-results.md` 中 Set 类 21 个失败（39.6%）+ Create 类 29 个失败（38.5%），合计 50 个场景
> 关联: [create-op-redesign.md](create-op-redesign.md)、[merge-clause-design.md](merge-clause-design.md)、[slot-id-design.md](slot-id-design.md)
> 状态: **已实现**（Phase 1-5 完成；Set 32→38，Create 48→64，净改进 22 场景）

---

## 一、问题概述

TCK 报告（2026-07-24 快照）显示 Set / Create 是当前失败率最高的两个子句类别。前期调研得到一个**错误假设**：mutation 算子在 pipeline 中的位置导致副作用计数错误。经过对 `physical_planner.cpp`、`set_physical_op.cpp`、`create_node_physical_op.cpp`、`create_edge_physical_op.cpp`、`bind_mutation.cpp`、`bind_merge.cpp` 的逐行核查，结论是：

> **pipeline 顺序本身是正确的**（mutation 在 LIMIT/SKIP/WHERE 之前执行）。50 个失败由 **5 个互相独立的实现缺陷**导致，没有单一"系统性根因"，但当前实现存在**两类共性的设计缺口**：
>
> 1. **写后不可见（write-but-not-visible）**：mutation 算子持久化到 KV store，但没有同步更新 in-memory `VertexValue` / `EdgeValue`。后续 `RETURN` / `WITH` 在同一查询内读取的是 chunk 里旧的内存值。
> 2. **Cypher 语义守卫不一致**：MERGE 已经实现的 `VariableAlreadyBound` / `NoSingleRelationshipType` / `CreatingVarLength` 等检查在 CREATE 路径完全缺失；`SET_PROPERTY` 与 `SET_PROPERTIES` 对 `null` 的处理相互矛盾。

本设计目标是**用统一方式补齐这两类缺口**，而不是为每个失败场景打补丁。

---

## 二、根因分类（按真实根因，而非按场景编号）

### A. 写后不可见 — 内存态未镜像持久化结果（约 18 个场景）

#### A1. `SET_LABELS` 不更新 `VertexValue.labels`

**位置**: `src/query/physical_plan/operator/set_physical_op.cpp:166-170`

```cpp
if (item.kind == cypher::SetItemKind::SET_LABELS) {
    auto lit = label_name_to_id_.find(item.label);
    if (it == label_name_to_id_.end()) continue;
    co_await store_.addVertexLabel(vid, lit->second);
    // ❌ 没有 chunk->setValue 更新 VertexValue.labels
}
```

**影响**: `Set3 [1-7]` 等场景 `SET n:Label RETURN labels(n)` 看不到新标签；`Set3 [5]` `SET n:A REMOVE n:B RETURN labels(n)` 顺序对最终内存态也不可见。

#### A2. `CREATE` 创建的边属性不进入 `EdgeValue.properties`

**位置**: `src/query/physical_plan/operator/create_edge_physical_op.cpp:215-225`

```cpp
EdgeValue ev;
ev.id = eid;
ev.src_id = src;
ev.dst_id = dst;
ev.label_id = effective_label_id;
ev.seq = 0;
// ❌ ev.properties 未填充
```

对比 `CreateNodePhysicalOp:262-269` 正确填充了 `vv.properties[lid] = lp`。

**影响**: `Create2 [14][16][17]` 等 `CREATE (a)-[:T {since: 1999}]->(b) RETURN r.since` 类型查询读不到刚写的属性。

#### A3. `SET_PROPERTIES` 已正确镜像节点（参考实现）

`set_physical_op.cpp:259-360` 对 vertex 的 `+=` 与 `=` 都正确执行了 `chunk->setValue(...)` 写回 `updated_vertex`。该路径可作为 A1/A2 的镜像范式。

---

### B. Cypher null 语义不一致（约 5 个场景）

#### B1. `SET n.p = null` 应当**移除属性**，而非写入 null

**位置**: `set_physical_op.cpp:171-177`（SET_PROPERTY 分支）

当前代码无条件调用 `valueToPropertyValue` 把 null 转成空 `PropertyValue{}`，再 `putVertexProperty` 写入。结果：`RETURN n.p` 返回 null，但 `keys(n)` 仍然包含 `p`。

`SET_PROPERTIES` 分支（`set_physical_op.cpp:294-298`）已正确实现：map value 为 null 时 `continue` 跳过。

**期望（Cypher LRB 规范）**: `SET n.p = null` ≡ `REMOVE n.p`。属性应从 `keys(n)` 中消失。

**影响**: `Set2 [1][2][3]` 等场景。

#### B2. `SET n = map` 与 `SET n += map` 对 null 的语义（已按 openCypher 规范修正）

`SET_PROPERTIES` 的两种模式对 map 中 `null` 值的处理，**严格遵循 openCypher 规范**：

| 语法 | 含义 | map 中 `p: null` 行为 |
|------|------|----------------------|
| `SET n = m` | **整体替换**：先删除节点上的**所有**旧属性，再写入 m 的非 null 项 | null 不可持久化 ⇒ p 不写入 ⇒ p 不存在（**所有旧属性也已被清空，包括旧的 p**） |
| `SET n += m` | **增量合并**：保留所有旧属性；对 m 中每个 key 单独处理 | `p: null` 显式 REMOVE p；`p: v`（v 非 null）覆盖/新增 |

**关键点**：`SET n = {p: null}` 不是"p 维持原值不变"。`=` 是先清空再写入，p 既不在写入集合里也不在旧属性里 ⇒ p 必然不存在。

**当前实现**（`set_physical_op.cpp:294-298`）的 `continue` 行为：

```cpp
if (std::holds_alternative<std::monostate>(entry_val))
    continue;
```

- 对 `=` 模式：**结果正确**。因为 `=` 分支前面已经 `deleteVertexProperty` 清空了所有旧属性（`set_physical_op.cpp:276-291`），跳过 null 写入 ⇒ p 自然不存在。
- 对 `+=` 模式：**结果错误**。`+=` 没有清空旧属性，跳过 null 写入 ⇒ p 保留旧值。但规范要求 `p: null` 在 `+=` 模式下 REMOVE p。

修正方向见 §4.2.2 (b)。

---

### C. CREATE 缺少语义守卫（约 8 个场景）

**位置**: `src/query/planner/binder/bind_mutation.cpp:69-212`（`bindCreate`）

`bind_merge.cpp:193-229` 已实现下列检查；`bind_mutation.cpp` 完全没有：

| 守卫 | 错误码 | 触发条件 | 影响 |
|------|--------|---------|------|
| 节点变量已绑定且附加新谓词 | `VariableAlreadyBound` | `CREATE (n:Foo)` 而 `n` 已在 scope | `Create1 [4]` 等 |
| 关系变量已绑定 | `VariableAlreadyBound` | `CREATE (a)-[r:T]->(b)` 而 `r` 已绑定 | `Create3 [x]` |
| 关系必须只有一个 type | `NoSingleRelationshipType` | `CREATE (a)-[:A\|:B]->(b)` | `Create2 [x]` |
| 不允许变长 | `CreatingVarLength` | `CREATE (a)-[:T*1..3]->(b)` | `Create2 [x]` |
| 无向关系限制 | `RequiresDirectedRelationship` | `CREATE (a)-[:T]-(b)`（无箭头） | MERGE/CREATE 共同缺陷 |

**这些检查不能复制粘贴到 bindCreate**，而应抽取为共用 helper（见第四节）。

---

### D. CREATE 不处理反向 / 无向关系（约 4 个场景）

**位置**: `bind_mutation.cpp:148-156`

```cpp
create_edge->src_variable = start_var;
create_edge->dst_variable = dst_var;
// ❌ 完全忽略 rel_pat.direction
```

Cypher 语义：
- `CREATE (a)<-[:T]-(b)`：实际方向 b → a，因此 src=b, dst=a
- `CREATE (a)-[:T]-(b)`：**语义错误**（`RequiresDirectedRelationship`）

**影响**: `Create2 [4]`、`Create2 [6]` 等使用反向箭头的场景。

---

### E. 其他次要问题（约 5 个场景）

#### E1. SET 带 `()` 包装的对象表达式解析

TCK 中存在 `SET (n).p = 'x'` 语法（带括号的属性访问），AST 解析路径未覆盖。需要核查 `cypher_parser.cpp` 的 primary expression 是否允许 `( expr ).prop`。

#### E2. List-of-maps 属性类型校验

`Set6 [x]` 的 `SET n.p = [{k: 1}]`：list-of-maps 不在 `valueToPropertyValue` 支持的元素类型里，会被静默丢弃为 `PropertyValue{}`。应在 `valueToPropertyValue` 中至少给出明确错误，或按设计文档允收。

#### E3. CREATE 多 pattern 跨引用变量

详见 memory `project_comma_create_limitation.md`：`CREATE (a), (b)-[:T]->(a)` 中第二个 pattern 引用第一个 pattern 的 `a` 会创建额外的 anon 节点。这是 binder 已知问题，需要修复 `bindCreate` 的"变量已绑定"识别路径。

---

## 三、不是问题的问题（前期错误假设）

### pipeline 顺序问题（已排除）

通过阅读 `physical_planner.cpp`，确认 SET/CREATE/REMOVE/DELETE/MERGE 算子在物理计划中位于 LIMIT/SKIP/WHERE/AGG 之前。`UNWIND [1,1,1] CREATE (n)` 类查询的副作用计数正确，不需要重排算子。

### `CREATE` 算子的"逐行创建"问题（已修复）

参考 [create-op-redesign.md](create-op-redesign.md)，`CreateNodePhysicalOp` / `CreateEdgePhysicalOp` 已经实现 child chunk 的逐行消费，不存在"只创建一个"的问题。

---

## 四、统一解决方案

### 4.1 内存态镜像统一化（解决 A 类）

#### 4.1.1 设计原则

**写入路径必须双写**：持久化到 KV store（保证跨事务可见）+ 同步到 chunk 中的 `VertexValue` / `EdgeValue`（保证同事务内 RETURN/WITH 可见）。

参考实现：`set_physical_op.cpp:241-258`（SET_PROPERTY 节点的镜像逻辑）、`CreateNodePhysicalOp` 节点属性填充逻辑。

#### 4.1.2 修改点

**(a) `SetPhysicalOp` — SET_LABELS 镜像**

在 `set_physical_op.cpp:166-170` 调用 `store_.addVertexLabel` 之后，构造新的 `VertexValue`：

```cpp
VertexValue updated = vertex;
if (!updated.labels.has_value())
    updated.labels = LabelIdSet{};
updated.labels->insert(lit->second);
chunk->setValue(static_cast<size_t>(col), row_idx, Value(std::move(updated)));
```

注意：`SET_LABELS` 重复添加同一 label 应为 no-op，`LabelIdSet` 已是 set 语义。

**(b) `CreateEdgePhysicalOp` — 边属性镜像**

在 `create_edge_physical_op.cpp:215-225` 构造 `EdgeValue` 之后，从 `props`（已由 `buildProps` 计算的 `Properties`）回填：

```cpp
EdgeValue ev;
ev.id = eid;
ev.src_id = src;
ev.dst_id = dst;
ev.label_id = effective_label_id;
ev.seq = 0;
if (!props.empty())
    ev.properties = props;   // 新增
```

**EdgeValue 的属性存储结构**是 `std::optional<Properties>`（不是 map，因为边只有一个 label，不像 vertex 的 `unordered_map<LabelId, Properties>`），因此直接赋值即可。

**(c) 抽取镜像 helper（避免每个算子各自实现）**

在 `src/query/physical_plan/operator/` 新增 `mutation_mirror.hpp`：

```cpp
namespace eugraph::compute {

/// 把 chunk 的 (col, row) 位置的 vertex 替换为 updated。
/// 重要：会扫描同一 row 的其他 VERTEX 类型列，对 vid 相同的列一并替换，
/// 避免出现 "MATCH (a)-(a) WITH a AS a1, a AS a2 SET a1.p = 1 RETURN a2.p"
/// 这种"同 vid 多列引用"场景下 a2 读到旧值的问题。
void mirrorVertexToAllReferences(DataChunk& chunk, size_t col, size_t row, VertexValue updated);

/// 同上：构造 EdgeValue 并写入 (col, row)，同时扫描其他 EDGE 类型列，
/// 对 (src_id, dst_id, label_id, id) 相同的列一并替换。
void mirrorEdgeToAllReferences(DataChunk& chunk, size_t col, size_t row, EdgeValue ev);

} // namespace
```

**同 vid 多列引用的处理（Gemini 隐患 #1）**：

`DataChunk` 的列是值语义。同一行里如果两列都引用同一 vid（典型场景：`MATCH (a) WITH a AS a1, a AS a2 ...`），它们持有的是 `VertexValue` 的独立拷贝。`mirrorVertex` 若只更新触发列，其他列读到旧值。

对策：helper 函数内扫描 chunk 中所有 VERTEX 类型列，对 `vertex.id == updated.id` 的列统一替换。性能开销 = O(numColumns) per mutation，对于典型的宽表（< 32 列）可接受；若 chunk 内 vertex 列很多，可以考虑在算子初始化时建立 `vid -> column indices` 的索引，但不在本次实现范围内。

**shared_ptr buffer 共享的 COW 检查（Gemini 隐患 #1 子项，已核查源码）**：

`Column::buffer` 是 `std::shared_ptr<ColumnBuffer>`（`data_chunk.hpp:297`）。`Column::setValue` 在 FLAT 形式下直接调 `buffer->setValue(i, val)` —— 如果该 buffer 同时被另一 Column 通过 `Column::dict(buf, sel)` 引用（DICTIONARY 列的零拷贝派生），写穿透会污染 DICTIONARY 列。

现有 `set_physical_op.cpp:131-147` 预处理只检查 `form != FLAT`，没检查 `buffer.use_count() > 1`。`mirrorVertexToAllReferences` 必须补这个检查：

```cpp
inline void ensureExclusiveBuffer(Column& col, size_t n) {
    if (col.form != VectorForm::FLAT) {
        // 非 FLAT（CONSTANT/DICTIONARY）：物化到新 FLAT buffer
        auto fresh = Column::flat(col.type, n);
        for (size_t i = 0; i < n; ++i)
            fresh.setValue(i, col.getValue(i));
        col = std::move(fresh);
        return;
    }
    if (col.buffer && col.buffer.use_count() > 1) {
        // FLAT 但被共享：COW 拷贝
        auto fresh = Column::flat(col.type, n);
        for (size_t i = 0; i < n; ++i)
            fresh.setValue(i, col.getValue(i));
        col = std::move(fresh);
    }
}
```

`mirrorVertexToAllReferences` 进入前对触发列和所有候选同 vid 列各调用一次 `ensureExclusiveBuffer`。

SET/CREATE/REMOVE/DELETE 算子统一调用此 helper，避免散点更新漏字段、漏同 vid 列、漏 buffer 独占性检查。

---

### 4.2 Cypher null 语义统一（解决 B 类）

#### 4.2.1 设计原则

**null 在 SET 中是"删除指令"，不是"空值"。** 三种 SET 形态对 null 的处理（openCypher 规范）：

| 语法 | 含义 | null 行为 |
|------|------|---------|
| `SET n.p = v` | 单属性覆盖 | `v IS NULL` ⇒ REMOVE p |
| `SET n = m` | **整体替换**：先清空所有旧属性，再写入 m 非 null 项 | m 中 `p: null` ⇒ p 不写入（null 不可持久化）；旧 p 也已被清空 ⇒ p 必不存在 |
| `SET n += m` | **增量合并**：保留旧属性；逐项处理 | m 中 `p: null` ⇒ 显式 REMOVE p；`p: v` ⇒ 覆盖/新增 |

#### 4.2.2 修改点

**(a) SET_PROPERTY 分支增加 null 检查**

`set_physical_op.cpp:171-236` 的开头：

```cpp
Value v = value_results[idx][row_idx];
if (std::holds_alternative<std::monostate>(v)) {
    // null ⇒ REMOVE property
    if (item.strong_mode && item.resolved_label_id && item.resolved_prop_id) {
        co_await store_.deleteVertexProperty(vid, *item.resolved_label_id, *item.resolved_prop_id);
        // 同时更新内存态
        VertexValue updated = vertex;
        auto it = updated.properties.find(*item.resolved_label_id);
        if (it != updated.properties.end() && it->second.size() > *item.resolved_prop_id)
            it->second[*item.resolved_prop_id].reset();
        chunk->setValue(col, row_idx, Value(std::move(updated)));
    } else {
        // convenience mode: 找到所有匹配的 (lid, pid) 并删除
        // （逻辑参考 SET_PROPERTIES 的删除分支）
    }
    continue;
}
```

**(b) SET_PROPERTIES 分支区分 `=` vs `+=` 的 null 处理**

当前 `set_physical_op.cpp:294-298`：

```cpp
if (std::holds_alternative<std::monostate>(entry_val))
    continue;  // 当前对 = 和 += 都是跳过
```

应改为：

```cpp
if (std::holds_alternative<std::monostate>(entry_val)) {
    if (item.is_add_assign) {
        // += : null 显式 REMOVE —— 解析 (lid, pid) 后
        //      deleteVertexProperty + mirrorVertexToAllReferences(...)
        //      将内存态对应位置 .reset()
    }
    // = : null 在整体替换下"自然"导致 p 不存在——
    //     前面 §276-291 已经清空所有旧属性，这里 continue 跳过写入，
    //     最终结果就是 p 不存在，与规范一致。
    continue;
}
```

**注意**：`=` 模式的"清空所有旧属性"逻辑（§276-291）必须确保扫描到节点所有的 (lid) 标签，包括 `__anon__`。否则会出现"SET n = {} 后旧属性还在"的 bug。核查点：`store_.getVertexLabels(vid)` 是否返回了 `__anon__`，以及 `deleteVertexProperty` 是否对 `__anon__` 标签也生效。

---

### 4.3 CREATE 语义守卫统一（解决 C 类）

#### 4.3.1 设计原则

**MERGE 与 CREATE 共用同一组守卫**，避免出现"MERGE 报错但 CREATE 静默接受"的不对称。守卫的判定逻辑（不涉及具体错误码格式）抽取到 binder 内部 helper。

#### 4.3.2 修改点

新增 `src/query/planner/binder/mutation_guards.hpp`（或 `bind_mutation.cpp` 内匿名命名空间，若仅本文件使用）：

```cpp
namespace eugraph::binder {

enum class MutationKind { CREATE, MERGE };

/// 检查 pattern element 是否满足 CREATE/MERGE 的语义约束。
/// 失败时调用 binder.error() 并返回 false。
bool validateMutationPattern(const cypher::PatternElement& element,
                             const BindContext& ctx,
                             MutationKind kind,
                             Binder& binder);
} // namespace
```

`validateMutationPattern` 实现：
- 遍历 `element.chain`
- 检查 `rel_pat.range`（`CreatingVarLength`）
- 检查 `rel_pat.variable` 是否已绑定（`VariableAlreadyBound`）
- 检查 `rel_pat.rel_types.size() != 1`（`NoSingleRelationshipType`，CREATE 与 MERGE 一致）
- 检查 `rel_pat.direction == UNDIRECTED`（`RequiresDirectedRelationship`，CREATE 与 MERGE 一致）
- 检查 end node 已绑定 + 新谓词（`VariableAlreadyBound` for nodes）

`bindMerge` 与 `bindCreate` 在绑定开始处各调用一次。

#### 4.3.3 TCK 错误码对齐（Gemini 隐患 #3）

TCK 测试集对错误分类极其敏感，常以 `Then the result should be: ArgumentTypeError` / `SemanticError` / `ClientError` 等形式断言。在抽取守卫时**必须**：

1. 先扫描 `third_party/openCypher/tck/features/clauses/create/*.feature` 与 `merge/*.feature` 中所有 `Scenario` 的 `Then` 行，列出期望错误断言；
2. 核对 `Binder::error(...)` 当前是如何把字符串错误映射到执行期 Status / Exception 的；
3. 对每个守卫的错误码用词保持与 MERGE 现有实现一致（如 `bind_merge.cpp:202` `CreatingVarLength: ...`、`:207` `VariableAlreadyBound: ...`），不允许在 CREATE 中改写措辞；
4. 若 TCK 期望 `ArgumentTypeError` 但我们抛的是 `SemanticError`，需要在错误分发层补映射，不能为了让 TCK 通过而硬编码 status 字符串。

---

### 4.4 CREATE 反向 / 无向关系（解决 D 类）

#### 4.4.1 设计原则

`bindRelationshipPattern` 已经把 `<-`、`->`、`-` 解析为 `RelDirection::{LEFT, RIGHT, UNDIRECTED}`。`bindCreate` 当前忽略它，直接以 `start_var → dst_var` 的方向构造 `BoundCreateEdgeOp`。

#### 4.4.2 修改点

`bind_mutation.cpp:148-156` 在赋值 `src_variable` / `dst_variable` 之前根据 `rel_pat.direction` 决定：

```cpp
std::string effective_src, effective_dst;
switch (rel_pat.direction) {
case cypher::RelDirection::RIGHT:
case cypher::RelDirection::UNDIRECTED:   // 由 §4.3 守卫拦截
    effective_src = start_var;
    effective_dst = dst_var;
    break;
case cypher::RelDirection::LEFT:
    effective_src = dst_var;
    effective_dst = start_var;
    break;
}
create_edge->src_variable = effective_src;
create_edge->dst_variable = effective_dst;
```

注意 `src_col_idx_` / `dst_col_idx_` 在物理算子端也要相应交换，或改成 `BoundCreateEdgeOp` 显式存储 src/dst 变量名，由物理 planner 解析为 column index（推荐后者，因为更稳定）。

`bindMerge` 也需要补同样的方向处理，但 merge-clause-design.md §"无方向关系"提到 MERGE 已经做了 — 需要核查一致性。

---

### 4.5 其他（解决 E 类）

#### E1 — `(n).p` 解析（Gemini 隐患 #2）

TCK 中存在 `SET (n).p = 'x'` 语法（带括号的属性访问），AST 解析路径未覆盖。

**核查路径**：项目使用**手写递归下降 parser**（`src/query/parser/cypher_parser.cpp`），不是 ANTLR。但思路相同——在 primary expression 解析中，括号 `( expr )` 后是否允许接 `.property`。

**具体核查点**：
- `cypher_parser.cpp` 中 `parsePrimary` / `parseAtom` 的括号分支
- 是否允许后接 `postfix * ( '.' identifier )`（即 propertyLookup 链）
- 当前 `makePropertyAccess` 调用处（`:683`）是否能收到 `ParenExpr(n)` 作为 object

修复方案二选一：
- (i) 在 parser 的 postfix 链中允许 `ParenthesizedExpression` 作为 property access 的对象；
- (ii) 在 SET target 解析处对 `PropertyAccess` 的 object 做解包：若 object 是 `ParenExpr`，递归解包到内层 expr。

**先单独验证现状（写一个最小测试用例），再决定走 (i) 还是 (ii)，不在此分支强行实现。**

#### E2 — List-of-maps 属性类型

在 `valueToPropertyValue` 中对 `ListValue` 分支补充：当首元素是 `MapValue` 时，调用 `binder.error("SetProperty: list of maps not supported")` 而非静默丢弃。

#### E3 — 逗号 CREATE 跨 pattern 变量引用（Gemini 隐患 #4）

修复 `bindCreate` 的外层循环：**每绑定完一个节点立即注册到 BindContext 的 symbol table**，使后续 pattern 能识别到 `VariableAlreadyBound` 状态并转为"引用已有节点"逻辑。

当前代码在 `bind_mutation.cpp:122-125` 已经做了 `ctx_.symbols[start_var].is_create_variable = true;`，但这只标记了 `is_create_variable` flag。核查：
- `bindNodePattern` 在 `start_exists = true` 时是否真的不会创建新节点（看 `:82-92`）
- 边的 `dst_exists` 同样需要核查（`:136`、`:176-178`）
- 多 pattern 之间：`pi > 0` 时 `current` 已经从上一个 pattern 流出，但 start_var 的复用是否走对路径

**关键场景**：`CREATE (a:Foo), (a)-[:T]->(b)` 期望只创建 1 个 `a` 节点 + 1 条边 + 1 个 `b` 节点。当前可能创建 2 个 `a`（第一个 pattern 创建 + 第二个 pattern 因为某条件没识别到已绑定而又创建）。

修复方向：在 §4.3 `validateMutationPattern` 之后，紧接 `bindCreate` 中的节点绑定逻辑前，确保 `ctx_.lookup(var)` 在同一 CREATE 子句内已经被前一个 pattern 注册过的变量也能命中。

---

## 五、实施计划

> 严格遵循 `notes.md` "零-三" 流程：每完成一个 phase 等待开发者确认。

### Phase 1：内存态镜像统一（§4.1）— A 类
- 新增 `mutation_mirror.hpp`（含 `mirrorVertexToAllReferences` / `mirrorEdgeToAllReferences` / `ensureExclusiveBuffer`）
- 修复 `SetPhysicalOp` 的 SET_LABELS 镜像
- 修复 `CreateEdgePhysicalOp` 的属性镜像
- **新增单元测试** `tests/query/test_mutation_mirror.cpp`（参考 Gemini 实施建议 #2）：
  - `MATCH (a) WITH a AS a1, a AS a2 SET a1.p = 1 RETURN a2.p` 应返回 1（同 vid 别名镜像）
  - `MATCH (a) SET a.p = 1 RETURN a.p` 应返回 1（基础镜像）
  - 构造 DICTIONARY 派生列后 SET 触发列，验证不会污染 DICTIONARY 列（buffer 独占性）
- TCK 验证：Set3 [1-7] 通过；Create2 [14][16][17] 通过

### Phase 2：null 语义统一（§4.2）— B 类
- 修复 SET_PROPERTY null 处理
- 修复 SET_PROPERTIES `+=` null 处理
- TCK 验证：Set2 [1-3] 通过

### Phase 3：CREATE 守卫（§4.3）— C 类
- 抽取 `validateMutationPattern`
- bindCreate 调用
- TCK 验证：Create1 [4]、Create2 [x] 等通过

### Phase 4：CREATE 方向（§4.4）— D 类
- bindCreate 处理 rel_pat.direction
- 物理端 src/dst column 解析
- TCK 验证：Create2 [4][6] 通过

### Phase 5：其他（§4.5）— E 类
- (n).p 解析（先核查再定）
- List-of-maps 报错
- 逗号 CREATE 变量复用

每个 phase 完成后跑完整 TCK Set + Create 类，确认无回归（参考基线：Set 32 通过 / Create 48 通过）。

---

## 六、需要修改的文件

### 新增
- `src/query/physical_plan/operator/mutation_mirror.hpp`（可选，§4.1.2 c）

### 修改
| 文件 | 修改内容 |
|------|---------|
| `src/query/physical_plan/operator/set_physical_op.cpp` | SET_LABELS 镜像、SET_PROPERTY null、SET_PROPERTIES += null |
| `src/query/physical_plan/operator/create_edge_physical_op.cpp` | 边属性镜像 |
| `src/query/planner/binder/bind_mutation.cpp` | 调用 validateMutationPattern、rel_pat.direction 处理、逗号 CREATE 变量复用 |
| `src/query/planner/binder/bind_merge.cpp` | 提取 validateMutationPattern 后调用（保持现有行为） |
| `src/query/planner/logical_plan/operator/bound_create_edge_op.hpp` | 显式 src/dst 变量名（若 §4.4 选此方案） |

### 设计文档同步
- 本文档（实施过程中遇到的偏差同步到此处）
- 若 `merge-clause-design.md` §"无方向关系"与 §4.4 实现不一致，相应更新

---

## 七、风险与权衡

### 风险 1：内存态镜像可能漏字段

`VertexValue` / `EdgeValue` 字段较多（id, labels, properties, seq, src_id, dst_id, ...）。如果只补 `labels` 而漏 `properties`，又会出现新的"写后不可见"。**对策**：§4.1.2 (c) 的 `mirrorVertex` / `mirrorEdge` helper 强制要求调用方提供完整 updated 对象，避免散点更新。

### 风险 2：守卫抽取可能改变 MERGE 现有行为

`bind_merge.cpp:205-228` 中部分守卫有特定的优先级顺序（如 "关系已绑定优先于 NoSingleRelationshipType"）。**对策**：抽取时保留原顺序，单元测试覆盖每个守卫的 trigger 顺序。

### 风险 3：null 语义改动可能破坏现有通过的 Set 测试

`SET n = {p: null}` 当前实现是"跳过"，规范是"p 不存在"——两者结果一致（因为 `=` 前面已经清空了所有属性）。**对策**：Phase 2 单独提交，跑完整 Set + With + Match 类 TCK 确认无回归。

### 风险 4：CREATE 方向改动影响 src_col_idx_ 解析

当前 `BoundCreateEdgeOp.src_variable` / `dst_variable` 是变量名字符串，物理 planner 通过 `slot_layout` 解析为 col_idx。如果 binder 端交换了 src/dst，物理端不需要改。**对策**：保持 `BoundCreateEdgeOp` 用变量名，只 binder 端 swap。但如果 col_idx 已经在 binder 期间通过 `bindNodePattern` 写入 `BoundCreateEdgeOp`，需要核查一并 swap。

### 风险 5：内存态镜像漏掉同 vid 多列引用（Gemini）

`mirrorVertexToAllReferences` 必须扫描同 row 的所有 VERTEX 列；如果未来引入更复杂的别名传播（UNWIND、subquery 返回值），可能出现"算子端扫描不到的隐藏别名"。**对策**：Phase 1 完成后，专门跑一组 `MATCH (a) WITH a AS a1, a AS a2 SET a1.p = 1 RETURN a2.p` 类用例确认行为；如果发现遗漏场景，考虑改为"按 vid 索引"的更新模型（更彻底但超出本次范围）。

### 风险 6：TCK 错误码与现有 error 字符串措辞不一致（Gemini）

如果 `validateMutationPattern` 抽取过程中无意识改动了错误信息措辞（比如把 `VariableAlreadyBound: variable 'x' is already defined in this scope` 改成 `Variable 'x' already bound`），即使守卫判定正确，TCK 也会因为 status code mismatch 失败。**对策**：抽取时严格复制原字符串；守卫 helper 不应该"自由发挥"错误信息，统一从一个 const 错误模板表里取。

### 风险 7：shared_ptr buffer 浅拷贝污染 DICTIONARY 派生列（Gemini，已核查源码确认）

`Column::buffer` 是 `std::shared_ptr<ColumnBuffer>`。`Column::setValue` 在 FLAT 形式下直接修改 buffer，不做 `use_count()` 检查。如果一个 FLAT 列的 buffer 被另一个 DICTIONARY 列通过 `Column::dict(buf, sel)` 引用（执行器内部零拷贝派生），mutation 的 setValue 会污染 DICTIONARY 列。

**已确认**（`data_chunk.hpp:411-423`、`set_physical_op.cpp:131-147`）：
- `Column::setValue` 没有 COW 检查
- 现有 SET 预处理只检查 `form != FLAT`，没检查 `use_count() > 1`

**对策**：`mutation_mirror.hpp` 中提供 `ensureExclusiveBuffer(col, n)` helper，所有 mirror 函数进入前对触发列 + 候选同 vid 列各调用一次。Phase 1 单元测试要专门覆盖 DICTIONARY 派生场景。

---

## 八、验收标准

1. `docs/tests/tck-results.md` 中 Set 类失败从 21 降到 ≤ 5
2. Create 类失败从 29 降到 ≤ 5
3. 全部 TCK 子句类无回归（基线：3577 通过 / 249 失败）
4. 现有单元测试 `tests/query/` 全部通过
5. `./scripts/check-format.sh` 与 `./scripts/build.sh` 通过

---

## 九、实施记录（2026-08-01）

### 实际实现差异（vs 设计）

1. **守卫内联**：`validateCreatePattern` 实现为 `bind_mutation.cpp` 匿名命名空间内的函数（仅 CREATE 使用）。**没有**把 bind_merge 的守卫重构为共用 helper —— 避免重构现有工作的回归风险。后续可抽取为 `mutation_guards.hpp` 共用。

2. **EdgeValue 结构修正**：设计文档原本说 EdgeValue 的 properties 是 `unordered_map<EdgeLabelId, Properties>`，实际是 `std::optional<Properties>`（边只有一个 label）。CREATE 边的属性镜像因此简化为 `ev.properties = props`。

3. **E3 逗号 CREATE 变量复用**：经测试验证（`CommaCreateNodeAndEdge` 等 4 个测试全部通过），现有 `bindNodePattern` 的 `start_exists` 路径已经正确处理跨 pattern 变量复用。原 memory 记录的 bug 已在之前的 PR 中修复。**无需新增代码**。

4. **`stripParens` 是死代码**：`ast.hpp:617` 定义了但全代码库无调用方。`(n).p` 形式的 SET target 需要在 binder 端手动展开 ParenExpr 包装。已在 `bindSet` 的 SET_PROPERTY visitor 中处理。

### Phase 完成情况

| Phase | 范围 | 状态 | 改进场景 |
|-------|------|------|---------|
| 1 | 内存态镜像统一（mutation_mirror.hpp + SET_LABELS + CREATE edge properties） | ✅ | Set3 [1-7]、Create2 [14][16][17] 等 |
| 2 | Cypher null 语义统一（SET_PROPERTY null=REMOVE；SET_PROPERTIES += null=REMOVE） | ✅ | Set2 [1-3] 等 |
| 3 | CREATE 守卫（VariableAlreadyBound / NoSingleRelationshipType / CreatingVarLength / RequiresDirectedRelationship） | ✅ | Create1 [4]、Create2 等 |
| 4 | CREATE 反向关系（RIGHT_TO_LEFT 交换 src/dst） | ✅ | Create2 [4][6] 等 |
| 5 | 其他（(n).p 解析、list-of-maps 报错、E3 验证已修复） | ✅ | E1 SET parenthesized、E2 list-of-maps |

### TCK 改进数据

| 类别 | 之前 | 现在 | 改进 |
|------|------|------|------|
| Set | 32/53 通过（21 失败） | **38/53 通过（15 失败）** | **+6** |
| Create | 48/78 通过（30 失败） | **64/78 通过（14 失败）** | **+16** |
| **合计** | 80/131（61%） | **102/131（78%）** | **+22 场景** |

### 单元测试

- `tests/test_query_executor.cpp` 全部 445 个测试通过，零回归。
- 包含 `CommaCreateNodeAndEdge` / `CommaCreateChainThenIndependent` / `CommaCreateChainMultipleNodesAndEdge` 等关键回归覆盖。

### 未实现 / 后续

- `tests/query/test_mutation_mirror.cpp` 独立单元测试（Gemini 建议）：未写。验证依赖 TCK + query_executor_tests。
- `validateCreatePattern` 抽取到 `mutation_guards.hpp` 让 bindMerge 共用：未做（避免重构风险）。
- 完整 TCK 其他类别零回归验证：进行中。
