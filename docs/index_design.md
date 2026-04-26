# EuGraph 辅助索引（Secondary Index）实现设计

## 需求概述

为顶点和边的属性添加 B-tree 二级索引，支持：
- 等值查询和范围查询
- 唯一索引（UNIQUE 约束）
- openCypher 语法创建/删除/查询索引
- 查询优化器自动利用索引
- 异步 DDL：索引创建/删除不阻塞用户事务，后台线程串行执行
- 后台回填（backfill）：在已有数据上创建索引时，后台线程填充索引条目
- 崩溃恢复：服务器重启后自动恢复未完成的 DDL 任务
- Thrift RPC 接口

**第一版不包含**：复合索引

### 设计参考

索引创建和删除本质上是 DDL 操作。参考 F1 论文（*Online, Asynchronous Schema Change in F1*）的中间状态机制（delete-only → write-only → public），在单机场景下做如下简化：

| F1 概念 | 单机是否需要 | 原因 |
|---------|------------|------|
| 模式租约 | 不需要 | 单进程，不存在"不同服务器使用不同 schema" |
| 两版本限制 | 不需要 | 同上 |
| 写隔离（Write Fencing） | 不需要 | 同上 |
| 中间状态（write-only, delete-only） | **需要** | 保证回填期间写入路径正确维护索引 |
| 后台回填 | **需要** | 需要为已有数据填充索引条目 |
| 回填幂等性 | **需要** | 回填过程中并发写入可能先于回填完成 |

### 核心原则

- **DDL 不阻塞用户事务**：读事务无任何限制；写事务不阻塞，但根据索引状态有不同的索引维护行为
- **索引元数据即 DDL 任务状态**：不需要单独的 DDL 任务持久化，索引的 `state` 字段已持久化在元数据中
- **事务级 Schema 绑定**：每个事务在开始时加载 schema 快照，整个事务期间（读取路径和写入路径）使用同一份 schema，DDL 状态变更不影响运行中的事务

---

## 一、索引状态定义

### 1.1 IndexState 枚举

```cpp
enum class IndexState : uint8_t {
    WRITE_ONLY   = 0,  // 创建中：写入路径维护索引，查询不可用
    PUBLIC       = 1,  // 已就绪：完全可用
    DELETE_ONLY  = 2,  // 删除中：写入路径不维护，仅清理残留条目
    ERROR        = 3,  // 异常：唯一索引回填时发现重复值
};
```

### 1.2 IndexDef 扩展

**文件**: `src/common/types/graph_types.hpp`

```cpp
struct IndexDef {
    std::string name;
    std::vector<uint16_t> prop_ids;
    bool unique = false;
    IndexState state = IndexState::WRITE_ONLY;  // 新增
};
```

### 1.3 状态转换图

```
                        CREATE INDEX
                            │
                            ▼
                       ┌──────────┐
                       │WRITE_ONLY│ ← 写入路径维护索引，查询不可用
                       └────┬─────┘
                            │ 后台回填完成
                   ┌────────┴────────┐
                   ▼                 ▼
              ┌─────────┐      ┌─────────┐
              │  PUBLIC │      │  ERROR  │ ← 唯一索引回填发现重复值
              └────┬────┘      └─────────┘
                   │               │
        DROP INDEX │               │ DROP INDEX
                   ▼               ▼
              ┌─────────────┐  （直接删除元数据）
              │ DELETE_ONLY │ ← 写入路径不维护，仅清理残留条目
              └──────┬──────┘
                     │ 后台清理完成
                     ▼
                （删除元数据）
```

### 1.4 用户事务行为

DDL 期间用户事务无阻塞：

| | 读事务 | 写事务 |
|---|--------|--------|
| 是否阻塞 | **不阻塞** | **不阻塞** |
| 行为变化 | 无，全表扫描 | 根据索引 state 有不同的索引维护行为 |

写路径按索引 state 区分：

| 写操作 | WRITE_ONLY | PUBLIC | DELETE_ONLY | ERROR |
|--------|-----------|--------|-------------|-------|
| insertVertex/Edge | 写入索引条目 | 写入索引条目 | 不维护 | 不维护 |
| deleteVertex/Edge | 删除索引条目 | 删除索引条目 | 删除索引条目 | 删除索引条目 |
| updateProperty | 删旧+写新 | 删旧+写新 | 仅删旧 | 仅删旧 |
| 唯一性检查 | 检查（新数据） | 检查 | 不检查 | 不检查 |

### 1.5 Schema 版本一致性

#### 1.5.1 问题背景

参考 F1 论文（*Online, Asynchronous Schema Change in F1*）的分布式场景，不同服务器可能同时使用不同 schema 版本操作同一份数据。F1 通过两版本约束、schema 租约、写隔离等机制保证安全性。

EuGraph 是单进程系统，不存在"不同服务器使用不同 schema"的问题。但仍有以下需要考虑的场景：

#### 1.5.2 单机场景下的 Schema 一致性问题

用户事务执行期间，DDL Worker 可能修改索引状态（如 WRITE_ONLY → PUBLIC、PUBLIC → DELETE_ONLY）。如果事务内的不同操作看到不同的 schema 版本，可能导致行为不一致。

