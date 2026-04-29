# 多图支持设计

> [设计规划] 参见 [README.md](../README.md) 返回文档导航

**状态**：设计阶段，尚未实现。当前为单图模式（每个 server 进程对应一个图）。

## 一、当前实现

当前每个 `eugraph-server` 进程管理一个图，使用两个独立的 WiredTiger 数据库：

```
data-dir/
├── data/     ← 图数据（WT_CONNECTION，按 label 分表存储）
└── meta/     ← 元数据（WT_CONNECTION，schema 信息）
```

WiredTiger 表按 label/edge_label ID 分表（详见 [kv-encoding.md](kv-encoding.md)）：

| 表名 | 说明 |
|------|------|
| `table:label_reverse` | 顶点→标签反向索引（全局） |
| `table:pk_forward` / `table:pk_reverse` | 主键正反向查询（全局） |
| `table:edge_index` | 邻接索引（全局） |
| `table:label_fwd_{label_id}` | 标签正向索引（per-label） |
| `table:vprop_{label_id}` | 顶点属性（per-label） |
| `table:etype_{edge_label_id}` | 边类型索引（per-edge-label） |
| `table:eprop_{edge_label_id}` | 边属性（per-edge-label） |

如需运行多个图，目前只能启动多个 server 进程，各自使用不同的 `--data-dir`。

## 二、多图设计方案 — WiredTiger 多数据库隔离

### 2.1 设计目标

- 支持在同一个 eugraph-server 进程中创建和管理多个图
- 删除图的速度要快（接近 O(1)）
- 各图数据物理隔离，互不影响

### 2.2 方案概述

利用 WiredTiger 的独立数据库（不同目录）实现图的隔离：

```
data-dir/
├── catalog/          ← 图目录（graph_name → db_path 映射）
├── graph_1/
│   ├── data/         ← 图1 的 WT 数据库
│   └── meta/         ← 图1 的 WT 元数据库
├── graph_2/
│   ├── data/
│   └── meta/
└── graph_N/
    ├── data/
    └── meta/
```

- **catalog**：存储全局信息，包括图名到路径的映射、graph_id 分配计数器
- **`graph_{id}/`**：每个图独立的 WT 数据库目录，内部结构与当前单图完全一致
- **删除图** = 从 catalog 删除映射 + 异步清理目录，近乎 O(1)

### 2.3 catalog 的 Key 编码

```
图名映射:
  Key:   G|{graph_name:string}
  Value: {graph_id:uint32 BE}|{created_at:uint64 BE}|{状态标志}

graph_id 分配计数器:
  Key:   N|next_graph_id
  Value: {next_graph_id:uint32 BE}
```

### 2.4 操作流程

#### 创建图

```
1. 开启 catalog 事务
2. 检查 G|{graph_name} 是否已存在
3. 读取 N|next_graph_id，分配新 graph_id，递增并写回
4. 在 catalog 写入 G|{graph_name} → {graph_id}|{created_at}
5. 创建 data-dir/graph_{id}/data/ 和 data-dir/graph_{id}/meta/ 目录
6. 打开 WT_CONNECTION（data 和 meta 各一个）
7. 提交事务
```

#### 删除图

```
1. 标记图为 "删除中"（写回 catalog）
2. 停止接受该图的新请求，等待进行中操作完成
3. 关闭该图的 WT_CONNECTION
4. 从 catalog 删除 G|{graph_name} 映射
5. 后台异步删除 graph_{id}/ 目录（不阻塞）
```

#### 使用图（切换当前图）

```
1. 读取 catalog 中 G|{graph_name} 获取 graph_id
2. 获取该图的 WT_CONNECTION 句柄
3. 后续所有操作使用该图的存储实例
```

### 2.5 连接管理

```cpp
// 每个图拥有独立的 WT_CONNECTION 对
struct GraphConnection {
    WT_CONNECTION* data_conn;
    WT_CONNECTION* meta_conn;
    std::unique_ptr<GraphSchema> schema;
};

class GraphManager {
    // 启动时：从 catalog 加载所有图映射，打开各图的 WT 连接
    // 运行时：通过 graph_name 获取 GraphConnection
    // 创建/删除图时：动态维护连接的增删
    std::unordered_map<std::string, std::unique_ptr<GraphConnection>> graphs_;
    std::shared_mutex mu_;
};
```

- 所有连接由 GraphManager 统一管理
- 使用 `std::shared_mutex` 保护：读操作用共享锁，创建/删除图用独占锁
- 删除图时，先标记停用，等待进行中操作完成后关闭连接

### 2.6 启动与恢复

```
1. 打开 catalog 的 WT 数据库
2. 扫描所有 G|{name} 映射
3. 逐个打开 graph_{id}/ 的 WT 连接，加载 schema
4. 一致性校验：
   a. 映射中有但目录不存在的 graph_id → 创建过程中崩溃
      → 从 catalog 中删除该映射（视为创建失败）
   b. 目录存在但映射中没有的 graph_id → 删除不完整
      → 异步清理孤儿目录
5. 启动完成
```

### 2.7 graph_id 分配

- graph_id 使用 uint32，单调递增，不回收
- 分配计数器存储在 catalog 的 `N|next_graph_id` 中
- 即使图被删除，其 graph_id 也不会被复用，避免歧义

### 2.8 测试用例

| # | 测试场景 | 验证点 |
|---|---------|--------|
| 1 | 创建图 | 创建后能通过图名查到 graph_id，对应目录存在 |
| 2 | 创建重名图 | 返回错误，不产生副作用 |
| 3 | 删除图 | 映射记录消失，目录被清理，操作快速完成 |
| 4 | 删除不存在的图 | 返回错误，无副作用 |
| 5 | 删除图后数据不可访问 | 删除后所有操作返回错误或空 |
| 6 | 多图隔离 | 图A 和图B 各自写入数据，互不可见 |
| 7 | 启动恢复 - 创建中途崩溃 | catalog 有映射但无目录 → 自动清理映射 |
| 8 | 启动恢复 - 删除中途崩溃 | 有孤儿目录但无映射 → 自动清理 |
| 9 | 删除图后 graph_id 不复用 | 删除图1后再创建新图，id 应为2而非1 |

### 2.9 已知风险与注意事项

| # | 问题 | 应对策略 |
|---|------|----------|
| 1 | 创建/删除图的原子性 | 启动时一致性校验并自动修复 |
| 2 | WT_CONNECTION 的生命周期 | 引用计数 + 标记停用，等待进行中操作完成 |
| 3 | graph_id 不回收 | uint32 上限约 42 亿，实际不会用完 |
| 4 | 磁盘空间释放 | 删除图时异步清理目录，不阻塞服务 |
| 5 | 打开连接数 | 每图 2 个 WT_CONNECTION，需限制最大图数量 |
