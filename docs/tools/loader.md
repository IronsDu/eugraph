# CSV Loader

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

批量 CSV 数据装载工具（`eugraph-loader`），通过 RPC 连接 eugraph server，将 LDBC SNB 格式的 CSV 数据导入图数据库。

---

## 一、使用

```bash
eugraph-loader --host 127.0.0.1 --port 9090 --data-dir ./csv-data --batch-size 500
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | 127.0.0.1 | Server 地址 |
| `--port` | 9090 | Server 端口 |
| `--data-dir` | **必填** | CSV 文件所在目录 |
| `--batch-size` | 500 | 每 RPC 批次的记录数 |

---

## 二、数据格式

### 目录结构

```
data-dir/
├── static/          # 静态数据
└── dynamic/         # 动态数据
```

### 文件命名

- 点文件: `{label}_0_0.csv`（3 段）
- 边文件: `{srcLabel}_{edgeType}_{dstLabel}_0_0.csv`（5 段）

### CSV 格式

分隔符: `|`，首行为表头。

**点文件**：首列 `id` 为 CSV 主键（INT64），剩余列为属性值。

**边文件**：前两列为起点/终点的 CSV 主键，剩余列为边属性。

### 类型推断

采样前 N 行：尝试 `int64_t` → INT64，否则 → STRING。

---

## 三、装载流程

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

## 四、服务端扩展

Loader 使用两个批量 RPC 端点：

- **`batchInsertVertices`**：接收 label_name + 记录列表 → 批量分配 VertexId → 写入顶点（独立事务）→ 返回 VertexId 列表
- **`batchInsertEdges`**：接收 edge_label_name + 记录列表（含已解析的 src/dst VertexId）→ 批量分配 EdgeId → 写入边（独立事务）

两个端点均使用独立事务（不参与外层 Cypher 事务）。

---

## 五、文件结构

```
src/loader/
  loader_main.cpp       # 入口：参数解析、流程编排
  csv_loader.hpp/cpp    # 目录扫描、CSV 解析、Schema 构建、数据加载
```

## 六、错误处理

| 场景 | 处理方式 |
|------|---------|
| Server 连接失败 | 报错退出 |
| CSV 文件解析失败 | 报错退出，打印文件名和行号 |
| 标签/关系类型已存在 | 跳过创建，继续装载 |
| 批量写入失败 | 回滚当前批次，报错退出 |
| 边的 src/dst 顶点未找到 | 报错退出，打印缺失的顶点信息 |