**只读事务**：安全。WT snapshot isolation 保证事务看到一致的数据快照；读取路径仅使用 PUBLIC 索引（IndexScan），物理计划在事务开始时一次性构建，不随 schema 变化。

**读写混合事务**：需要保证读取路径（物理计划）和写入路径（索引维护）使用同一份 schema。否则可能出现：

```
1. 事务 T 开始，加载 schema → 索引 I 是 PUBLIC
2. 物理计划使用 IndexScan on I
3. DDL 发生，索引 I 状态变为 DELETE_ONLY
4. T 执行写入 → 若写入路径看到 DELETE_ONLY → 不维护索引
5. T 提交后，T 写入的数据没有索引条目
```

虽然在当前火山模型中，IndexScan 是源算子（管道头部执行），CreateNode/CreateEdge 是独立写算子，不会出现"先写再通过索引读回来"的情况。但绑定 schema 版本能从根本上消除这类风险，实现更简洁。

#### 1.5.3 设计方案：事务级 Schema 绑定

每个事务在开始时加载 schema 快照，读取路径和写入路径共用同一份 schema：

```
事务 T 执行流程:
  1. 从 meta store 加载 schema → S_txn（事务级快照）
  2. 构建 PlanContext（读取路径使用 S_txn）
  3. 写入路径使用 S_txn（而非全局 schema 指针）
  4. 提交/回滚

DDL Worker:
  1. 修改 meta store 中的索引状态（持久化）
  2. 更新全局 schema（使用 shared_ptr 原子替换）
  3. 不影响已在运行的事务（它们持有旧 schema 的 shared_ptr）
  4. 新事务开始时自然加载到最新 schema
```

安全性论证：

| 场景 | 安全性 | 原因 |
|------|--------|------|
| 事务持有旧 schema，DDL 升级索引到 PUBLIC | 安全 | 旧 schema 中索引是 WRITE_ONLY，写入路径维护索引（与 PUBLIC 行为一致）。缺失的条目由回填补齐 |
| 事务持有旧 schema，DDL 降级索引到 DELETE_ONLY | 安全 | 旧 schema 中索引是 PUBLIC，写入路径维护索引。DDL Worker 在确认无旧事务引用后清理 |
| 只读事务，索引状态变化 | 安全 | WT snapshot isolation 保证数据一致性，物理计划不使用非 PUBLIC 索引 |

#### 1.5.4 DROP INDEX 的物理删表安全性

当 DROP INDEX 完成（元数据删除、索引条目清理完毕）后，需要物理删除 WT 索引表。但可能仍有事务持有旧 schema，其物理计划中包含对该索引表的引用（IndexScan cursor）。

直接删表会导致运行中的查询报错。采用**延迟物理删除**方案：

```
DROP INDEX 完整流程:
  1. 更新元数据 state = DELETE_ONLY
  2. DDL Worker 清理所有索引条目（dropAllIndexEntries）
  3. 删除索引元数据（从 LabelDef.indexes 移除 + 删除 M|index:{name}）
  4. 将索引表名记录到"待删除列表"（持久化在 meta store 中）
  5. 不立即物理删表

延迟清理（两种触发方式，任选其一）:
  A. 后台定期检查：扫描待删除列表，检查是否有活跃事务引用该索引表，
     若无则物理删表并从列表中移除
  B. 重启时清理：服务器启动时扫描待删除列表，此时无活跃事务，
     直接物理删表（推荐，实现最简单）
```

推荐方案 B（重启时清理），原因：
- 实现简单，无需跟踪活跃事务的 schema 引用
- DROP INDEX 本身是低频操作，残留的空 WT 表占用资源极少
- 重启时在 DDL 恢复流程中顺便处理，逻辑统一

---

## 二、实现阶段

### 阶段 1：类型与 Schema 扩展

#### 1.1 扩展 EdgeLabelDef 添加索引字段

**文件**: `src/common/types/graph_types.hpp`

`LabelDef` 已有 `IndexDef` 和 `indexes` 字段。`EdgeLabelDef` 缺少 `indexes`：

```cpp
struct EdgeLabelDef {
    EdgeLabelId id = INVALID_EDGE_LABEL_ID;
    EdgeLabelName name;
    std::vector<PropertyDef> properties;
    bool directed = true;
    std::vector<LabelDef::IndexDef> indexes;  // 新增
};
```

#### 1.2 MetadataCodec 序列化/反序列化 IndexDef

**文件**: `src/storage/meta/meta_codec.cpp`

当前状态：
- `encodeLabelDef()` 第 188-189 行：硬编码写入 `count=0`
- `decodeLabelDef()` 第 203-205 行：跳过 indexes

修改：
- `encodeLabelDef()`: 遍历 `def.indexes` 序列化每个 IndexDef（name: string, prop_ids count: u16 + u16 values, unique: u8, state: u8）
- `decodeLabelDef()`: 反序列化 IndexDef 到 `def.indexes`
- `encodeEdgeLabelDef()`: 在 `directed` 字段后添加 indexes 序列化
- `decodeEdgeLabelDef()`: 添加 indexes 反序列化

