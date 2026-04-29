# 元数据服务设计

> [当前实现] 参见 [overview.md](overview.md) 返回文档导航

---

## 一、概述

元数据服务管理 Label/EdgeLabel 定义、索引元数据和全局 ID 分配。通过 `IAsyncGraphMetaStore` 接口暴露，内部维护 `GraphSchema` 内存对象。

---

## 二、接口定义

文件：`src/storage/meta/i_async_graph_meta_store.hpp`

```cpp
class IAsyncGraphMetaStore {
    // 生命周期
    virtual folly::coro::Task<bool> open(ISyncGraphMetaStore& store, IoScheduler& io) = 0;
    virtual folly::coro::Task<void> close() = 0;

    // Label 管理
    virtual folly::coro::Task<LabelId> createLabel(name, properties) = 0;
    virtual folly::coro::Task<optional<LabelId>> getLabelId(name) = 0;
    virtual folly::coro::Task<optional<LabelDef>> getLabelDef(name) = 0;
    virtual folly::coro::Task<vector<LabelDef>> listLabels() = 0;

    // EdgeLabel 管理
    virtual folly::coro::Task<EdgeLabelId> createEdgeLabel(name, properties, directed) = 0;
    virtual folly::coro::Task<vector<EdgeLabelDef>> listEdgeLabels() = 0;

    // ID 分配
    virtual folly::coro::Task<VertexId> nextVertexId() = 0;
    virtual folly::coro::Task<EdgeId> nextEdgeId() = 0;
    virtual folly::coro::Task<vector<VertexId>> nextVertexIdRange(count) = 0;
    virtual folly::coro::Task<vector<EdgeId>> nextEdgeIdRange(count) = 0;

    // 索引管理
    virtual folly::coro::Task<bool> createVertexIndex(name, label_name, prop_name, unique) = 0;
    virtual folly::coro::Task<bool> createEdgeIndex(name, edge_label_name, prop_name, unique) = 0;
    virtual folly::coro::Task<bool> updateIndexState(name, new_state) = 0;
    virtual folly::coro::Task<bool> dropIndex(name) = 0;
    virtual folly::coro::Task<vector<IndexInfo>> listIndexes() = 0;
    virtual folly::coro::Task<optional<IndexInfo>> getIndex(name) = 0;

    // Schema 访问
    virtual const GraphSchema& schema() const = 0;
};
```

---

## 三、GraphSchema

文件：`src/storage/graph_schema.hpp`

```cpp
struct GraphSchema {
    unordered_map<LabelId, LabelDef> labels;
    unordered_map<string, LabelId> label_name_to_id;
    unordered_map<EdgeLabelId, EdgeLabelDef> edge_labels;
    unordered_map<string, EdgeLabelId> edge_label_name_to_id;
    set<LabelId> deleted_labels;        // tombstone
    set<EdgeLabelId> deleted_edge_labels; // tombstone
    LabelId next_label_id = 1;
    EdgeLabelId next_edge_label_id = 1;
    VertexId next_vertex_id = 1;
    EdgeId next_edge_id = 1;
};
```

Tombstone 用于 DROP LABEL 后过滤 L| 反向索引中已删除的标签。

---

## 四、持久化

元数据通过 `ISyncGraphMetaStore` 持久化到独立 WT 连接（`{db}/meta/`），使用 `MetadataCodec` 二进制序列化（非 ASCII 管道分隔）。

### KV 键空间

```
M|label:{name}          → encodeLabelDef(def)       # Label 定义（含 indexes）
M|label_id:{id}         → encodeLabelDef(def)       # Label 反向查找
M|edge_label:{name}     → encodeEdgeLabelDef(def)   # EdgeLabel 定义（含 indexes）
M|edge_label_id:{id}    → encodeEdgeLabelDef(def)   # EdgeLabel 反向查找
M|index:{name}          → {label_id:u16}{prop_id:u16}{is_edge:u8}{state:u8}
M|next_ids              → {next_vertex_id:u64}{next_edge_id:u64}{next_label_id:u16}{next_edge_label_id:u16}
```

`MetadataCodec` 序列化 `LabelDef` 时会遍历 `def.indexes` 写入每个 `IndexDef`（name, prop_ids, unique, state）。

---

## 五、核心流程

### 启动（open）

1. 扫描 `M|label:*` → 反序列化所有 LabelDef → 填充 schema_.labels
2. 扫描 `M|edge_label:*` → 反序列化所有 EdgeLabelDef → 填充 schema_.edge_labels
3. 扫描 `M|index:*` → 加载索引元数据
4. 读取 `M|next_ids` → 恢复 ID 计数器

### createLabel

1. 检查名称是否已存在
2. 分配 LabelId = schema_.next_label_id++
3. 为 properties 分配 prop_id（从 1 开始递增）
4. 持久化 `M|label:{name}` 和 `M|label_id:{id}`
5. `saveNextIds()` 持久化计数器
6. 更新内存 schema
7. 不创建物理数据表（DDL 协调由 handler 负责：先调 meta_.createLabel 持久化元数据，再调 data_.createLabel 创建物理表）

### ID 分配

`nextVertexId()`/`nextEdgeId()` 每次分配一个 ID 并持久化 `M|next_ids`。`nextVertexIdRange(n)`/`nextEdgeIdRange(n)` 批量分配 N 个连续 ID 并一次持久化，用于 loader 批量导入。

### 索引管理

`createVertexIndex`：查找 label_name → LabelId，prop_name → prop_id，添加 IndexDef（state=WRITE_ONLY）到 LabelDef.indexes，持久化。

---

## 六、文件结构

```
src/storage/meta/
  i_sync_graph_meta_store.hpp       # ISyncGraphMetaStore 接口
  sync_graph_meta_store.hpp/cpp     # SyncGraphMetaStore 实现
  i_async_graph_meta_store.hpp      # IAsyncGraphMetaStore 接口
  async_graph_meta_store.hpp/cpp    # AsyncGraphMetaStore 实现
  meta_codec.hpp/cpp                # MetadataCodec 序列化（含 IndexDef 编解码）
```
