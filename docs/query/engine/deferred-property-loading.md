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
旧:   Scan → [Filter] → Project
        (加载 labels + 全部属性)

目标:  Scan → ProjectionExtract → Filter → Project
        (拓扑id) (按需加载 labels/属性)  (只对幸存行)
```

Scan/Expand 只产出拓扑类型（VertexRef / EdgeKey / PathTopology），需要什么再挂什么算子。

> **实施状态（2026-06）**：
> - `feature/topology-semantic-split`：拓扑类型输出 + 旧 wrap 管线 ✅
> - `feature/lightweight-extract`：删除 `VertexLabelReadPhysicalOp`、`VertexPropertyReadPhysicalOp`，统一为 `VertexPropertyExtractPhysicalOp`（三种请求类型），standalone 模式暂未启用
> - `feature/projection-extract-op`：**新增 ProjectionExtractPhysicalOp**，融合点/边属性抽取 + Project 语义。5 处 inline loader（loadVertexLabelsInPlace / loadVertexPropertiesInPlace）替换为 dispatchProjectionExtract。通过 PlanRequirements 按需收集（triple-guard: whole_vertex / whole_edge / 细粒度 property），column_rewrite 将 BoundPropertyRef 重写为 BoundColumnRef。**旧 inline loader 及 dispatch 函数已清理**。`wrapPathElementPropertyRead` 保留用于 PathBuild 路径（PathTopology→PathValue 全量加载，待后续按需优化）。
>
> **拓扑/语义类型对齐（2026-06，feature/projection-extract-op 收尾）**：ProjectionExtract 落地后暴露出三类「binder 类型为语义形式（VERTEX），运行时为拓扑形式（VertexRef）」的边界场景，全部修复后 394/394 单测通过：
> - **CorrelatedSource**：binder 把 correlated 变量类型标为 `VERTEX`，但 LeftJoin 实际传入的是上游扫描产出的 `VertexRef`，`Column::setValue(VERTEX, VertexRef)` 静默丢弃 → OPTIONAL MATCH 全表 0 命中。修复：`CorrelatedSourcePhysicalOp::executeChunk` 改为按运行时值推导列 kind。
> - **Project 的 BoundColumnRef 直通**：`WITH b` / `RETURN b` 中 item 是 `BoundColumnRef`，binder 类型为 `VERTEX`，Project 据此建 VERTEX 列，写入 VertexRef 时再次被丢弃。修复：Project 对 `BoundColumnRef` item 改用源列的运行时 type，而非 binder 类型。
> - **column_rewrite 漏改直通引用**：`BoundPropertyRef` 已由 `rewriteExpr` 改写 `column_index`，但**直接变量引用**（item.expr 本身就是 `BoundColumnRef`）之前未处理；当上游 ProjectionExtract 追加属性列后，源列下标发生偏移，引用指向错误列。修复：`rewriteExpr` 新增 `BoundColumnRef` 分支，按 `PropertyExtractionInfo::base_col` 校正 `column_index`；同时让 `buildExtractionInfo` 为 `need_whole_vertex/edge` 变量也建立条目（之前因「同名列就地覆盖」假设被跳过，但该假设仅在同 index 时成立，前置变量追加属性列即被破坏）。
> - **base_col 统计补全**：`updateBaseCols` 之前未处理 `BoundCorrelatedSourceOp`（右子树漏算 correlated 列）和 `BoundVarLenExpandOp`（漏算 dst/path/edge 输出列），导致下游变量 base_col 偏小，column_rewrite 错误校正到 col 0。修复：补齐两类算子的列数累加。
> - **属性列去重**：ProjectionExtract 在管线中可能多次执行（Filter 内联属性抽取 + 后续算子再次抽取），第二次会按 reqs 重复 emit `var.prop` 列，挤掉下游列。修复：`dispatchProjectionExtract` 构建 `existing_cols` 集合，已存在的 `var.prop_name` 跳过。

## 三、方案概要

- Scan 输出 `VertexRef{id}` — VERTEX_REF 类型，仅含 id
- Expand 输出 `EdgeKey{id, src_id, dst_id, label_id, seq}` — 仅结构字段
- `ProjectionExtractPhysicalOp` — 融合点/边属性抽取 + Project 语义的统一算子（7 种 ColumnSpec）
- `PathElementPropertyReadPhysicalOp` — 仅用于 PathBuild 路径，读路径元素属性和标签，将 PathTopology 升级为 PathValue
- 通过 **Column::dict** / `std::move(chunk->columns)` 零拷贝直通
- **不改 evaluator、不改 BoundPropertyRef、不改列索引**（`column_rewrite` 仅校正 `column_index` 数值）

> 历史路径：曾设计 `VertexLabelRead` / `VertexPropertyRead` / `EdgePropertyRead` 三个独立算子（见下一节"五、新增物理算子"末尾的历史记录），后统一收敛为 `ProjectionExtractPhysicalOp`。

## 四、最终架构（已实施）

| 数据源 | 抽取方式 |
|---------|------|
| Scan / LabelScan / IndexScan | `dispatchProjectionExtract` |
| Expand | `dispatchProjectionExtract` |
| VarLenExpand dst | `dispatchProjectionExtract` |
| VarLenExpand edge | 不加载（边属性过滤仍走 DFS 内部 `getEdgeProperties()`） |
| VarLenExpand path | `wrapPathElementPropertyRead`（当前全量加载，待按需优化） |
| PathBuild | `wrapPathElementPropertyRead` |

## 五、新增物理算子

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
> **后续优化方向 B（消除此算子）**: 更彻底的方案是**不引入 `PathElementPropertyReadPhysicalOp`**，而是在 binder 阶段分析 `nodes(p)` / `relationships(p)` 被哪些表达式消费（如 `all(x IN nodes(p) WHERE x.name = 'a')`），推导出路径内顶点/边需要哪些属性。然后复用 `ProjectionExtractPhysicalOp` 的列加载能力，作用于 PathValue 内的元素。
>
> 方向 B 的优势：属性加载逻辑收敛到单一算子；劣势：需让 ProjectionExtract 支持「路径内元素」作为操作目标（而非独立列），并需 binder 在路径元素类型为 ANY 时仍能从使用点反推属性需求。当前先用独立算子（方向 A 的前置）快速解决 TCK，方向 B 作为后续重构目标。

### ProjectionExtractPhysicalOp（融合方案）

`src/query/physical_plan/operator/projection_extract_physical_op.hpp/.cpp`

**设计理念**：将点/边属性抽取融合为统一算子，输出 schema 精确匹配下游需求。

**ColumnSpec（7 种列来源）**：
| Kind | 行为 |
|------|------|
| `Passthrough` | 从上游列直接透传 |
| `LoadVertexProp` | 按 (label_id, prop_id) 加载单个点属性 |
| `LoadEdgeProp` | 按 (edge_label_id, prop_id) 加载单个边属性 |
| `LoadVertexLabels` | 加载 labels 为 `List<String>` |
| `LoadEdgeType` | 加载 edge type 为 `String` |
| `ConstructVertex` | 加载完整 VertexValue（labels + 全部属性） |
| `ConstructEdge` | 加载完整 EdgeValue（全部属性） |

**核心原则**：
- **覆盖原则**：need_whole_vertex 覆盖 need_props（同一变量不重复加载）
- **Per-row 缓存**：行内多个 spec 引用同一 source_col 时共享 vid/eid（避免重复解析 Value 变体）
- **列顺序**：Passthrough → Vertex props → Edge props → Vertex labels → Edge type（确保 column_rewrite 偏移计算正确）

**PlanRequirements 收集**（`collectPlanRequirements`）：从 BoundLogicalOperator 树根遍历，收集每个变量的需求。关键逻辑：
- ProjectOp / AggregateOp 内（`in_schema_changing_op=true`）：BoundPropertyRef 路由到 need_whole_vertex
- FilterOp / SortOp 内（`in_schema_changing_op=false`）：BoundPropertyRef 追加到 vertex_props / edge_props
- SET / REMOVE target：need_whole_vertex = true
- DELETE target：根据 TargetKind 设置 need_whole_vertex / need_whole_edge
- BoundLabelCast（n::Label）：need_whole_vertex = true
- properties(n) / keys(n)：根据参数类型设置 need_whole_vertex / need_whole_edge
- labels(n)：need_vertex_labels = true
- type(r)：need_edge_type = true

**Column rewrite**（`rewriteColumnIndices`）：将 FilterOp / SortOp 中的 BoundPropertyRef 重写为 BoundColumnRef，指向 ProjectionExtract 输出的扁平列。跳过 need_whole_vertex / need_whole_edge 变量（Construct 列原地替换，索引不变）。新增处理 `BoundColumnRef` 直通引用的列偏移校正（按 `PropertyExtractionInfo::base_col`）。

**dispatchProjectionExtract**：替代旧的 5 处 inline loader 调用（Scan × 3 + IndexScan + Expand dst + VarLenExpand dst）。读取 `ctx.requirements`，为每个变量构建 ColumnSpec 数组，插入 ProjectionExtractPhysicalOp。构建 `existing_cols` 集合实现列去重（管线中多次 dispatch 时，已存在的 `var.prop_name` 列不重复 emit）。

> **拓扑/语义边界场景修复**：见本文档开头"拓扑/语义类型对齐（2026-06，feature/projection-extract-op 收尾）"小节——CorrelatedSource 按运行时值推导列 kind、Project 对 BoundColumnRef 用源列类型、column_rewrite 校正 BoundColumnRef 直通引用的 base_col、updateBaseCols 补全 CorrelatedSource/VarLenExpand 的列数累加、属性列去重。

### 历史方案（已删除）

以下算子曾存在，已被 `ProjectionExtractPhysicalOp` 统一替代：

| 算子 | 替代 spec |
|------|----------|
| `VertexLabelReadPhysicalOp` | `LoadVertexLabels` |
| `VertexPropertyReadPhysicalOp` | `LoadVertexProp` |
| `EdgePropertyReadPhysicalOp` | `LoadEdgeProp` / `ConstructEdge` |
| `VertexPropertyExtractPhysicalOp` | `ConstructVertex`（need_whole）或多个 `LoadVertexProp` + `LoadVertexLabels` |
| `EdgePropertyExtractPhysicalOp` | `ConstructEdge`（need_whole）或多个 `LoadEdgeProp` |

对应的内联包装函数 `loadVertexLabelsInPlace` / `loadVertexPropertiesInPlace` / `wrapEdgePropertyRead` / `dispatchVertexPropertyExtract` / `dispatchEdgePropertyExtract` / `canUsePropertyExtract` 同步删除。



## 六、PhysicalPlanner 集成

统合分发函数（`physical_planner.cpp`）：

```cpp
dispatchProjectionExtract(child_result, store, ctx)
```

路径元素属性读取（保留用于 PathBuild）：

```cpp
wrapPathElementPropertyRead(child_result, path_variable, store)
```

| 逻辑算子 | 抽取方式 |
|---------|------|
| BoundScanOp / BoundLabelScanOp | dispatchProjectionExtract |
| BoundExpandOp | dispatchProjectionExtract |
| BoundVarLenExpandOp | dispatchProjectionExtract (dst) + wrapPathElementPropertyRead (path, 若存在) |
| BoundPathBuildOp | wrapPathElementPropertyRead |
| tryBoundIndexScan / tryBoundEdgeIndexScan | dispatchProjectionExtract |

## 七、关键实现细节

### 7.1 DICTIONARY selection vector 保留

Read 算子对 DICTIONARY 列做 passthrough 时必须**保留原始 dict_sel**，不能用 identity selection 替换。原始 buffer 的行数可能远小于逻辑行数（如 CorrelatedSource buffer 只有 1 行，但 Expand 输出 N 行）。FLAT 列则使用 identity selection。

### 7.2 自关系变量搜索

`(n)-[r]->(n)` 中变量 `n` 在 schema 中出现两次。dispatchProjectionExtract 的变量查找从后往前搜索，匹配 Expand 产出的新列（末尾出现），而非 Scan 的输入列。

### 7.3 keys(n) 全属性需求

`keys(n)` 和 `properties(n)` 在 binder 中触发所有标签的全属性需求，确保 ConstructVertex 加载全部属性。TCK 快照验证依赖此行为。

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