#### 1.3 GraphSchema 添加索引查找辅助方法

**文件**: `src/storage/graph_schema.hpp`

添加便利方法：
```cpp
std::optional<LabelDef::IndexDef> findIndexByName(const std::string& name) const;
std::optional<LabelDef::IndexDef> findVertexIndex(LabelId label_id, uint16_t prop_id) const;
std::optional<LabelDef::IndexDef> findEdgeIndex(EdgeLabelId label_id, uint16_t prop_id) const;
```

#### 1.4 索引表名常量

**文件**: `src/common/types/constants.hpp`

```cpp
inline std::string vidxTable(LabelId label_id, uint16_t prop_id) {
    return "table:vidx_" + std::to_string(label_id) + "_" + std::to_string(prop_id);
}
inline std::string eidxTable(EdgeLabelId label_id, uint16_t prop_id) {
    return "table:eidx_" + std::to_string(label_id) + "_" + std::to_string(prop_id);
}
```

---

### 阶段 2：索引键编码与存储操作

#### 2.1 索引键编码器 IndexKeyCodec

**新文件**: `src/storage/kv/index_key_codec.hpp`, `src/storage/kv/index_key_codec.cpp`

索引键格式：`{sortable_property_value}{entity_id:u64BE}`

可排序属性值编码（type prefix + value）：

| 类型 | 编码 |
|------|------|
| null | 1 字节: `0xFF`（排最前） |
| bool | 1 字节: `0x00` / `0x01` |
| int64 | 9 字节: `0x00` 前缀 + big-endian 符号翻转（`val ^ 0x8000000000000000`） |
| double | 9 字节: `0x01` 前缀 + IEEE 754 符号翻转 |
| string | 可变: `0x02` 前缀 + 原始字节（v1 仅支持 ASCII 排序） |

```cpp
class IndexKeyCodec {
public:
    static std::string encodeIndexKey(const PropertyValue& value, uint64_t entity_id);
    static uint64_t decodeEntityId(std::string_view key);
    static std::string encodeSortableValue(const PropertyValue& value);
    static std::string encodeEqualityPrefix(const PropertyValue& value);
};
```

#### 2.2 ISyncGraphDataStore 接口扩展

**文件**: `src/storage/data/i_sync_graph_data_store.hpp`

新增方法：
```cpp
// Index DDL
virtual bool createIndex(const std::string& table_name) = 0;
virtual bool dropIndex(const std::string& table_name) = 0;

// Index entry operations
virtual bool insertIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id) = 0;
virtual bool deleteIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id) = 0;
virtual bool checkUniqueConstraint(const std::string& table, const PropertyValue& value) = 0;

// Index scan
virtual void scanIndexEquality(GraphTxnHandle txn, const std::string& table,
                                const PropertyValue& value,
                                const std::function<bool(uint64_t)>& callback) = 0;
virtual void scanIndexRange(GraphTxnHandle txn, const std::string& table,
                             const std::optional<PropertyValue>& start,
                             const std::optional<PropertyValue>& end,
                             const std::function<bool(uint64_t)>& callback) = 0;

// Index cleanup (for DROP INDEX)
virtual void dropAllIndexEntries(const std::string& table) = 0;
```

#### 2.3 SyncGraphDataStore 实现

**文件**: `src/storage/data/sync_graph_data_store.hpp`, `src/storage/data/sync_graph_data_store.cpp`

- `createIndex()`: 使用 DDL session 创建 WiredTiger 表（`key_format=u,value_format=u`）
- `insertIndexEntry()`: 编码 key → `tablePut`
- `deleteIndexEntry()`: 编码 key → `tableDel`
- `checkUniqueConstraint()`: 用 `encodeEqualityPrefix()` 前缀扫描，找到任何条目即违反唯一性
- `scanIndexEquality()`: 前缀扫描，解码 entity_id
- `scanIndexRange()`: `search_near` + 范围边界检查
- `dropAllIndexEntries()`: 扫描索引表所有条目并删除

#### 2.4 Async 接口扩展

**文件**: `src/storage/data/i_async_graph_data_store.hpp`, `src/storage/data/async_graph_data_store.hpp`

添加 async 版本的索引方法，通过 `IoScheduler::dispatch()` 调度到 IO 线程：

```cpp
// Index DDL
folly::coro::Task<bool> createIndex(const std::string& table_name) override;
folly::coro::Task<bool> dropIndex(const std::string& table_name) override;

// Index scan
folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndex(
    LabelId label_id, uint16_t prop_id, const PropertyValue& value) override;
folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndexRange(
    LabelId label_id, uint16_t prop_id,
    const std::optional<PropertyValue>& start,
    const std::optional<PropertyValue>& end) override;
```

---

### 阶段 3：索引维护（写入路径）

#### 3.1 当前实现：计算层写入路径维护

当前版本在计算层（`CreateNodePhysicalOp`）而非存储层维护索引条目。`PhysicalPlanner` 在构建 `CreateNodePhysicalOp` 时传入 `PlanContext::label_defs` 指针，`execute()` 在插入顶点后遍历索引定义写入索引条目。

