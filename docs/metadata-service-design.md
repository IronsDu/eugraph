# 元数据服务设计

## 一、概述

元数据服务负责管理 Label（点类型）、EdgeLabel（关系类型）的定义，以及全局 ID 分配。通过 `IAsyncGraphMetaStore` 接口暴露给计算层和服务层，内部维护 `GraphSchema` 内存对象。

## 二、接口定义

文件：`src/storage/meta/i_async_graph_meta_store.hpp`

```cpp
class IAsyncGraphMetaStore {
public:
    virtual folly::coro::Task<bool> open(ISyncGraphMetaStore& store, IoScheduler& io) = 0;
    virtual folly::coro::Task<void> close() = 0;

    // Label 管理
    virtual folly::coro::Task<LabelId> createLabel(const std::string& name,
                                                   const std::vector<PropertyDef>& properties = {}) = 0;
    virtual folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) = 0;
    virtual folly::coro::Task<std::vector<LabelDef>> listLabels() = 0;

    // EdgeLabel 管理
    virtual folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name, ...) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelId>> getEdgeLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() = 0;

    // ID 分配
    virtual folly::coro::Task<VertexId> nextVertexId() = 0;
    virtual folly::coro::Task<EdgeId> nextEdgeId() = 0;

    // Schema 访问
    virtual const GraphSchema& schema() const = 0;
};
```

## 三、实现类

### AsyncGraphMetaStore

文件：`src/storage/meta/async_graph_meta_store.hpp/cpp`

```cpp
class AsyncGraphMetaStore : public IAsyncGraphMetaStore {
    ISyncGraphMetaStore* store_;
    IoScheduler* io_;
    GraphSchema schema_;
};
```

所有阻塞的 `store_->metadataPut/Get/Scan` 调用通过 `io_->dispatch/dispatchVoid` 调度到 IO 线程池。

### GraphSchema

文件：`src/storage/graph_schema.hpp`

内存 schema 对象，由 `AsyncGraphMetaStore` 维护：

```cpp
struct GraphSchema {
    std::unordered_map<LabelId, LabelDef> labels;
    std::unordered_map<std::string, LabelId> label_name_to_id;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_labels;
    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
    LabelId next_label_id = 1;
    EdgeLabelId next_edge_label_id = 1;
    VertexId next_vertex_id = 1;
    EdgeId next_edge_id = 1;
};
```

## 四、持久化设计

元数据通过 `ISyncGraphMetaStore` 存储到 `M|` 前缀的 KV 中。启动时全量加载到 `GraphSchema`，后续读取走内存，写入时同时更新 `GraphSchema` 和 KV。

### 4.1 KV 编码

```
// Label 定义（按名称索引）
Key:   M|label:{name}
Value: MetadataCodec::encodeLabelDef(def)

// Label 定义（按 ID 索引）
Key:   M|label_id:{id}
Value: MetadataCodec::encodeLabelDef(def)

// EdgeLabel 定义（按名称索引）
Key:   M|edge_label:{name}
Value: MetadataCodec::encodeEdgeLabelDef(def)

// EdgeLabel 定义（按 ID 索引）
Key:   M|edge_label_id:{id}
Value: MetadataCodec::encodeEdgeLabelDef(def)

// ID 计数器
Key:   M|next_ids
Value: {next_vertex_id}|{next_edge_id}|{next_label_id}|{next_edge_label_id}
```

### 4.2 序列化（MetadataCodec）

文件：`src/storage/meta/meta_codec.hpp/cpp`

`MetadataCodec` 提供 `LabelDef`、`EdgeLabelDef`、`PropertyValue` 的二进制序列化/反序列化。采用简单字段顺序拼接，非 protobuf。

## 五、核心流程

### 5.1 启动流程（open）

```
1. 保存 ISyncGraphMetaStore* 和 IoScheduler*
2. co_await io_->dispatchVoid: 扫描 M|label:* → 反序列化到 schema_.labels / label_name_to_id
3. co_await io_->dispatchVoid: 扫描 M|edge_label:* → 反序列化到 schema_.edge_labels / edge_label_name_to_id
4. co_await io_->dispatch: 读取 M|next_ids → 恢复 next_*_id 计数器
```

### 5.2 createLabel 流程

```
1. 检查 schema_.label_name_to_id 是否已存在 → 已存在则返回 INVALID_LABEL_ID
2. 分配 LabelId = schema_.next_label_id++
3. 为 properties 分配 prop_id（从 1 开始递增）
4. co_await io_->dispatchVoid: 序列化 LabelDef，写入 M|label:{name} 和 M|label_id:{id}
5. co_await saveNextIds(): 持久化 M|next_ids
6. 更新内存 schema_.labels / schema_.label_name_to_id
7. 注意：不创建物理数据表，DDL 协调由 handler 负责
```

### 5.3 ID 分配

```
nextVertexId():
  id = schema_.next_vertex_id++
  co_await saveNextIds()
  co_return id
```

每次分配都持久化 `M|next_ids`，确保重启后 ID 不重复。

## 六、目录结构

```
src/storage/meta/
├── i_sync_graph_meta_store.hpp       # ISyncGraphMetaStore 接口
├── sync_graph_meta_store.hpp/cpp     # SyncGraphMetaStore 实现 (继承 WtStoreBase)
├── i_async_graph_meta_store.hpp      # IAsyncGraphMetaStore 接口
├── async_graph_meta_store.hpp/cpp    # AsyncGraphMetaStore 实现
└── meta_codec.hpp/cpp                # MetadataCodec 序列化
```
