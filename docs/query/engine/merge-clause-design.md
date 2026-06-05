# MERGE Clause Design

## 语义概述

MERGE 是 Cypher 的 UPSERT 操作：**匹配已有数据或创建新数据**。三种形式：

| 形式 | 示例 | 行为 |
|------|------|------|
| 节点 MERGE | `MERGE (n:L {prop: val})` | 按 label + properties 匹配节点，无则创建 |
| 关系 MERGE | `MATCH (a),(b) MERGE (a)-[:R]->(b)` | 端点必须已绑定，按端点+类型+属性匹配边，无则创建 |
| 路径绑定 | `MERGE p = (a)-[:R]->(b)` | 匹配或创建完整 pattern，路径变量绑定到 `p` |

### ON CREATE / ON MATCH

```
MERGE (a:L {prop: val})
  ON CREATE SET a.created = timestamp()
  ON MATCH  SET a.seen = a.seen + 1
```

- **ON CREATE**：仅当元素被创建时执行（首次运行）
- **ON MATCH**：仅当匹配到已有元素时执行（后续运行）

### 幂等性（Read-Own-Writes）

同一查询内，后续行能看到前面行创建的数据：

```cypher
UNWIND [1, 1, 2] AS x
MERGE (n:Num {val: x})
```
三行输入只创建 **2** 个节点（val=1 和 val=2），第二行匹配第一行创建的节点。

---

## 设计决策

**新增 `BoundMergeOp` + `MergePhysicalOp`**，不拆解为现有算子的组合。

原因：MERGE 的"匹配或创建"决策是原子化的，且行间可见性（read-own-writes）要求算子内部持有已创建实体的状态。拆解为 MATCH + LeftJoin + Create + Set 的组合无法在流水线中跨批次共享此状态。

---

## 语法与 AST

ANTLR 语法规则（`grammar/CypherParser.g4`）：

```antlr
mergeSt      : MERGE patternPart mergeAction*
mergeAction  : ON (MATCH | CREATE) setSt
```

AST 节点（已实现，`src/query/parser/ast.hpp`）：

```cpp
struct MergeClause {
    PatternPart pattern;
    std::vector<SetItem> on_create;  // ON CREATE SET 项
    std::vector<SetItem> on_match;   // ON MATCH SET 项
};
```

Parser 已完整（`src/query/parser/cypher_parser.cpp:buildMergeClause()`）。实现工作集中在 Binder 和 Physical Operator。

---

## 日志算子（BoundMergeOp）

新增文件：`src/query/planner/logical_plan/operator/bound_merge_op.hpp`

```cpp
struct BoundMergeOp {
    // 路径绑定
    std::optional<std::string> path_variable;

    // 起始节点
    std::string start_var;
    std::vector<LabelId> start_labels;
    std::vector<std::pair<uint16_t, BoundExpression>> start_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> start_pending_props;

    // 关系（可选）
    bool has_relationship = false;
    std::string edge_var;
    std::optional<EdgeLabelId> edge_label_id;
    cypher::RelationshipDirection direction;
    std::vector<std::pair<uint16_t, BoundExpression>> edge_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> edge_pending_props;

    // 目标节点（如果有关系）
    std::string end_var;
    std::vector<LabelId> end_labels;
    std::vector<std::pair<uint16_t, BoundExpression>> end_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> end_pending_props;

    // ON CREATE / ON MATCH SET 项
    std::vector<BoundSetOp::SetItem> on_create_items;
    std::vector<BoundSetOp::SetItem> on_match_items;

    // 子算子（输入数据来源）
    BoundLogicalOperator child;
};
```

---

## Binder（bindMerge）

新增文件：`src/query/planner/binder/bind_merge.cpp`

### 错误检查（绑定阶段）

| 错误 | 触发条件 | TCK 场景 |
|------|---------|---------|
| `VariableAlreadyBound` | MERGE pattern 中的变量在进入 MERGE 前已存在 | Merge1[15], Merge5[22][26] |
| `NoSingleRelationshipType` | 关系类型数 != 1 | Merge5[23][24][25] |
| `CreatingVarLength` | 关系 pattern 包含 `*min..max` | Merge5[28] |
| `UndefinedVariable` | ON CREATE/MATCH SET 引用未定义变量 | Merge2[6], Merge3[5] |
| `InvalidParameterUse` | 参数作为节点/关系断言 | Merge1[16], Merge5[27] |
| `MergeReadOwnWrites` | 属性值为 null | Merge1[17], Merge5[29]（运行时） |

### 绑定流程

1. 从 `MergeClause` 提取单 `PatternPart`
2. 绑定起始节点：复用 `bindNodePattern()` 逻辑，注册变量到 `ctx_.symbols`
3. 若有关系链：绑定关系（要求单类型）、绑定目标节点
4. 绑定属性表达式：字面量解析为 `(prop_id, expr)`，动态表达式保留为 pending
5. 绑定 ON CREATE/MATCH SET items：复用 `bindSet` 逻辑，验证变量引用
6. 构造 `BoundMergeOp`，chain 到 child，注册所有新变量到 `ctx_.symbols`

---

## 物理算子（MergePhysicalOp）

新增文件：`src/query/physical_plan/operator/merge_physical_op.{hpp,cpp}`

### 执行算法