**文件**: `src/compute_service/physical_plan/physical_operator.hpp`

`CreateNodePhysicalOp` 构造函数新增 `label_defs` 指针参数：

```cpp
CreateNodePhysicalOp(std::string variable, std::vector<LabelId> label_ids,
                     std::vector<std::pair<LabelId, Properties>> label_props,
                     IAsyncGraphDataStore& store, VertexId assigned_vid,
                     std::unique_ptr<PhysicalOperator> child = nullptr,
                     const std::unordered_map<LabelId, LabelDef>* label_defs = nullptr);
```

**文件**: `src/compute_service/physical_plan/physical_operator.cpp`

`execute()` 在 `insertVertex` 成功后：

```cpp
if (ok && label_defs_) {
    for (const auto& [label_id, props] : label_props_) {
        auto def_it = label_defs_->find(label_id);
        if (def_it == label_defs_->end()) continue;
        for (const auto& idx : def_it->second.indexes) {
            if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC) continue;
            for (auto prop_id : idx.prop_ids) {
                if (prop_id < props.size() && props[prop_id].has_value()) {
                    co_await store_.insertIndexEntry(vidxTable(label_id, prop_id),
                                                     props[prop_id].value(), assigned_vid_);
                }
            }
        }
    }
}
```

**文件**: `src/compute_service/physical_plan/physical_planner.cpp`

`PhysicalPlanner` 在构建 `CreateNodePhysicalOp` 时传入 `&ctx.label_defs`。

#### 3.2 当前局限

当前仅实现了 `insertVertex` 的索引维护，以下操作尚未维护索引条目：

- `deleteVertex` / `deleteEdge`：删除时未清理索引条目
- `putVertexProperty` / `putEdgeProperty`：更新属性时未删除旧索引条目 + 写入新索引条目
- `deleteVertexProperty` / `deleteEdgeProperty`：删除属性时未清理索引条目
- 唯一性约束检查（`checkUniqueConstraint`）未在写入路径中调用
- Edge 写入路径（`CreateEdgePhysicalOp`）未维护边属性索引

#### 3.3 未来目标：存储层统一维护（事务级 Schema 绑定）

最终目标是下沉到存储层（`SyncGraphDataStore`），通过事务级 schema 快照统一维护所有写入路径的索引：

每个事务在开始时获取 schema 快照，写入路径通过事务级别的 schema 引用访问索引信息，而非使用全局 schema 指针。

全局 schema 通过 `std::shared_ptr<GraphSchema>` 管理，DDL Worker 更新索引状态后通过 `std::atomic_store` 原子替换。新事务启动时加载最新版本，运行中的事务持有旧版本的 shared_ptr 不受影响。

**insertVertex/insertEdge** — 在写入属性后：
- 遍历 `state == WRITE_ONLY || state == PUBLIC` 的索引
- 对每个索引的 prop_id，如果 `props[prop_id]` 有值：
  - 若是 UNIQUE 索引，先调用 `checkUniqueConstraint()`，失败则返回错误
  - 调用 `insertIndexEntry(table, props[prop_id], entity_id)`

**deleteVertex/deleteEdge** — 在删除属性前：
- 遍历 `state == WRITE_ONLY || state == PUBLIC || state == DELETE_ONLY` 的索引
- 先读取被索引的属性值
- 调用 `deleteIndexEntry(table, old_value, entity_id)`

**putVertexProperty/putEdgeProperty** — 在写入新属性值时：
- 遍历 `state == WRITE_ONLY || state == PUBLIC` 的索引（若该 prop_id 被索引）：
  - 读取旧值 → `deleteIndexEntry(table, old_value, entity_id)`
  - 写入新值 → `insertIndexEntry(table, new_value, entity_id)`
  - 若是 UNIQUE 索引，先 `checkUniqueConstraint(new_value)`

**deleteVertexProperty/deleteEdgeProperty** — 在删除属性值时：
- 遍历 `state == WRITE_ONLY || state == PUBLIC` 的索引（若该 prop_id 被索引）：
  - 读取旧值 → `deleteIndexEntry(table, old_value, entity_id)`

---

### 阶段 4：索引 DDL 解析与异步执行

#### 4.1 索引 DDL 解析器

**新文件**: `src/compute_service/parser/index_ddl_parser.hpp`, `src/compute_service/parser/index_ddl_parser.cpp`

轻量级字符串解析器，在 ANTLR 解析前拦截索引 DDL：

```cpp
struct IndexDdlStatement {
    enum Type { CREATE_VERTEX_INDEX, CREATE_EDGE_INDEX, DROP_INDEX, SHOW_INDEXES };
    Type type;
    bool unique = false;
    std::string index_name;
    std::string label_name;
    std::string property_name;
};

class IndexDdlParser {
public:
    static std::optional<IndexDdlStatement> tryParse(const std::string& query);
};
```

支持的语法：
```
CREATE INDEX idx_name FOR (n:Label) ON (n.prop)
CREATE UNIQUE INDEX idx_name FOR (n:Label) ON (n.prop)
CREATE INDEX idx_name FOR ()-[r:TYPE]-() ON (r.prop)
CREATE UNIQUE INDEX idx_name FOR ()-[r:TYPE]-() ON (r.prop)
DROP INDEX idx_name
SHOW INDEXES
SHOW INDEX idx_name
```

