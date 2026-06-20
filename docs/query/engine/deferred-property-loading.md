# 延迟属性物化：独立的属性加载算子

> 参见 [README.md](../../README.md) 返回文档导航

---

## 一、问题与背景

当前 Scan（LabelScan/AllNodeScan/IndexScan）和 Expand 算子在 `executeChunk()` 中内联加载顶点/边属性以及顶点 labels。这导致：

1. **算子职责不清晰**：扫描/展开算子同时承担了数据定位、labels 加载、属性加载三件事
2. **性能浪费**：labels 和属性对所有行加载，即使下游 Filter 淘汰大部分行，或者查询根本不需要（如 `count(*)`）
3. **AllNodeScan 尤其浪费**：无条件加载**全部标签的全部属性**
4. **Expand 忽略 `edge_prop_ids_`**：代码实际加载全部边属性，过滤参数未生效

## 二、目标（✅ 已实现）

将 labels 和属性加载从数据源算子中剥离为独立算子：

```
当前:  Scan → [Filter] → Project
         (加载 labels + 全部属性)

目标:  Scan → VertexLabelRead → VertexPropertyRead → Filter → Project
         (拓扑id) (按需加载)    (按需加载)      (只对幸存行)
```

Scan/Expand 只产出拓扑类型（VertexRef / EdgeKey / PathTopology），需要什么再挂什么算子。

> **实施状态（2026-06）**：分支 `feature/topology-semantic-split` 已完整实现。Scan/Expand 输出拓扑类型，wrap 管线（`wrapVertexLabelRead` / `wrapVertexPropertyRead` / `wrapEdgePropertyRead` / `wrapPathElementPropertyRead`）按需加载语义数据。

## 三、方案概要（已实施）

- Scan 输出 `VertexRef{id}` — VERTEX_REF 类型，仅含 id
- Expand 输出 `EdgeKey{id, src_id, dst_id, label_id, seq}` — 仅结构字段
- `VertexLabelReadPhysicalOp` — 读 labels，将 VertexRef 原地升级为 VertexValue
- `VertexPropertyReadPhysicalOp` — 读属性，将 VertexValue 的 properties 填充
- `EdgePropertyReadPhysicalOp` — 读边属性，将 EdgeKey 原地升级为 EdgeValue
- `PathElementPropertyReadPhysicalOp` — 读路径元素属性和标签，将 PathTopology 升级为 PathValue
- 所有 wrap 算子通过 **Column Replacement** 替换对应列，其他列 DICTIONARY 零拷贝透传
- **不改 evaluator、不改 BoundPropertyRef、不改列索引**

## 四、最终架构（已实施）
| VarLenExpand edge | — | 不加载 | 输出仅结构字段 |
| VarLenExpand path | — | PathElementPropertyRead | 路径元素属性按需加载（当前全量） |

## 五、新增物理算子

### VertexLabelReadPhysicalOp

`src/query/physical_plan/operator/vertex_label_read_physical_op.hpp/.cpp`

```
输入:  DataChunk, col_idx 指向 VERTEX 列 (VertexValue{id})
参数:  anon_label_id
行为:  收集有效行 vid → co_await store_.getVertexLabels(vid) → 移除 anon_label_id → vv.labels
输出:  同 schema，col_idx 被替换为 VertexValue{id, labels}
```

### VertexPropertyReadPhysicalOp

`src/query/physical_plan/operator/vertex_property_read_physical_op.hpp/.cpp`

```
输入:  DataChunk, col_idx 指向 VERTEX 列 (VertexValue{id, labels?})
参数:  label_prop_ids: map<LabelId, vector<uint16_t>>
行为:  对每行，对每个 (lid, prop_ids)，co_await store_.getVertexProperties(vv.id, lid, prop_ids)
      结果写入 vv.properties[lid]
输出:  同 schema，col_idx 被替换为 VertexValue{id, labels?, properties={loaded}}
```
> 不依赖 VertexValue.labels。按规划期确定的 label_prop_ids 直接调存储。

### EdgePropertyReadPhysicalOp

`src/query/physical_plan/operator/edge_property_read_physical_op.hpp/.cpp`

```
输入:  DataChunk, col_idx 指向 EDGE 列 (EdgeValue{id, src, dst, label, properties=nullopt})
参数:  edge_prop_ids: vector<uint16_t>
行为:  对每行，co_await store_.getEdgeProperties(ev.label_id, ev.id, edge_prop_ids) → ev.properties
输出:  同 schema，col_idx 被替换为 EdgeValue{..., properties=loaded}
```

### PathElementPropertyReadPhysicalOp

`src/query/physical_plan/operator/path_element_property_read_physical_op.hpp/.cpp`

```
输入:  DataChunk, col_idx 指向 PATH 列 (PathValue)
参数:  path_variable
行为:  遍历每行 PathValue 的 elements：
       - VertexValue: 若 properties 为空，getVertexLabels(id) → 对每个 label getVertexProperties(id, lid)
       - EdgeValue:   若 properties 为空，getEdgeProperties(label_id, id)
输出:  同 schema，path 列被替换为填充属性后的 PathValue
```

