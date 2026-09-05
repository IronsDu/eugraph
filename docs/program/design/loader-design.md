# Loader 设计

> [当前实现] 参见 [README.md](README.md) 返回文档导航

使用文档见 [loader.md](../usage/loader.md)。

---

## 装载流程

```
1. 扫描目录，收集并分类 CSV 文件
2. 解析表头，推断属性类型
3. 创建所有标签（DDL）
4. 创建所有关系类型（DDL）
5. 并发/串行写入点文件 → 收集 (label, csv_id) → VertexId 映射
6. 为每个标签的 ID 属性创建唯一索引（CREATE UNIQUE INDEX）
7. 并发/串行写入边文件（使用映射解析顶点）
```

默认串行（`--concurrency 1`）。配置 `--eventbase-threads N --concurrency M` 后，
loader 会创建 `N` 个独立 RPC EventBase 客户端，并用最多 `M` 个工作线程并发处理 CSV 文件：
先并行装载 vertex 文件，待点映射全部就绪后再并行装载 edge 文件。

映射使用 `unordered_map<string, unordered_map<int64_t, VertexId>>`（label_name → csv_id → vertex_id）。

ID 特征：CSV 中的 id 值跨标签不唯一，因此需建立 (label, csv_id) → VertexId 映射。

---

## 服务端扩展

Loader 使用两个批量 RPC 端点：

- **`batchInsertVertices`**：接收 label_name + 记录列表 → 批量分配 VertexId → 写入顶点（独立事务）→ 返回 VertexId 列表
- **`batchInsertEdges`**：接收 edge_label_name + 记录列表（含已解析的 src/dst VertexId）→ 批量分配 EdgeId → 写入边（独立事务）

两个端点均使用独立事务（不参与外层 Cypher 事务）。

> 并发装载要求服务端能够安全地并发分配 VertexId/EdgeId。`AsyncGraphMetaStore` 对
> `nextVertexIdRange()` / `nextEdgeIdRange()` 的计数更新和 `M|next_ids` 持久化做了互斥保护，
> 避免多个批量导入请求同时到达时得到重叠 ID 区间。
>
> 为了减少每次 RPC 批次都写 `M|next_ids` 的开销，服务端采用 ID 缓存批量分配：
> 内存中缓存一段已持久化的 VertexId/EdgeId 区间，正常分配从缓存取用；
> 缓存耗尽时才从持久化高水位批量预留一批（当前 16384 个）并持久化一次。
>
> 另外，批量写入路径复用 `TxnState` 中缓存的 WT cursor（`tablePutTxn`），
> 避免每条边/点重复 `open_cursor` 与 dhandle/malloc 开销。

---

## 文件结构

```
src/program/loader/
  loader_main.cpp       # 入口：参数解析、流程编排
  csv_loader.hpp/cpp    # 目录扫描、CSV 解析、Schema 构建、数据加载
```

## 错误处理

| 场景 | 处理方式 |
|------|---------|
| Server 连接失败 | 报错退出 |
| CSV 文件解析失败 | 格式错误的行会抛异常导致退出 |
| 标签/关系类型已存在 | Handler 返回 id=0，Loader 继续执行（幂等） |
| 批量写入失败 | 回滚当前批次，报错退出 |
| 边的 src/dst 顶点未找到 | 跳过该边（计数 skipped），继续加载 |