#### 4.2 Meta Store 索引方法

**文件**: `src/storage/meta/i_async_graph_meta_store.hpp`, `src/storage/meta/async_graph_meta_store.hpp/.cpp`

新增方法：
```cpp
// 创建索引元数据（state=WRITE_ONLY）
folly::coro::Task<bool> createVertexIndex(const std::string& name,
    const std::string& label_name, const std::string& prop_name, bool unique);
folly::coro::Task<bool> createEdgeIndex(const std::string& name,
    const std::string& edge_label_name, const std::string& prop_name, bool unique);

// 更新索引状态
folly::coro::Task<bool> updateIndexState(const std::string& name, IndexState new_state);

// 删除索引（清理元数据）
folly::coro::Task<bool> dropIndex(const std::string& name);

// 查询索引信息
struct IndexInfo {
    std::string name;
    std::string label_name;
    std::string property_name;
    bool unique;
    bool is_edge;
    IndexState state;
    std::string error_message;  // 仅 state==ERROR 时有值
};
folly::coro::Task<std::vector<IndexInfo>> listIndexes();
folly::coro::Task<std::optional<IndexInfo>> getIndex(const std::string& name);
```

实现逻辑：
1. 查找 label_name → LabelId，prop_name → prop_id
2. 检查无同名索引、无同 label+prop 的重复索引
3. 添加 IndexDef 到 LabelDef.indexes
4. 持久化更新后的 LabelDef 到 `M|label:{name}`
5. 写入 `M|index:{name}` → `{label_id:u16}{prop_id:u16}{is_edge:u8}{state:u8}`（快速索引名查找）
6. 在 `open()` 时扫描 `M|index:*` 加载索引元数据

#### 4.3 当前实现：同步回填

当前版本未实现 DdlWorker 后台线程，而是在 `QueryExecutor::handleIndexDdl()` 中同步执行回填。用户执行 `CREATE INDEX` 时，在当前请求内完成所有工作后返回。

**文件**: `src/compute_service/executor/query_executor.cpp`

##### CREATE INDEX 同步执行流程

```
handleIndexDdl(CREATE_VERTEX_INDEX):
  1. 参数校验：label 存在性、property 存在性
  2. 创建索引元数据（state=WRITE_ONLY）
  3. 创建索引存储表（WT 表）
  4. 同步回填：
     a. 开启事务
     b. scanVerticesByLabel(label_id) 扫描该 label 下所有顶点
     c. 对每个顶点：getVertexProperties → 若属性有值 → insertIndexEntry
     d. 提交事务
  5. 更新索引状态为 PUBLIC
  6. 返回"Index created: {name}"
```

回填代码：

```cpp
GraphTxnHandle txn = co_await async_data_.beginTran();
async_data_.setTransaction(txn);

auto gen = async_data_.scanVerticesByLabel(label_def->id);
while (auto batch = co_await gen.next()) {
    for (auto vid : *batch) {
        auto props = co_await async_data_.getVertexProperties(vid, label_def->id);
        if (props.has_value() && prop_id < props->size() && (*props)[prop_id].has_value()) {
            co_await async_data_.insertIndexEntry(table, (*props)[prop_id].value(), vid);
        }
    }
}

co_await async_data_.commitTran(txn);
```

##### DROP INDEX 同步执行流程

```
handleIndexDdl(DROP_INDEX):
  1. 删除索引元数据（从 LabelDef.indexes 移除 + 删除 M|index:{name}）
  2. 返回"Index dropped: {name}"
```

注意：当前 DROP INDEX 未清理已有索引条目（无 `dropAllIndexEntries` 调用），也未物理删除 WT 索引表。

##### SHOW INDEXES

直接查询 `async_meta_.listIndexes()` 返回所有索引信息（含状态）。

#### 4.4 未来目标：DdlWorker 后台异步执行

**新文件**: `src/storage/ddl/ddl_worker.hpp`, `src/storage/ddl/ddl_worker.cpp`

##### DDL 任务定义

```cpp
struct DdlTask {
    enum Type { CREATE_INDEX, DROP_INDEX };
    Type type;
    std::string index_name;
    // CREATE 特有字段:
    std::string label_name;
    std::string property_name;
    bool unique = false;
    bool is_edge = false;
};
```

##### DDL 后台线程

用户可并发提交 DDL，但后台线程串行执行（同一时刻只有一个 DDL 在执行）：

```
用户线程 (多个)          DDL 队列 (线程安全)         后台 DDL 线程 (单个)
─────────────────     ─────────────────────     ────────────────────
CREATE INDEX idx1 ──→  [idx1(CREATE), ...] ──→  取出 idx1, 执行回填
CREATE INDEX idx2 ──→  [idx1, idx2(CREATE)]     idx1 完成, 取出 idx2
DROP INDEX idx3   ──→  [idx1, idx2, idx3(DROP)]  ...
```

