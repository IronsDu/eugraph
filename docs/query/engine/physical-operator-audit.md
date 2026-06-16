# 物理算子职责审计

## 核心问题

当前架构中，物理算子"自给自足"地工作——扫描全量数据、加载全部标签和属性，然后把整个 DataChunk 往后传。下游算子实际只需要其中一小部分数据，但上游完全不知道。根因是**规划阶段没有把下游需求传递给上游算子**。

## 一、属性/标签无条件加载

### VertexLabelReadPhysicalOp
- 每行一个 `co_await store_.getVertexLabels(vv.id)`，即使 RETURN 没有引用 `labels(n)`
- planner 无条件在 scan/expand 后包裹 `wrapVertexLabelRead`，不检查是否需要

### VertexPropertyReadPhysicalOp
- 正确按 `label_prop_ids_` 过滤了需要的属性，但 TODO 提示未批量 co_await

### PathElementPropertyReadPhysicalOp
- **最严重**：加载路径元素全部属性，无 prop_id 过滤
- 调用 `co_await store_.getVertexProperties(vv.id, lid)` 和 `co_await store_.getEdgeProperties(...)` 时未传 projection

### EdgeIndexScanPhysicalOp
- 无条件获取 src 和 dst 两端的 vertex labels，与实际查询需求无关

### LabelScanPhysicalOp
- 始终填充 `vv.labels`，即使下游不需要

### MergePhysicalOp
- 输出顶点时加载全部属性（所有 label + anon label），无投影过滤

## 二、逐行 co_await（IO 放大）

以下算子代码中均有 `// TODO: batch co_await` 注释，当前每个 chunk 行都独立发起一次 storage 协程调用：
- `VertexLabelReadPhysicalOp`
- `VertexPropertyReadPhysicalOp`
- `EdgePropertyReadPhysicalOp`

1024 行的 chunk = 1024 次 round-trip，而非一次批量查询。

## 三、全量扫描 + 阻塞式 pipeline

### AllNodeScanPhysicalOp
- 先扫描所有 label 的全部顶点到 `std::unordered_set<VertexId>`，再逐行输出
- 百万级顶点图内存占用可达上百 MB
- 阻塞 pipeline：所有顶点扫描完成前，下游无法开始处理

## 四、算子链中重复拆装 DataChunk

`VertexLabelRead → VertexPropertyRead` 等链式包装时，每个算子执行：
1. `chunk->toRows()` 全量物化
2. 修改其中一列
3. 重建 DataChunk（其余列做 DICTIONARY 引用）

两个算子链在一起，数据被拆装两次，纯 CPU 浪费。可合并为单算子一次完成。

## 五、缺少列裁剪

从 Scan → Expand → Filter → LabelRead → ... 十几列数据一路透传，最终 Project 可能只保留 2 列。中间每个算子都在复制和传递从未使用的列。planner 层面没有列级别的需求传递机制。

## 六、AllNodeScan + 标签过滤未优化

`MATCH (n) WHERE n:Person RETURN n` 执行的是 AllNodeScan + Filter，而非直接 LabelScan(Person)。可在 planner 中实现简单的 Filter-pushdown 重写规则。

## 建议实施顺序

| 优先级 | 改动 | 影响 |
|--------|------|------|
| P0 | 在 planning 阶段做需求分析，收集 RETURN/WHERE/ORDER BY 引用的列、标签、属性，沿算子树向下传递 | 同时解决一、二、五 |
| P0 | PathElementPropertyRead 加 prop_id 过滤 | 最大单一浪费点 |
| P1 | 合并 VertexLabelRead + VertexPropertyRead 为单算子 | 消除重复拆装 |
| P1 | AllNodeScan 改为流式 + 去重优化 | 大图内存 |
| P2 | AllNodeScan + label filter → LabelScan 重写 | planner 优化规则 |

## 设计原则

物理算子应遵循"按需加载"原则：每个算子只获取和输出下游实际需要的数据。列需求、属性需求、标签需求应从 planner 向下传递，由各算子据此裁剪工作。
