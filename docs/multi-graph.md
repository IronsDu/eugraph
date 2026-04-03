# 多图支持设计

> 参见 [overview.md](overview.md) 返回文档导航

## 一、多图支持 — 列簇隔离方案

### 1.1 设计目标

- 支持在同一个 RocksDB 实例中创建多个图（Graph）
- 删除图的速度要快（接近 O(1)）
- 各图数据物理隔离，互不影响

### 1.2 方案概述

利用 RocksDB 的 Column Family（列簇）机制实现图的隔离：

```
RocksDB 实例
├── "default" 列簇              ← 全局元数据（图名 → graph_id 映射等）
├── "graph_1" 列簇              ← 图1 的全部数据（L/I/P/R/X/E/G/D/M）
├── "graph_2" 列簇              ← 图2 的全部数据
└── "graph_N" 列簇              ← 图N 的全部数据
```

- **`default` 列簇**：存储全局信息，包括图名到 graph_id 的映射、graph_id 分配计数器
- **`graph_{id}` 列簇**：存储单个图的所有数据，Key 编码与现有设计完全一致（L/I/P/R/X/E/G/D/M），无需额外前缀
- **删除图** = 删除 `default` 中的映射记录 + `DropColumnFamily("graph_{id}")`，近乎 O(1)

### 1.3 default 列簇的 Key 编码

```
图名映射:
  Key:   G|{graph_name:string}
  Value: {graph_id:uint32 BE}|{created_at:uint64 BE}|{状态标志}

graph_id 分配计数器:
  Key:   N|next_graph_id
  Value: {next_graph_id:uint32 BE}

已删除图待清理标记（可选）:
  Key:   D|{graph_id:uint32 BE}
  Value: {graph_name:string}|{删除时间}
```

### 1.4 操作流程

#### 创建图

```
1. 开启事务
2. 检查 G|{graph_name} 是否已存在
3. 读取 N|next_graph_id，分配新 graph_id，递增并写回
4. 在 default 列簇写入 G|{graph_name} → {graph_id}|{created_at}
5. 调用 RocksDB::CreateColumnFamily("graph_{id}")
6. 提交事务
```

#### 删除图

```
1. 开启事务
2. 读取 G|{graph_name} 获取 graph_id
3. 从 default 列簇删除 G|{graph_name}
4. 调用 RocksDB::DropColumnFamily("graph_{id}")
5. 释放内存中的列簇句柄
6. 提交事务
```

#### 使用图（切换当前图）

```
1. 读取 G|{graph_name} 获取 graph_id
2. 获取 "graph_{id}" 列簇的句柄
3. 后续所有操作使用该句柄执行
```

### 1.5 列簇句柄管理

```cpp
class ColumnFamilyManager {
public:
    // 启动时：ListColumnFamilies → 打开所有列簇 → 从 default 加载映射
    // 运行时：通过 graph_name 或 graph_id 获取对应 Handle*
    // 创建/删除图时：动态维护 Handle 的增删

    rocksdb::ColumnFamilyHandle* getDefaultHandle();       // default 列簇
    rocksdb::ColumnFamilyHandle* getGraphHandle(uint32_t graph_id);  // 某个图的列簇
};
```

- 所有句柄由 ColumnFamilyManager 统一管理
- 使用 `std::shared_mutex` 保护：读操作用共享锁，创建/删除图用独占锁
- 删除图时，先标记停用该句柄，等待进行中的操作完成后释放

### 1.6 启动与恢复

RocksDB 要求打开数据库时指定所有列簇名。启动流程：

```
1. 调用 RocksDB::ListColumnFamilies() 获取所有列簇名
2. 组装 ColumnFamilyDescriptors（default + 所有 graph_* 列簇）
3. 调用 RocksDB::Open() 打开数据库
4. 从 default 列簇读取所有 G|{name} 映射，构建内存索引
5. 一致性校验：
   a. 对比映射中的 graph_id 集合与实际列簇名集合
   b. 若存在映射中有但列簇不存在的 graph_id → 映射不完整（创建过程中崩溃）
      → 从 default 列簇中删除该映射记录（视为创建失败）
   c. 若存在列簇但映射中没有的 graph_id → 删除不完整（删除过程中崩溃）
      → 调用 DropColumnFamily 清理孤儿列簇
   d. 修复完成后，映射与列簇应完全一致
6. 启动完成
```

### 1.7 对 IKVEngine 接口的影响

当前 IKVEngine 是单列簇设计。需要扩展以支持多列簇：

**方案：引入 graph_id 参数**

```cpp
// 方案一：在现有接口中增加 graph_id 参数（可选，默认无）
class IKVEngine {
public:
    virtual Result<void> put(GraphId graph_id, std::string_view key, std::string_view value) = 0;
    virtual Result<std::optional<std::string>> get(GraphId graph_id, std::string_view key) = 0;
    // ...
};

// 方案二：为每个图创建独立的 IKVEngine 实例（代理模式）
class IKVEngine {
public:
    // 原有接口不变，内部通过不同的 ColumnFamilyHandle* 操作
    static std::unique_ptr<IKVEngine> create(RocksDB*, ColumnFamilyHandle*);
};
```

具体选哪种方案在实现阶段确定，两种都可以。

### 1.8 graph_id 分配

- graph_id 使用 uint32，单调递增，不回收
- 分配计数器存储在 `default` 列簇的 `N|next_graph_id` 中
- 即使图被删除，其 graph_id 也不会被复用，避免歧义

### 1.9 测试用例

| # | 测试场景 | 验证点 |
|---|---------|--------|
| 1 | 创建图 | 创建后能通过图名查到 graph_id，对应列簇存在 |
| 2 | 创建重名图 | 返回错误，不产生副作用 |
| 3 | 删除图 | 映射记录消失，列簇不存在，操作快速完成 |
| 4 | 删除不存在的图 | 返回错误，无副作用 |
| 5 | 删除图后数据不可访问 | 删除后 get/scan 均返回错误或空 |
| 6 | 多图隔离 | 图A 和图B 各自写入数据，互不可见 |
| 7 | 启动恢复 - 创建中途崩溃 | default 有映射但无对应列簇 → 自动清理映射 |
| 8 | 启动恢复 - 删除中途崩溃 | 有孤儿列簇但无映射 → 自动清理列簇 |
| 9 | 删除图后 graph_id 不复用 | 删除图1后再创建新图，id 应为2而非1 |

### 1.10 已知风险与注意事项

| # | 问题 | 影响 | 应对策略 |
|---|------|------|----------|
| 1 | 创建/删除图的原子性 | 两步操作（写映射 + 操作列簇）之间崩溃会导致不一致 | 启动时做一致性校验并自动修复（见 1.6） |
| 2 | 打开数据库需预知所有列簇 | 无法直接 Open，必须先 ListColumnFamilies 再 Open | 封装在启动流程中，对上层透明 |
| 3 | WAL 共享 | 所有列簇共享同一个 WAL，崩溃恢复时需要回放所有图的日志 | RocksDB 内部已处理，无额外工作；但恢复时间与总写入量成正比 |
| 4 | 列簇句柄的生命周期 | 删除图后，仍有进行中的操作持有旧句柄会导致崩溃 | 引用计数 + 标记停用，等待进行中操作完成后释放 |
| 5 | graph_id 不回收 | 大量创建又删除图后，graph_id 会越来越大，列簇名变长 | uint32 上限约 42 亿，实际不会用完 |
| 6 | 磁盘空间延迟释放 | DropColumnFamily 是元数据操作，数据空间由后台 Compaction 异步回收 | 对用户透明 |