```cpp
class DdlWorker {
public:
    void start();   // 启动后台线程
    void stop();    // 停止
    void submit(DdlTask task);  // 提交 DDL（用户线程调用，线程安全）

private:
    void run();     // 主循环：从队列取任务，串行执行
    void executeCreateIndex(const DdlTask& task);
    void executeDropIndex(const DdlTask& task);

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<DdlTask> queue_;
    bool stopped_ = false;
};
```

##### CREATE INDEX 后台执行流程

```
executeCreateIndex(task):
  1. 通过 I|{label_id}|* 正向索引扫描该 label 下所有 VertexId
  2. 对每个 VertexId:
     a. 读取索引属性的值
     b. 调用 insertIndexEntry()
        - 若条目已存在（并发写入先写了）→ 跳过（幂等）
        - 若是唯一索引且发现重复值 → 标记 state=ERROR，停止回填，记录错误信息
  3. 回填成功 → 更新元数据 state = PUBLIC
```

##### DROP INDEX 后台执行流程

```
executeDropIndex(task):
  1. 更新元数据 state = DELETE_ONLY
  2. 扫描索引表所有条目，逐条删除（dropAllIndexEntries）
  3. 删除索引元数据（从 LabelDef.indexes 移除 + 删除 M|index:{name}）
  4. 将索引表名记录到"待删除列表"（持久化到 meta store）
  5. 不立即物理删表（可能仍有运行中事务持有旧 schema 引用该索引）

重启时清理（参见 4.5 节）:
  - 扫描待删除列表，物理删除 WT 索引表
  - 从待删除列表中移除已删除的表
```

#### 4.5 未来目标：服务器重启恢复

启动时自动恢复未完成的 DDL 任务：

```
服务器启动
  → 加载 schema（从元数据存储读取所有 LabelDef/EdgeLabelDef）
  → 扫描所有 indexes，检查 state
  → 发现 WRITE_ONLY  → 放入 DDL 队列（重新回填）
  → 发现 DELETE_ONLY  → 放入 DDL 队列（重新清理）
  → 发现 ERROR        → 保持不变（数据未变，重试也会失败）
  → 发现 PUBLIC        → 正常加载，无需操作
  → 扫描待删除列表，物理删除 WT 索引表（此时无活跃事务，安全删表）
  → 启动 DdlWorker 后台线程，串行执行队列中的任务
```

恢复安全性由幂等性保证：
- **回填**（WRITE_ONLY）：`insertIndexEntry()` 遇到已存在的条目则跳过，即使上次回填进行到一半崩溃，重启后从头扫描也能安全恢复
- **清理**（DELETE_ONLY）：删除操作天然幂等，删除不存在的条目直接跳过
- **ERROR**：不自动重试，用户看到后需先 `DROP INDEX` 修复数据，再重新 `CREATE INDEX`

#### 4.6 QueryExecutor 集成

**文件**: `src/compute_service/executor/query_executor.cpp`

在 `executeAsync()` 中，在 Cypher 解析前调用 `IndexDdlParser::tryParse()`：

**当前实现**：
- 若匹配到 `CREATE INDEX` → 参数校验 + 写入元数据（WRITE_ONLY）+ 创建存储表 + 同步回填已有数据 + 设为 PUBLIC → 返回"Index created"
- 若匹配到 `DROP INDEX` → 校验存在性 + 删除元数据 → 返回"Index dropped"
- 若匹配到 `SHOW INDEXES` / `SHOW INDEX` → 查询元数据，返回索引信息（含状态）
- 若不匹配 → 走原有 Cypher 解析流程

**未来目标（DdlWorker 版本）**：
- 若匹配到 `CREATE INDEX` → 参数校验 + 写入元数据（state=WRITE_ONLY）+ 创建存储表 + 提交 DdlTask → 立即返回"索引创建中"
- 若匹配到 `DROP INDEX` → 校验存在性 + 更新 state=DELETE_ONLY + 提交 DdlTask → 立即返回"索引删除中"

---

### 阶段 5：查询优化——IndexScan 物理算子

#### 5.1 IndexScanPhysicalOp

**文件**: `src/compute_service/physical_plan/physical_operator.hpp`

```cpp
class IndexScanPhysicalOp : public PhysicalOperator {
    std::string variable_;
    LabelId label_id_;
    uint16_t prop_id_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
    enum class ScanMode { EQUALITY, RANGE } mode_;
    PropertyValue search_value_;
    std::optional<PropertyValue> range_end_;
};
```

**文件**: `src/compute_service/physical_plan/physical_operator.cpp`

`execute()` 实现：
1. EQUALITY 模式：调用 `store_.scanVerticesByIndex(label_id_, prop_id_, search_value_)`
2. RANGE 模式：调用 `store_.scanVerticesByIndexRange(label_id_, prop_id_, search_value_, range_end_)`
3. 对每个返回的 VertexId，加载属性构造 VertexValue，yield RowBatch

#### 5.2 物理计划器优化

**文件**: `src/compute_service/physical_plan/physical_planner.cpp`

在 `planOperator()` 中处理 `FilterOp`（子节点为 `LabelScanOp`）时：

