# 元数据服务设计

## 一、概述

元数据服务负责管理 Label（点类型）、EdgeLabel（关系类型）的定义，以及全局 ID 分配。它是查询引擎执行 Cypher 语句的前置依赖——`QueryExecutor` 需要通过元数据服务解析标签名称到 ID 的映射。

## 二、接口定义

```cpp
// src/metadata_service/metadata_service.hpp

#pragma once

#include "common/types/graph_types.hpp"
#include "storage/graph_store.hpp"

#include <folly/coro/Task.h>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {

/// 元数据服务接口。
/// 管理 Label/EdgeLabel 的定义（名称 ↔ ID 映射、属性定义）以及全局 ID 分配。
/// 协程风格，所有操作通过 co_await 调用。
class IMetadataService {
public:
    virtual ~IMetadataService() = default;

    // ==================== 生命周期 ====================

    /// 打开元数据服务，加载持久化的元数据到内存缓存。
    /// @param store  复用已有的 IGraphStore 实例（共享 WiredTiger 连接）
    virtual folly::coro::Task<bool> open(IGraphStore& store) = 0;
    virtual folly::coro::Task<void> close() = 0;

    // ==================== Label（点类型）管理 ====================

    /// 创建标签。分配 LabelId，注册属性定义（含 prop_id 分配），
    /// 持久化到 M| KV，并调用 IGraphStore::createLabel() 创建存储表。
    /// @return 新分配的 LabelId
    /// @throws 如果同名标签已存在，返回错误
    virtual folly::coro::Task<LabelId> createLabel(
        const std::string& name,
        const std::vector<PropertyDef>& properties = {}) = 0;

    /// 根据名称获取 LabelId。走内存缓存。
    virtual folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) = 0;

    /// 根据 LabelId 获取名称。走内存缓存。
    virtual folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) = 0;

    /// 根据名称获取完整的标签定义（含属性列表）。
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) = 0;

    /// 根据 LabelId 获取完整的标签定义。
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) = 0;

    /// 列出所有已定义的标签。
    virtual folly::coro::Task<std::vector<LabelDef>> listLabels() = 0;

    // ==================== EdgeLabel（关系类型）管理 ====================

    /// 创建关系类型。分配 EdgeLabelId，注册属性定义，
    /// 持久化到 M| KV，并调用 IGraphStore::createEdgeLabel() 创建存储表。
    virtual folly::coro::Task<EdgeLabelId> createEdgeLabel(
        const std::string& name,
        const std::vector<PropertyDef>& properties = {}) = 0;

    virtual folly::coro::Task<std::optional<EdgeLabelId>> getEdgeLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getEdgeLabelName(EdgeLabelId id) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) = 0;
    virtual folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() = 0;

    // ==================== ID 分配 ====================

    /// 分配下一个全局唯一的 VertexId（原子递增）。
    virtual folly::coro::Task<VertexId> nextVertexId() = 0;

    /// 分配下一个全局唯一的 EdgeId（原子递增）。
    virtual folly::coro::Task<EdgeId> nextEdgeId() = 0;
};

} // namespace eugraph
```

## 三、持久化设计

元数据通过 `IGraphStore` 存储到 `M|` 前缀的 KV 中。启动时全量加载到内存缓存，后续读取走缓存，写入时同时更新缓存和 KV。

### 3.1 KV 编码

沿用 architecture.md 中已定义的 `M|` 编码：

```
// Label 定义（按名称索引）
Key:   M|label:{name}
Value: {label_id:uint16}|{properties_def 序列化}

// Label 定义（按 ID 索引）
Key:   M|label_id:{id:uint16}
Value: {name}|{properties_def 序列化}

// EdgeLabel 定义（按名称索引）
Key:   M|edge_label:{name}
Value: {edge_label_id:uint16}|{properties_def 序列化}

// EdgeLabel 定义（按 ID 索引）
Key:   M|edge_label_id:{id:uint16}
Value: {name}|{properties_def 序列化}

// ID 计数器
Key:   M|next_ids
Value: {next_vertex_id:uint64}|{next_edge_id:uint64}|{next_label_id:uint16}|{next_edge_label_id:uint16}
```

### 3.2 内存缓存结构

```cpp
struct MetadataCache {
    // Label
    std::unordered_map<std::string, LabelDef> label_by_name;   // name → LabelDef
    std::unordered_map<LabelId, LabelDef> label_by_id;         // id → LabelDef
    LabelId next_label_id = 1;                                  // 下一个可分配的 LabelId

    // EdgeLabel
    std::unordered_map<std::string, EdgeLabelDef> edge_label_by_name;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_by_id;
    EdgeLabelId next_edge_label_id = 1;

    // ID 分配
    VertexId next_vertex_id = 1;
    EdgeId next_edge_id = 1;
};
```

### 3.3 序列化格式

使用简单的二进制序列化（非 protobuf），按字段顺序拼接：

```
LabelDef 序列化:
  label_id      : uint16
  name_length   : uint16
  name          : bytes[name_length]
  prop_count    : uint16
  properties[]  : PropertyDef 序列化 × prop_count

PropertyDef 序列化:
  prop_id       : uint16
  name_length   : uint16
  name          : bytes[name_length]
  type          : uint8   (PropertyType 枚举值)
  required      : uint8   (0/1)
  has_default   : uint8   (0/1)
  [default_value: PropertyValue 序列化]  // 仅当 has_default=1

PropertyValue 序列化:
  type_tag      : uint8   (variant index)
  [value        : 按类型编码]
```

## 四、核心流程

### 4.1 启动流程（open）

