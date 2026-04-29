# Loader 设计

> [当前实现] 参见 [README.md](README.md) 返回文档导航

使用文档见 [program_usage/loader.md](../program_usage/loader.md)。

---

## 装载流程

```
1. 扫描目录，收集并分类 CSV 文件
2. 解析表头，推断属性类型
3. 创建所有标签（DDL）
4. 创建所有关系类型（DDL）
5. 逐文件写入点数据 → 收集 (label, csv_id) → VertexId 映射
6. 逐文件写入边数据（使用映射解析顶点）
```

映射使用 `unordered_map<string, unordered_map<int64_t, VertexId>>`（label_name → csv_id → vertex_id）。

ID 特征：CSV 中的 id 值跨标签不唯一，因此需建立 (label, csv_id) → VertexId 映射。

---

## 服务端扩展

Loader 使用两个批量 RPC 端点：

- **`batchInsertVertices`**：接收 label_name + 记录列表 → 批量分配 VertexId → 写入顶点（独立事务）→ 返回 VertexId 列表
- **`batchInsertEdges`**：接收 edge_label_name + 记录列表（含已解析的 src/dst VertexId）→ 批量分配 EdgeId → 写入边（独立事务）

两个端点均使用独立事务（不参与外层 Cypher 事务）。

---

## 文件结构

```
src/loader/
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