1. 分析谓词表达式，提取 `PropertyAccess(Variable, prop) op Literal` 模式
2. 检查 `PlanContext::label_defs[label_id].indexes` 中是否存在该属性的索引 **且 `state == PUBLIC`**
3. 仅使用 `state == PUBLIC` 的索引进行 IndexScan
4. 若找到匹配索引：
   - `EQ` → `IndexScanPhysicalOp(..., EQUALITY, value)`
   - `GT/GTE` → `IndexScanPhysicalOp(..., RANGE, value, nullopt)`
   - `LT/LTE` → `IndexScanPhysicalOp(..., RANGE, nullopt, value)`
   - `GT/GTE AND LT/LTE`（同一属性）→ `IndexScanPhysicalOp(..., RANGE, start, end)`

辅助函数 `tryExtractIndexPredicate()`：
```cpp
struct IndexPredicate {
    std::string variable;
    std::string property;
    cypher::BinaryOperator op;
    cypher::Expression value;
};
std::optional<IndexPredicate> tryExtractIndexPredicate(const cypher::Expression& expr);
```

---

### 阶段 6：Thrift RPC 接口

**文件**: `proto/eugraph.thrift`（或对应位置）

```thrift
enum IndexState {
    WRITE_ONLY = 0,
    PUBLIC = 1,
    DELETE_ONLY = 2,
    ERROR = 3,
}

struct IndexInfo {
    1: string name
    2: string label_name
    3: string property_name
    4: bool unique
    5: bool is_edge_index
    6: IndexState state
    7: optional string error_message
}

service EuGraphService {
    // ... 现有方法 ...
    IndexInfo createIndex(1: string name, 2: string label_name,
                          3: string property_name, 4: bool unique = false,
                          5: bool is_edge = false)
    IndexInfo dropIndex(1: string name)
    list<IndexInfo> listIndexes()
    IndexInfo getIndex(1: string name)
}
```

**文件**: `src/server/eugraph_handler.hpp`, `src/server/eugraph_handler.cpp`

- `co_createIndex`: 参数校验 + 写入元数据（WRITE_ONLY）+ 创建存储表 + 提交 DdlTask → 返回 IndexInfo（含 state）
- `co_dropIndex`: 校验 + 提交 DdlTask → 返回 IndexInfo（含 state）
- `co_listIndexes`: 查询所有索引信息
- `co_getIndex`: 查询单个索引信息

---

### 阶段 7：测试

#### 7.1 单元测试：IndexKeyCodec

**新文件**: `tests/test_index_key_codec.cpp`

- int64 正确排序（负数 < 正数）
- double 正确排序
- string 正确排序（ASCII）
- entity_id 编解码往返
- null 排最前

#### 7.2 单元测试：IndexDdlParser

**新文件**: `tests/test_index_ddl_parser.cpp`

- 解析各种合法的 CREATE INDEX 语法
- 解析 DROP INDEX
- 解析 SHOW INDEXES / SHOW INDEX
- 非索引语句返回 nullopt

#### 7.3 集成测试：索引存储操作

**新文件**: `tests/test_index_store.cpp`

- 创建索引 → 插入顶点 → 验证索引条目存在
- 等值扫描索引 → 验证结果正确
- 范围扫描索引 → 验证结果正确
- 删除顶点 → 验证索引条目被删除
- 更新索引属性 → 验证旧条目删除、新条目添加
- 唯一索引 → 插入重复值 → 验证失败

#### 7.4 端到端测试：索引查询

**新文件**: `tests/test_index_query.cpp`

- 创建标签 → 创建索引 → 插入数据 → `WHERE n.prop = value` → 验证结果正确
- 范围查询 `WHERE n.prop > value`
- 唯一约束违反测试
- 边属性索引测试

#### 7.5 集成测试：DDL 异步执行

**新文件**: `tests/test_index_ddl.cpp`

- CREATE INDEX → 立即返回 → 查询状态为 WRITE_ONLY → 等待 → 状态变为 PUBLIC
- 在已有数据上 CREATE INDEX → 回填完成后查询使用索引 → 结果正确
- 唯一索引回填发现重复值 → 状态变为 ERROR
- DROP INDEX → 立即返回 → 等待 → 索引不存在
- 并发提交多个 DDL → 串行执行完成
- 回填幂等性：回填过程中并发写入 → 回填完成 → 结果一致

#### 7.6 集成测试：崩溃恢复

**新文件**: `tests/test_index_recovery.cpp`

- 创建索引（WRITE_ONLY）→ 强制停止 → 重启 → 自动恢复回填 → 状态变为 PUBLIC
- 删除索引（DELETE_ONLY）→ 强制停止 → 重启 → 自动恢复清理 → 索引不存在
- ERROR 状态 → 重启 → 状态保持 ERROR

---

## 三、实现顺序与当前进度

```
阶段 1（类型/Schema）→ 阶段 2（存储操作）→ 阶段 3（索引维护）
                                         ↘
阶段 4（DDL 解析 + 回填）→ 阶段 5（查询优化）→ 阶段 6（Thrift RPC）
测试与各阶段并行
```

