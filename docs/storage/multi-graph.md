# 多图支持设计

> [设计规划] 参见 [README.md](../README.md) 返回文档导航

**状态**：已实现。支持在同一个 eugraph-server 进程中创建和管理多个图。

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

#### 创建图（当前实现）

`GraphManager::createGraph` 持有图表写锁，串行化创建：

```
1. catalog_.getGraph(name)：已存在 ⇒ 直接返回既有 entry
2. catalog_.createGraph(name)（每一步一个短生命周期 WtSession，autocommit；无显式事务）：
   a. 检查 G|{graph_name} 是否已存在
   b. allocateNextId()：读 N|next_graph_id，分配并写回（另一把 WtSession）
   c. 写 G|{graph_name} → {graph_id}|{created_at}
3. openGraphInstance：创建 graph_{id}/{data,meta}/ 目录，
   打开 WtConnectionStore data_conn / meta_conn，
   ensure_tables → AsyncGraphMetaStore → SessionPool → QueryExecutor
4. 将实例放入 graphs_ 表
```

> 注：catalog 的多步读写**没有**显式事务；由 `GraphManager` 的写锁保证创建序列不交错。
> `N|next_graph_id` 的“读—写”若脱离该锁并不原子（见
> [session-cursor-pool-design.md §11](session-cursor-pool-design.md)）。

#### 删除图（当前实现）

```
1. 从 graphs_ 表摘除实例，并从 catalog 删除 G|{graph_name}（持写锁）
2. usage->close()：关闸，拒绝新使用者
3. usage->waitDrained(10s)：等在飞使用者归零（超时则记录错误后继续）
4. session_pool->clear()：只关闭空闲会话（借出会话由租约释放时关闭）
5. inst.reset()：按声明逆序销毁（会话池 → store → data/meta 连接）
6. 同步 remove_all(graph_{id}/)
```

> 注：没有“删除中”状态标记，也没有后台异步删除；删除目录是同步的。
> 关闸/等归零是 `GraphUsageGate`，避免在飞查询与连接关闭并发（否则 WT_PANIC）。

#### 使用图（切换当前图）

```
1. GraphManager::acquireGraph(name)：shared_lock 内查 graphs_，并对该图 usage->tryEnter()
2. 返回 GraphLease（GraphInstance* + GraphUsageGate::Guard）
3. 语句持租约执行；租约归零前 DROP 不会关闭该图连接
```

> 已无“当前图连接句柄”这一层：调用方拿到的是 `GraphInstance*`（租约内有效）。

### 2.5 连接管理（当前实现）

```cpp
struct GraphInstance {
    std::shared_ptr<GraphUsageGate> usage;
    WtConnectionStore data_conn;      // 连接所有者（data）
    WtConnectionStore meta_conn;      // 连接所有者（meta）
    std::unique_ptr<SyncGraphDataStore> sync_data;   // 只 bind 连接
    std::unique_ptr<SyncGraphMetaStore> sync_meta;
    std::unique_ptr<AsyncGraphDataStore> async_data;
    std::unique_ptr<AsyncGraphMetaStore> async_meta;
    std::unique_ptr<compute::QueryExecutor> executor;
    std::shared_ptr<SessionPool> session_pool;       // 声明在最后 ⇒ 最先销毁
};

class GraphManager {
    std::unordered_map<std::string, std::unique_ptr<GraphInstance>> graphs_;
    std::shared_mutex mu_;
    std::shared_ptr<IoScheduler> io_scheduler_;
};
```

- 连接由每个 `GraphInstance` 的 `WtConnectionStore` 持有（data / meta 各一），store 只绑定；
  **声明顺序**保证会话池先于连接销毁。
- `std::shared_mutex` 保护图查找/增删：查找用共享锁，创建/删除用独占锁。
- 删除图时没有“标记停用”字段：用 `GraphUsageGate` 关闸 → 等在飞归零 → 清池 → 销毁实例 → 删目录。

### 2.6 启动与恢复（当前实现）

```
1. 打开 catalog 的 WT 数据库（catalog 自己持有 WtConnectionStore）
2. catalog_.listGraphs() 扫描 G|{name} 映射
3. 逐个 openGraphInstance：
   打开 graph_{id}/data、graph_{id}/meta 连接 → ensure_tables →
   AsyncGraphMetaStore → SessionPool → QueryExecutor
4. 失败处理（与“静默删数据”相反）：
   a. 映射中有、目录不存在 → 删除该 stale catalog entry
   b. 目录存在但打不开/格式不兼容 → **保留 catalog entry**，记录错误并跳过该图
      （绝不因一次读取失败就让数据不可达）
5. catalog 中没有 default 图 → 创建并打开
6. 启动 checkpoint 线程
```

> 不扫描“目录存在但 catalog 没有”的孤儿目录；这类目录需要人工处理（避免误删用户数据）。

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
| 8 | 启动恢复 - 删除中途崩溃 | 有孤儿目录但无映射 → **不自动清理**（避免误删）；需人工处理 |
| 9 | 删除图后 graph_id 不复用 | 删除图1后再创建新图，id 应为2而非1 |

### 2.9 已知风险与注意事项

| # | 问题 | 应对策略 |
|---|------|----------|
| 1 | 创建/删除图的原子性 | catalog 写锁串行化创建；启动时对“有映射无目录”清 stale entry，“有目录但打不开”保留 entry 报错 |
| 2 | WT_CONNECTION 的生命周期 | `GraphUsageGate`：关闸 → 等在飞归零（10s）→ 清会话池 → 销毁连接 |
| 3 | graph_id 不回收 | uint32 上限约 42 亿，实际不会用完 |
| 4 | 磁盘空间释放 | 删除图时**同步** `remove_all`（大目录会阻塞，当前无后台清理） |
| 5 | 打开连接数 | 每图 2 个 WT_CONNECTION，需限制最大图数量 |