> **当前限制（待优化）**: 此算子为路径内所有顶点/边加载**全部属性**。原因是路径元素类型在 binder 阶段为 ANY，`x.name` 绑定为 `BoundDynamicPropertyRef`（运行时动态查找），planner 无法在编译期确定需要哪些属性。
>
> **后续优化方向 A（按需加载）**: 增强 planner，从 quantifier WHERE 子句等表达式树中提取 `BoundDynamicPropertyRef` 的 property name 集合，传给此算子按需加载。需在 planner 阶段分析路径元素的属性访问。
>
> **后续优化方向 B（消除此算子）**: 更彻底的方案是**不引入 `PathElementPropertyReadPhysicalOp`**，而是在 binder 阶段分析 `nodes(p)` / `relationships(p)` 被哪些表达式消费（如 `all(x IN nodes(p) WHERE x.name = 'a')`），推导出路径内顶点/边需要哪些属性。然后复用已有的 `VertexPropertyReadPhysicalOp` / `EdgePropertyReadPhysicalOp`，让它们能作用于 PathValue 内的元素（遍历 elements 填充对应属性）。
>
> 方向 B 的优势：不增加新算子，属性加载逻辑收敛到已有算子；劣势：需让现有 PropertyRead 算子支持「路径内元素」作为操作目标（而非独立列），并需 binder 在路径元素类型为 ANY 时仍能从使用点反推属性需求。当前先用独立算子（方向 A 的前置）快速解决 TCK，方向 B 作为后续重构目标。

## 六、PhysicalPlanner 集成

新增四个 wrap 函数（`physical_planner.cpp`）：

```cpp
wrapVertexLabelRead(child_result, variable, anon_label_id, store)
wrapVertexPropertyRead(child_result, variable, label_prop_ids, store)
wrapEdgePropertyRead(child_result, variable, edge_prop_ids, store)
wrapPathElementPropertyRead(child_result, path_variable, store)
```

| 逻辑算子 | 包裹算子（按顺序） |
|---------|------|
| BoundScanOp / BoundLabelScanOp | VertexLabelRead → VertexPropertyRead |
| BoundExpandOp | EdgePropertyRead → VertexLabelRead → VertexPropertyRead |
| BoundVarLenExpandOp | VertexPropertyRead（dst）→ PathElementPropertyRead（path，若存在） |
| tryBoundIndexScan / tryBoundEdgeIndexScan | VertexLabelRead → VertexPropertyRead |

## 七、关键实现细节

### 7.1 DICTIONARY selection vector 保留

Read 算子对 DICTIONARY 列做 passthrough 时必须**保留原始 dict_sel**，不能用 identity selection 替换。原始 buffer 的行数可能远小于逻辑行数（如 CorrelatedSource buffer 只有 1 行，但 Expand 输出 N 行）。FLAT 列则使用 identity selection。

### 7.2 自关系变量搜索

`(n)-[r]->(n)` 中变量 `n` 在 schema 中出现两次。wrap 函数从后往前搜索，匹配 Expand 产出的新列（末尾出现），而非 Scan 的输入列。

### 7.3 keys(n) 全属性需求

`keys(n)` 和 `properties(n)` 在 binder 中触发所有标签的全属性需求，确保 VertexPropertyRead 加载全部属性。TCK 快照验证依赖此行为。

### 7.4 VarLenExpand 边属性过滤保留

VarLenExpand 的 `edge_prop_filters_`（DFS 逐跳过滤）继续使用 `getEdgeProperties()`。输出 EdgeValue 已剥离属性。

### 7.5 VarLenExpand 路径元素 labels 加载

VarLenExpand 构建 PathValue 时为所有内部顶点（含中间节点和终点）调用 `getVertexLabels()` 填充 `VertexValue.labels`。这是 `nodes(p)` / `relationships(p)` 能在结果中正确显示标签的前提。注意：这会增加每跳 1 次 KV 读，后续若路径元素属性按需加载（见 PathElementPropertyRead 的优化方向），可考虑合并 labels 与属性读取。

### 7.6 异构列表的全属性预加载

当 VERTEX 和 EDGE 类型的列被放入同一个列表（如 `[n, r]`），binder 的 `BoundType::merge` 会将结果类型合并为 ANY。此时列表元素绑定为 `BoundDynamicPropertyRef`（运行时动态解析属性名），binder 无法在编译期确定需要哪些具体属性。为此，在 `bindListExpr` 中检测到合并类型为 ANY 时，对列表中每个 VERTEX/EDGE 元素预加载其所有标签的全部属性，确保运行时属性访问能正确命中。

## 八、验证

- **单元测试**: 384/384 pass
- **ASAN**: 384/384 pass，零内存错误
- **TCK**: server 零崩溃，零 ASAN 报错
- **CI**: 与 main 分支相比，side effects 和自关系场景无回归