### 已完成
- 阶段 1：类型与 Schema 扩展（IndexState、IndexDef.state、EdgeLabelDef.indexes）
- 阶段 2：索引键编码（IndexKeyCodec）+ 存储接口（ISyncGraphDataStore 索引方法）+ Async 包装
- 阶段 3（简化版）：计算层 insertVertex 索引维护（CreateNodePhysicalOp）
- 阶段 4（简化版）：DDL 解析（IndexDdlParser）+ 同步回填 + QueryExecutor DDL 路由
- 阶段 5：IndexScan 物理算子 + 物理计划器索引优化
- 测试：274 个测试全部通过（含 15 个索引 E2E 测试）

### 待完成
- 阶段 3（完整版）：存储层写入路径统一维护（deleteVertex、putVertexProperty 等的索引维护）
- 阶段 4（完整版）：DdlWorker 后台异步执行 + 崩溃恢复 + 延迟物理删表
- 阶段 6：Thrift RPC 接口
- 测试：DDL 异步执行测试、崩溃恢复测试

---

## 四、文件清单

### 已实现的新建文件
| 文件 | 用途 |
|------|------|
| `src/storage/kv/index_key_codec.hpp` | 索引键编码接口 |
| `src/storage/kv/index_key_codec.cpp` | 索引键编码实现 |
| `src/compute_service/parser/index_ddl_parser.hpp` | DDL 解析器接口 |
| `src/compute_service/parser/index_ddl_parser.cpp` | DDL 解析器实现 |
| `tests/test_index_key_codec.cpp` | 编码单元测试 |
| `tests/test_index_store.cpp` | 索引存储集成测试 |
| `tests/test_index_e2e.cpp` | 端到端测试（含 DDL 路由、IndexScan、回填验证） |

### 待实现的新建文件
| 文件 | 用途 |
|------|------|
| `src/storage/ddl/ddl_worker.hpp` | DDL 后台执行器接口 |
| `src/storage/ddl/ddl_worker.cpp` | DDL 后台执行器实现 |
| `tests/test_index_ddl_parser.cpp` | DDL 解析测试（当前在 test_index_e2e.cpp 中） |
| `tests/test_index_ddl.cpp` | DDL 异步执行测试 |
| `tests/test_index_recovery.cpp` | 崩溃恢复测试 |

### 已修改文件
| 文件 | 修改内容 |
|------|----------|
| `src/common/types/graph_types.hpp` | IndexDef 增加 IndexState state 字段；EdgeLabelDef 添加 indexes 字段 |
| `src/common/types/constants.hpp` | 添加 vidxTable/eidxTable 辅助函数 |
| `src/storage/meta/meta_codec.cpp` | 序列化/反序列化 IndexDef（含 state） |
| `src/storage/data/i_sync_graph_data_store.hpp` | 添加索引相关 virtual 方法 |
| `src/storage/data/sync_graph_data_store.hpp` | 添加索引方法声明 |
| `src/storage/data/sync_graph_data_store.cpp` | 实现索引方法（createIndex/insertIndexEntry/scanIndex* 等） |
| `src/storage/data/i_async_graph_data_store.hpp` | 添加 async 索引方法 |
| `src/storage/data/async_graph_data_store.hpp` | 实现 async 索引方法 |
| `src/storage/meta/i_async_graph_meta_store.hpp` | 添加索引元数据方法 |
| `src/storage/meta/async_graph_meta_store.hpp` | 添加索引元数据方法声明 |
| `src/storage/meta/async_graph_meta_store.cpp` | 实现索引元数据 CRUD + 状态更新 + 加载 |
| `src/compute_service/physical_plan/physical_operator.hpp` | 添加 IndexScanPhysicalOp + CreateNodePhysicalOp 增加 label_defs |
| `src/compute_service/physical_plan/physical_operator.cpp` | 实现 IndexScanPhysicalOp::execute() + CreateNodePhysicalOp 写入索引条目 |
| `src/compute_service/physical_plan/physical_planner.cpp` | 索引感知优化逻辑 + 传入 label_defs 到 CreateNodePhysicalOp |
| `src/compute_service/executor/query_executor.cpp` | DDL 预解析与路由 + 同步回填 |
| `CMakeLists.txt` | 添加新源文件和测试目标 |

### 待修改文件
| 文件 | 修改内容 |
|------|----------|
| `src/storage/data/sync_graph_data_store.cpp` | 写入路径按 state 统一维护索引（deleteVertex/putVertexProperty 等） |
| `src/server/eugraph_handler.hpp` | 添加索引 RPC 方法 |
| `src/server/eugraph_handler.cpp` | 实现索引 RPC 方法 |

---

## 五、验证方法

1. **编译**：`cmake --build build` 确保所有新文件编译通过
2. **单元测试**：
   - `./build/debug/index_codec_tests` — 编码正确性和排序性
3. **存储测试**：`./build/debug/index_store_tests` — 索引 CRUD 和扫描
4. **E2E 测试**：`./build/debug/index_e2e_tests` — DDL 路由、IndexScan、回填验证（15 个测试）
5. **全量测试**：`cd build/debug && ctest` — 274 个测试全部通过
6. **待实现**：
   - `./build/test_index_ddl` — 异步 DDL 执行、状态转换
   - `./build/test_index_recovery` — 崩溃恢复
