# 延迟属性物化：独立的属性加载算子

> 参见 [README.md](../../README.md) 返回文档导航

---

## 一、问题与背景

当前 Scan（LabelScan/AllNodeScan/IndexScan）和 Expand 算子在 `executeChunk()` 中内联加载顶点/边属性以及顶点 labels。这导致：

1. **算子职责不清晰**：扫描/展开算子同时承担了数据定位、labels 加载、属性加载三件事
2. **性能浪费**：labels 和属性对所有行加载，即使下游 Filter 淘汰大部分行，或者查询根本不需要（如 `count(*)`）
3. **AllNodeScan 尤其浪费**：无条件加载**全部标签的全部属性**
4. **Expand 忽略 `edge_prop_ids_`**：代码实际加载全部边属性，过滤参数未生效

## 二、目标

将 labels 和属性加载从数据源算子中剥离为独立算子：

```
当前:  Scan → [Filter] → Project
         (加载 labels + 全部属性)

目标:  Scan → VertexLabelRead → VertexPropertyRead → Filter → Project
         (仅id)  (按需加载)     (按需加载)      (只对幸存行)
```

Scan/Expand 只产出最简身份标识，需要什么再挂什么算子。

## 三、方案概要

- Scan 输出 `VertexValue{id}` — VERTEX 类型，仅含 id
- Expand 输出 `EdgeValue{id, src_id, dst_id, label_id}` — 仅结构字段，无 properties
- `VertexLabelReadOp` — 读 labels，替换列为 `VertexValue{id, labels}`
- `VertexPropertyReadOp` — 读属性，替换列为 `VertexValue{id, labels?, properties}`
- `EdgePropertyReadOp` — 读边属性，替换列为 `EdgeValue{..., properties}`
- 所有新算子通过 **Column Replacement** 替换对应列，其他列 DICTIONARY 零拷贝透传
- **不改 evaluator、不改 BoundPropertyRef、不改列索引**

## 四、最终架构

| 算子 | labels | 属性 | 加载方式 |
|------|:---:|:---:|------|
| LabelScan（单标签） | VertexLabelRead（零 IO） | VertexPropertyRead | 编译时已知 labels |
| LabelScan（多标签） | VertexLabelRead（1 KV） | VertexPropertyRead | 过滤需要 labels |
| AllNodeScan | VertexLabelRead | VertexPropertyRead | 完全剥离 |
| IndexScan | VertexLabelRead | VertexPropertyRead | 完全剥离 |
| Expand dst | VertexLabelRead | VertexPropertyRead | 完全剥离 |
| Expand edge | — | EdgePropertyRead | 按需加载 |
| VarLenExpand dst | 内部加载（1 KV） | VertexPropertyRead | DFS 需要 labels |
| VarLenExpand edge | — | 不加载 | 输出仅结构字段 |

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

## 六、PhysicalPlanner 集成

新增三个 wrap 函数（`physical_planner.cpp`）：

```cpp
wrapVertexLabelRead(child_result, variable, anon_label_id, store)
wrapVertexPropertyRead(child_result, variable, label_prop_ids, store)
wrapEdgePropertyRead(child_result, variable, edge_prop_ids, store)
```

| 逻辑算子 | 包裹算子（按顺序） |
|---------|------|
| BoundScanOp / BoundLabelScanOp | VertexLabelRead → VertexPropertyRead |
| BoundExpandOp | EdgePropertyRead → VertexLabelRead → VertexPropertyRead |
| BoundVarLenExpandOp | VertexPropertyRead（仅 dst；labels 内部加载） |
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

## 八、验证

- **单元测试**: 384/384 pass
- **ASAN**: 384/384 pass，零内存错误
- **TCK**: server 零崩溃，零 ASAN 报错
- **CI**: 与 main 分支相比，side effects 和自关系场景无回归