```
for each DataChunk from child:
    output = new DataChunk(child_columns + new_columns)
    for each row in chunk:
        if has_relationship:
            src_id = extract from row (child column or created_vertices)
            dst_id = extract from row (child column or created_vertices)
            // 扫描已有边（无方向模式时双向扫描）
            edges = scanEdges(src_id, direction, edge_label_id)
                   + (direction==BOTH ? scanEdges(dst_id, reverse) : {})
            filter edges by properties + created_edges set
            if found:
                append row + existing edge to output
                execute ON MATCH SET items
            else:
                create edge + optionally create end node
                add to created_edges / created_vertices sets
                execute ON CREATE SET items
                append row + created edge to output
        else: // 节点 MERGE
            vertex = scan by label + properties + created_vertices set
            if found:
                append row + existing vertex to output
                execute ON MATCH SET items
            else:
                vertex = insertVertex(label(s), properties)
                add to created_vertices
                execute ON CREATE SET items
                append row + vertex to output
    yield output chunk
```

### 无方向关系（`-[r:TYPE]-`）

当关系模式不带箭头时，`direction` 设置为 `BOTH`：

- **匹配阶段**：双向扫描 `scanEdges(src, LEFT) ∪ scanEdges(src, RIGHT)`
- **创建阶段**：始终从左到右 `insertEdge(src → dst)`

对应 TCK 场景：Merge5 [11][12][13]。

### Read-Own-Writes

- `created_vertices`: `absl::flat_hash_set<VertexId>` — 算子实例内跨批次共享
- `created_edges`: `(src_id, dst_id, edge_label_id, props_hash)` 集合
- 扫描存储前先查内存集合
- 创建后立即加入集合

### SET 执行

复用 `SetPhysicalOp` 的 SetItem 执行逻辑，支持：
- `SET x.prop = val` — 属性设置
- `SET x:L` — 标签设置
- `SET x = node` — 属性拷贝（SET_PROPERTIES）
- `SET x += {k: v}` — map 合并（is_add_assign）
- `SET x.prop = null` — 属性移除

---

## 输出 Schema

| Pattern | 新增列 |
|---------|--------|
| `MERGE (n:L {p:v})` | `n` (VERTEX) |
| `MERGE (a)-[:R]->(b)` | `a` (VERTEX, 如非预绑定), `b` (VERTEX), `r` (EDGE) |
| `MERGE p = (a)-[:R]->(b)` | 同上 + `p` (PATH) |

预绑定变量（从前置子句传入）不在输出中重复列。

---

## 需要修改的文件

### 新增

| 文件 | 说明 |
|------|------|
| `src/query/planner/logical_plan/operator/bound_merge_op.hpp` | BoundMergeOp 结构体定义 |
| `src/query/planner/binder/bind_merge.cpp` | `Binder::bindMerge()` 实现 |
| `src/query/physical_plan/operator/merge_physical_op.hpp` | MergePhysicalOp 类声明 |
| `src/query/physical_plan/operator/merge_physical_op.cpp` | executeChunk 主逻辑 |

### 修改

| 文件 | 修改 |
|------|------|
| `src/query/planner/bound_logical_plan_fwd.hpp` | 前向声明 + variant 新增 `unique_ptr<BoundMergeOp>` |
| `src/query/planner/bound_logical_plan.hpp` | include bound_merge_op.hpp |
| `src/query/planner/binder.hpp` | 声明 `bindMerge()` 方法 |
| `src/query/planner/binder/binder.cpp` | `bindSingleQuery()` 新增 `MergeClause` 分发 |
| `src/query/planner/column_resolver.cpp` | 新增 `BoundMergeOp` 表达式解析分支 |
| `src/query/optimizer/opt_rule.hpp` | `OptNodeType::Merge = 24` |
| `src/query/optimizer/opt_rule.cpp` | `nodeTypeFromVariantIndex` 新增 Merge 映射 |
| `src/query/physical_plan/physical_planner.cpp` | 新增 `BoundMergeOp` → `MergePhysicalOp` |

---

## 已知限制

- ✅ **端点自创建**：`MERGE (a:L1 {p:1})-[:R]->(b:L2 {p:2})` 在单次 MERGE 中创建整个模式（端点+关系）。已实现，自定义测试覆盖。
- ✅ **多标签节点 MERGE**：`MERGE (n:L1:L2 {p:v})` 支持多标签匹配和创建。当前使用朴素 label scan + 属性过滤方式，性能优化（如复合标签索引扫描）留待后续。
- ✅ **边扫描延迟解析**：`CreateEdgePhysicalOp` 延迟到子算子执行后再解析标签 ID，解决子算子（如 `CreateEdgeLabelPhysicalOp`）共享映射尚未填充的问题。
- ✅ **LIMIT 0 副作用**：`LimitPhysicalOp` LIMIT 0 正确消费子数据触发副作用，仅结果集为空。
- **边扫描**：分支上 `MATCH ... CREATE` 边后，`ExpandPhysicalOp` 通过 `scanEdges` 扫描不到刚创建的边（`insertEdge` 未被调用，原因待查）。这是已有问题，非 MERGE 引入，但影响边 MERGE 的匹配阶段。
- **ON MATCH SET 边属性不可见**：ON MATCH 分支中通过 `putEdgeProperty` 写入的边属性在后续 snapshot 查询中不可见（Bug J）。ON CREATE 分支正常。影响 7 个 TCK 场景。
- **动态属性访问 `r[key]`**：edge 类型的动态属性访问未完整支持（Bug K）。影响 4 个 TCK 场景的控制查询。
- **findMatchingEdge 只返回首条**：扫描边时不验证所有属性约束即返回（Bug L）。影响 1 个 TCK 场景。
- **findMatchingNode 匹配不完整**：扫描标签表不过滤已删除节点（Bug M）。影响 3 个 TCK 场景。

## 当前 TCK 状态

**55 / 75 MERGE 场景通过**（73%），20 个失败场景详见 `docs/tests/tck-results.md` MERGE TCK 专项。