```
1. 保存 IGraphStore 引用
2. 开启事务
3. 读取 M|label:* 前缀，反序列化所有 LabelDef → label_by_name / label_by_id
4. 读取 M|edge_label:* 前缀，反序列化所有 EdgeLabelDef → edge_label_by_name / edge_label_by_id
5. 读取 M|next_ids → 恢复 next_label_id / next_edge_label_id / next_vertex_id / next_edge_id
6. 提交事务
```

### 4.2 createLabel 流程

```
1. 检查 label_by_name 是否已存在同名标签 → 已存在则返回错误
2. 分配 LabelId = next_label_id++
3. 为 properties 分配 prop_id（从 1 开始递增）
4. 开启事务
5. 序列化 LabelDef，写入 M|label:{name} 和 M|label_id:{id}
6. 更新 M|next_ids（持久化 next_label_id）
7. 提交事务
8. 调用 IGraphStore::createLabel(label_id) 创建存储表
9. 更新内存缓存 label_by_name / label_by_id
```

### 4.3 ID 分配流程

```
1. nextVertexId() / nextEdgeId()
2. 返回当前值，并递增
3. 定期或每次分配后更新 M|next_ids（持久化）
```

**注意**：ID 分配的持久化策略——每次分配都写 KV 会有性能开销。当前单进程阶段，采用**每次分配都持久化**的简单策略，避免重启后 ID 重复。后续分布式阶段再优化为批量预分配。

## 五、与 QueryExecutor 的集成

### 5.1 当前状态

`QueryExecutor` 目前：
- `label_name_to_id_` / `edge_label_name_to_id_` 通过 `setLabelMappings()` 手动设置
- `next_vertex_id_` / `next_edge_id_` 在内存中维护

### 5.2 改造方案

`QueryExecutor` 在构造时接收 `IMetadataService` 引用：

```cpp
class QueryExecutor {
public:
    QueryExecutor(IGraphStore& store, IMetadataService& meta, Config config = {});
    folly::coro::AsyncGenerator<RowBatch> execute(const std::string& cypher_query);
    // 移除 setLabelMappings()
};
```

`execute()` 内部改造：
- 每次执行前从 `IMetadataService` 获取最新的 `label_name_to_id` / `edge_label_name_to_id` 映射
- VertexId / EdgeId 通过 `meta.nextVertexId()` / `meta.nextEdgeId()` 分配
- 移除内部的 `next_vertex_id_` / `next_edge_id_`

### 5.3 Cypher 标签不存在时报错

在 PhysicalPlanner 阶段，当遇到 `CREATE (n:Person ...)` 时：
- 查找 `Person` 的 `label_name_to_id` 映射
- 如果不存在，返回错误信息（不是自动创建）

在 `Match (n:Person)` 时同理。

## 六、实现计划

| # | 任务 | 说明 |
|---|------|------|
| 6.1 | PropertyValue 序列化/反序列化 | 为元数据持久化提供基础能力 |
| 6.2 | MetadataServiceImpl 实现 | KV 读写 + 内存缓存 + 生命周期管理 |
| 6.3 | 单元测试 | Label/EdgeLabel CRUD、ID 分配、持久化恢复 |
| 6.4 | QueryExecutor 集成改造 | 接入 IMetadataService，移除旧的硬编码映射 |

### 6.1 PropertyValue 序列化

新增 `MetadataCodec`，负责 LabelDef / EdgeLabelDef / PropertyValue 的序列化和反序列化。

文件：`src/metadata_service/metadata_codec.hpp/cpp`

### 6.2 MetadataServiceImpl

文件：`src/metadata_service/metadata_service.hpp/cpp`

核心依赖：
- `IGraphStore&` — 复用现有存储实例
- `MetadataCache` — 内存缓存

### 6.3 测试

文件：`tests/test_metadata_service.cpp`

测试场景：
- 创建/查询 Label
- 创建/查询 EdgeLabel
- 重复创建报错
- ID 分配唯一性
- 属性定义完整存储和读取
- 重启后数据恢复（关闭后重新 open，验证缓存正确加载）
- Label 不存在时查询返回 nullopt

### 6.4 QueryExecutor 集成

- 构造函数增加 `IMetadataService&` 参数
- `execute()` 中从元数据服务加载映射、分配 ID
- 更新现有端到端测试，先创建标签再执行 Cypher

## 七、分布式扩展

`IMetadataService` 是抽象接口，当前阶段实现 `MetadataServiceImpl`（本地实现，直接读写 IGraphStore）。

后续分布式阶段，新增 `RemoteMetadataClient : public IMetadataService`（内部通过 fbthrift 调用远程元数据服务）。调用方（如 `QueryExecutor`）只需在构造时注入不同的实现实例，无需修改任何业务逻辑代码。

```
                    ┌─────────────────────┐
                    │   IMetadataService  │  (抽象接口)
                    └──────────┬──────────┘
                               │
                ┌──────────────┴──────────────┐
                │                             │
                ▼                             ▼
    ┌───────────────────────┐     ┌─────────────────────────┐
    │  MetadataServiceImpl  │     │  RemoteMetadataClient   │
    │  (本地实现)            │     │  (fbthrift 远程调用)     │
    │  直接读写 IGraphStore  │     │  通过 RPC 调用远程服务   │
    └───────────────────────┘     └─────────────────────────┘
```

这与 `architecture.md` 中定义的 `LocalImpl` / `RemoteImpl` 服务组合模式一致——通过接口隔离，切换实现不需要改动调用方。

## 八、目录结构

```
src/metadata_service/
├── metadata_service.hpp       # IMetadataService 接口 + MetadataServiceImpl 声明
├── metadata_service.cpp       # MetadataServiceImpl 实现
└── metadata_codec.hpp         # 序列化/反序列化工具函数
```
