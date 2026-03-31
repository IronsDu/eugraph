# EuGraph 图数据库设计方案

## 一、项目概述

- **名称**: EuGraph
- **定位**: 分布式图数据库，支持 GSQL 查询语言
- **语言**: C++20
- **存储引擎**: WiredTiger (KV 存储)
- **协程库**: folly
- **RPC 框架**: fbthrift

## 二、核心特性

| 特性 | 说明 |
|------|------|
| 查询语言 | GSQL (类 SQL 的图查询语言) |
| 事务模型 | MVCC + 2PC (分布式) |
| 隔离级别 | 可重复读 (REPEATABLE_READ) |
| 部署模式 | 单机 → 分布式 |
| 架构模式 | 模块化、服务化 |

## 三、系统架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           EuGraph Process                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌───────────────────┐  ┌───────────────────┐  ┌───────────────────────┐   │
│  │  Metadata Service │  │  Compute Service  │  │   Storage Service     │   │
│  │     (元数据)        │  │     (计算)        │  │      (存储)           │   │
│  └─────────┬─────────┘  └─────────┬─────────┘  └───────────┬───────────┘   │
│            │                       │                        │               │
│            └───────────────────────┼────────────────────────┘               │
│                                    │                                        │
│                        ┌───────────┴───────────┐                            │
│                        │   Service Manager    │                            │
│                        │  (服务配置/启动)      │                            │
│                        └───────────────────────┘                            │
│                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                        Abstract Interfaces                             │  │
│  │  IStorageService / IComputeService / IMetadataService                  │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                    │                                        │
│                   ┌────────────────┴────────────────┐                      │
│                   │                                │                      │
│                   ▼                                ▼                      │
│         ┌──────────────────┐            ┌──────────────────────┐          │
│         │  LocalImpl       │            │  RemoteImpl (RPC)    │          │
│         │  (零开销)         │            │  (fbthrift)          │          │
│         └──────────────────┘            └──────────────────────┘          │
│                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                        WiredTiger KV Store                              │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 四、服务定义

### 4.1 Storage Service (存储服务)

| 职责 | 说明 |
|------|------|
| 图数据存储 | Vertex / Edge 的 CRUD |
| KV 存储封装 | WiredTiger 接口封装 |
| 本地事务 | 单节点 MVCC 事务 |
| 数据持久化 | WAL + Checkpoint |

### 4.2 Compute Service (计算服务)
| 职责 | 说明 |
|------|------|
| GSQL 解析 | 词法/语法分析 |
| 查询规划 | 生成执行计划 |
| 查询优化 | 基于规则/代价优化 |
| 查询执行 | 执行查询计划 |

### 4.3 Metadata Service (元数据服务)
| 职责 | 说明 |
|------|------|
| Schema 管理 | Label/EdgeLabel 类型定义 |
| ID 映射 | Label名称↔ID, EdgeLabel名称↔ID |
| 统计信息 | 数据分布、基数估计 |
| ID 分配 | 全局唯一 ID 生成 |

## 五、服务组合模式

| 模式 | 开启的服务 | 说明 |
|------|-----------|------|
| 纯存储节点 | Storage | 只负责数据存储 |
| 纯计算节点 | Compute | 无状态计算，连接存储节点 |
| 存算一体 | Storage + Compute | 单机部署，本地计算，零 RPC 开销 |
| 全功能节点 | Storage + Compute + Metadata | 单机全功能 / 分布式协调节点 |
| 纯元数据节点 | Metadata | 独立的元数据中心 |

## 六、数据模型

### 6.1 核心概念

```
┌─────────────────────────────────────────────────────────────────┐
│                         Vertex (顶点)                           │
│                                                                 │
│  id: uint64                    ← 系统生成的唯一数字 ID            │
│  labels: Set<LabelId>         ← 多个标签 ID (通过索引维护)        │
│  properties: Map<string, Value>← 属性集合                        │
│                                                                 │
│  主键 (用户定义): 如 email, user_id 等 (全局唯一)               │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                         Edge (边)                               │
│                                                                 │
│  id: uint64                    ← 系统生成的唯一数字 ID            │
│  src_id: uint64                ← 源点 ID (与标签无关)            │
│  dst_id: uint64                ← 目标点 ID (与标签无关)          │
│  label_id: uint16              ← 边类型 ID                       │
│  direction: uint8              ← 方向 (OUT=0x00, IN=0x01)        │
│                                 仅存在于内存对象，不持久化         │
│  properties: Map<string, Value>← 属性集合                        │
└─────────────────────────────────────────────────────────────────┘
```

### 6.2 Label/EdgeLabel ID 映射

```
┌─────────────────────────────────────────────────────────────────┐
│                      Label (点类型) 映射                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Label Name → Label ID (uint16)                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ "Person"     → 1                                        │   │
│  │ "User"       → 2                                        │   │
│  │ "Product"    → 3                                        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  Label ID → Label Name (反向映射)                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                   EdgeLabel (关系类型) 映射                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  EdgeLabel Name → EdgeLabel ID (uint16)                        │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ "follows"    → 1                                        │   │
│  │ "likes"      → 2                                        │   │
│  │ "purchases"  → 3                                        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  EdgeLabel ID → EdgeLabel Name (反向映射)                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## 七、KV 编码设计

### 7.1 Key 前缀定义

```
前缀分配 (单字节):
┌──────────┬────────────────────────────────────┐
│ 前缀     │ 用途                               │
├──────────┼────────────────────────────────────┤
│ 'L' 0x4C │ Label 反向索引 (Vertex → Labels)   │
│ 'I' 0x49 │ Label 正向索引 (Label → Vertex)    │
│ 'P' 0x50 │ 主键索引 (Primary Key)             │
│ 'X' 0x58 │ 属性存储 (Property Storage)        │
│ 'E' 0x45 │ Edge 索引 (邻接查询)               │
│ 'D' 0x44 │ Edge 数据 (完整属性)               │
│ 'M' 0x4D │ 元数据 (Metadata)                  │
│ 'T' 0x54 │ 事务 (Transaction)                 │
│ 'W' 0x57 │ WAL                                │
└──────────┴────────────────────────────────────┘
```

### 7.2 详细编码

#### 标签反向索引 (Vertex → Labels)
```
Key:   L|{vertex_id:uint64 BE}|{label_id:uint16 BE}
Value: (empty)

说明: 记录顶点拥有的标签，每个 vertex-label 组合一条记录

示例:
  L|0x0000000000000001|0x0001 → (empty)  // Alice 是 Person
  L|0x0000000000000001|0x0002 → (empty)  // Alice 是 User
  L|0x0000000000000002|0x0001 → (empty)  // Bob 是 Person

前缀查询:
  L|{vertex_id}|  → 获取顶点的所有标签
```

#### 标签正向索引 (Label → Vertices)
```
Key:   I|{label_id:uint16 BE}|{vertex_id:uint64 BE}
Value: (empty)

说明: 用于高效查询某标签下的所有顶点

示例:
  I|0x0001|0x0000000000000001 → (empty)  // Alice 是 Person
  I|0x0001|0x0000000000000002 → (empty)  // Bob 是 Person
  I|0x0002|0x0000000000000001 → (empty)  // Alice 是 User

前缀查询:
  I|{label_id}|  → 获取该标签下的所有顶点 ID
```

#### 主键索引
```
Key:   P|{pk_value:encoded}
Value: {vertex_id:uint64 BE}

说明: 不包含 label_id，pk_value 是全局唯一的主键值

示例 (假设 Person 的主键是 email，User 的主键是 user_id):
  P|"alice@example.com" → 0x0000000000000001
  P|"user_001"           → 0x0000000000000001
  P|"bob@example.com"   → 0x0000000000000002

注意: 同一个 vertex 可能有多个主键索引项 (属于不同 label)
```

#### 属性存储
```
Key:   X|{label_id:uint16 BE}|{vertex_id:uint64 BE}|{prop_id:uint16 BE}
Value: {encoded_value}

说明:
  - prop_id 在每个 Label 内独立编号
  - Key 中使用 prop_id 而非属性名，字段改名只需修改元数据，无需修改数据存储

  - 支持部分属性查询

  - 同一 label 下的不同顶点可以使用相同的 prop_id（但字段可能不同）

  - 同一顶点可以有多个标签，因此可能有多组属性存储记录

    - X 格式： X|{label_id}|{vertex_id}|{prop_id}， prop_id 在各自 Label 内独立编号

    - 每组属性一条记录，便于部分查询

    - 支持字段改名（只改元数据）

  - 与 Edge 数据共享相同的 prop_id 空间

示例:
  X|0x0001|0x0000000000000001|0x0001 → "Alice"   // Person(id=1) 的顶点1 的属性1 (name)
  X|0x0001|0x0000000000000001|0x0002 → 30        // Person(id=1) 的顶点1 的属性2 (age)
  X|0x0001|0x0000000000000002|0x0001 → "Beijing"   // Person(id=1) 的顶点2 的属性1 (city)

  // 同一顶点1 也可以属于另一个标签，因此有两条属性存储记录
  X|0x0002|0x0000000000000002|0x0001 → "Bob"     // Company(id=2) 的顶点2 的属性1 (name)

  X|0x0002|0x0000000000000002|0x0002 → "Beijing"   // 公司(id=2) 的顶点2 的属性2 (city)

前缀查询:
  X|{label_id}|{vertex_id}|           → 获取某标签下某顶点的所有属性
  X|{label_id}|{vertex_id}|{prop_id}   → 获取某标签下某顶点的指定属性
```

#### Edge 索引 (邻接查询)
```
Key:   E|{vertex_id:uint64 BE}|{direction:uint8}|{edge_label_id:uint16 BE}|{neighbor_id:uint64 BE}|{附加id:uint64 BE}
Value: {edge_id:uint64 BE}

方向编码:
  OUT = 0x00
  IN  = 0x01

说明:
  - 附加id 用于区分两点间同类型的多条边（由业务层决定）
  - edge_id 存储在 value 中，用于获取边的完整信息

示例:
  // 公司A(id=1) 向 公司B(id=2) 开了3张发票
  E|0x0000000000000001|0x00|0x0001|0x0000000000000002|0x0000000000000001 → 100  // 第1张发票
  E|0x0000000000000001|0x00|0x0001|0x0000000000000002|0x0000000000000002 → 101  // 第2张发票
  E|0x0000000000000001|0x00|0x0001|0x0000000000000002|0x0000000000000003 → 102  // 第3张发票

前缀查询能力:
  E|{vertex_id}|OUT                              → 某点所有出边
  E|{vertex_id}|IN                               → 某点所有入边
  E|{vertex_id}|OUT|{label_id}                   → 某点某类型出边
  E|{vertex_id}|OUT|{label_id}|{neighbor_id}     → 某点到某点的某类型所有边
  E|{vertex_id}|OUT|{label_id}|{neighbor_id}|{附加id} → 某点到某点的某类型特定边
```

#### Edge 数据 (完整属性)
```
Key:   D|{edge_id:uint64 BE}
Value: {properties:encoded}

说明:
  - 仅存储边的属性，src_id/dst_id/edge_label_id/direction 可从边索引查询中获取
  - 一条逻辑边（如 A follows B）会产生两条索引记录：
    - E|A|OUT|follows|B|{seq} → edge_id
    - E|B|IN|follows|A|{seq} → edge_id
  - 两条索引指向同一个 edge_id，共享同一份 properties

示例:
  D|0x0000000000000064 → {since: 2020, weight: 0.8}
```

#### 元数据
```
Key:   M|label:{name}
Value: {label_id:uint16}|{properties_def}|{primary_key}|{indexes}

Key:   M|label_id:{id:uint16}
Value: {name}|{properties_def}|{primary_key}|{indexes}

Key:   M|edge_label:{name}
Value: {edge_label_id:uint16}|{properties_def}|{directed:bool}

Key:   M|edge_label_id:{id:uint16}
Value: {name}|{properties_def}|{directed:bool}

Key:   M|next_ids
Value: {next_vertex_id:uint64}|{next_edge_id:uint64}|{next_label_id:uint16}|{next_edge_label_id:uint16}
```

## 八、查询能力总结

```
┌────────────────────────────────────────────────────────────────────────────────┐
│                              查询能力                                          │
├────────────────────────────────────────────────────────────────────────────────┤
│                                                                                │
│  ────────────────── Vertex 查询 ──────────────────                            │
│                                                                                │
│  • 通过 vertex_id 获取属性          X|{label_id}|{vertex_id}|*  (前缀扫描)       │
│  • 通过 vertex_id 获取标签          L|{vertex_id}|*  (前缀扫描)              │
│  • 通过 label_id 扫描顶点          I|{label_id}|*  (前缀扫描)              │
│  • 通过主键获取 vertex_id          P|{pk_value}                               │
│  • 通过属性值扫描顶点              (待设计二级索引)                           │

│                                                                                │
│  ────────────────── Edge 查询 ──────────────────                              │
│                                                                                │
│  • 查询某点所有出边                  E|{vid}|OUT|*                             │
│  • 查询某点所有入边                  E|{vid}|IN|*                              │
│  • 查询某点某类型出边                E|{vid}|OUT|{label_id}|*                 │
│  • 查询某点某类型入边                E|{vid}|IN|{label_id}|*                  │
│  • 查询两点之间某类型所有边        E|{src}|OUT|{label}|{dst}|*                 │

│  • 通过 edge_id 获取边详情           D|{edge_id}                                │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
```

## 九、核心类型定义

```cpp
// src/common/types/graph_types.hpp

#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include <variant>
#include <unordered_map>
#include <set>
#include <vector>

namespace eugraph {

// ==================== ID 类型 ====================
using VertexId = uint64_t;
using EdgeId = uint64_t;
using TransactionId = uint64_t;
using SnapshotId = uint64_t;
using SessionId = uint64_t;

using LabelId = uint16_t;
using EdgeLabelId = uint16_t;

using LabelName = std::string;
using EdgeLabelName = std::string;

// 特殊值
constexpr VertexId INVALID_VERTEX_ID = 0;
constexpr EdgeId INVALID_EDGE_ID = 0;
constexpr LabelId INVALID_LABEL_ID = 0;
constexpr EdgeLabelId INVALID_EDGE_LABEL_ID = 0;

// ==================== Label ID 集合 ====================
using LabelIdSet = std::set<LabelId>;

// ==================== 属性值类型 ====================
using PropertyValue = std::variant<
    std::monostate,
    bool,
    int64_t,
    double,
    std::string,
    std::vector<int64_t>,
    std::vector<double>,
    std::vector<std::string>
>;

using Properties = std::unordered_map<std::string, PropertyValue>;

// ==================== 顶点 ====================
struct Vertex {
    VertexId id;
    Properties properties;
};

// ==================== 边 ====================
struct Edge {
    EdgeId id;
    VertexId src_id;
    VertexId dst_id;
    EdgeLabelId label_id;
    Direction direction;    // 仅内存对象，不持久化（从索引 key 解析）
    Properties properties;
};

// ==================== 方向 ====================
enum class Direction : uint8_t {
    OUT = 0x00,
    IN  = 0x01,
    BOTH = 0x02
};

// ==================== 隔离级别 ====================
enum class IsolationLevel {
    READ_UNCOMMITTED,
    READ_COMMITTED,
    REPEATABLE_READ,
    SERIALIZABLE
};

// ==================== 属性类型 ====================
enum class PropertyType {
    BOOL,
    INT64,
    DOUBLE,
    STRING,
    INT64_ARRAY,
    DOUBLE_ARRAY,
    STRING_ARRAY
};

struct PropertyDef {
    std::string name;
    PropertyType type;
    bool required = false;
    std::optional<PropertyValue> default_value;
};

// ==================== 标签定义 ====================
struct LabelDef {
    LabelId id = INVALID_LABEL_ID;
    LabelName name;
    std::vector<PropertyDef> properties;
    std::optional<std::string> primary_key;

    struct IndexDef {
        std::string name;
        std::vector<std::string> properties;
        bool unique = false;
    };
    std::vector<IndexDef> indexes;
};

// ==================== 边标签定义 ====================
struct EdgeLabelDef {
    EdgeLabelId id = INVALID_EDGE_LABEL_ID;
    EdgeLabelName name;
    std::vector<PropertyDef> properties;
    bool directed = true;
};

} // namespace eugraph
```

## 十、抽象接口设计

### 10.1 存储服务接口

```cpp
// src/common/interface/storage_interface.hpp

#pragma once

#include "common/types/graph_types.hpp"
#include "common/types/error.hpp"

#include <folly/coro/Task.h>
#include <folly/coro/AsyncGenerator.h>
#include <folly/Expected.h>
#include <folly/span.h>

namespace eugraph {

template<typename T>
using Result = folly::Expected<T, Error>;

// 图数据操作接口
class IGraphOperations {
public:
    virtual ~IGraphOperations() = default;

    // ==================== Vertex 操作 ====================

    virtual folly::coro::Task<Result<Vertex>> getVertex(
        TransactionId txn_id,
        VertexId vertex_id
    ) = 0;

    virtual folly::coro::Task<Result<VertexId>> insertVertex(
        TransactionId txn_id,
        const LabelIdSet& label_ids,
        const Properties& properties
    ) = 0;

    virtual folly::coro::Task<Result<void>> updateVertexProperties(
        TransactionId txn_id,
        VertexId vertex_id,
        const Properties& properties,
        bool merge = true
    ) = 0;

    virtual folly::coro::Task<Result<void>> deleteVertex(
        TransactionId txn_id,
        VertexId vertex_id
    ) = 0;

    // ==================== Label 操作 ====================

    virtual folly::coro::Task<Result<LabelIdSet>> getVertexLabels(
        TransactionId txn_id,
        VertexId vertex_id
    ) = 0;

    virtual folly::coro::Task<Result<void>> addLabelToVertex(
        TransactionId txn_id,
        VertexId vertex_id,
        LabelId label_id
    ) = 0;

    virtual folly::coro::Task<Result<void>> removeLabelFromVertex(
        TransactionId txn_id,
        VertexId vertex_id,
        LabelId label_id
    ) = 0;

    // ==================== 主键查询 ====================

    virtual folly::coro::Task<Result<VertexId>> getVertexIdByPrimaryKey(
        const PropertyValue& pk_value
    ) = 0;

    virtual folly::coro::Task<Result<Vertex>> getVertexByPrimaryKey(
        const PropertyValue& pk_value
    ) = 0;

    // ==================== 标签索引查询 ====================

    virtual folly::coro::AsyncGenerator<Result<VertexId>> getVertexIdsByLabel(
        TransactionId txn_id,
        LabelId label_id,
        std::optional<uint64_t> limit = std::nullopt
    ) = 0;

    virtual folly::coro::AsyncGenerator<Result<Vertex>> getVerticesByLabel(
        TransactionId txn_id,
        LabelId label_id,
        std::optional<uint64_t> limit = std::nullopt
    ) = 0;

    // ==================== 属性索引查询 ====================

    virtual folly::coro::AsyncGenerator<Result<VertexId>> queryVertexIndex(
        TransactionId txn_id,
        LabelId label_id,
        const std::string& property,
        const PropertyValue& value
    ) = 0;

    virtual folly::coro::AsyncGenerator<Result<VertexId>> queryVertexIndexRange(
        TransactionId txn_id,
        LabelId label_id,
        const std::string& property,
        const PropertyValue& start,
        const PropertyValue& end
    ) = 0;

    // ==================== Edge 操作 ====================

    virtual folly::coro::Task<Result<Edge>> getEdge(
        TransactionId txn_id,
        EdgeId edge_id
    ) = 0;

    virtual folly::coro::Task<Result<EdgeId>> insertEdge(
        TransactionId txn_id,
        VertexId src_id,
        VertexId dst_id,
        EdgeLabelId label_id,
        const Properties& properties
    ) = 0;

    virtual folly::coro::Task<Result<void>> deleteEdge(
        TransactionId txn_id,
        EdgeId edge_id
    ) = 0;

    // ==================== 边遍历 ====================

    virtual folly::coro::AsyncGenerator<Result<Edge>> getOutEdges(
        TransactionId txn_id,
        VertexId vertex_id,
        std::optional<EdgeLabelId> label_id = std::nullopt
    ) = 0;

    virtual folly::coro::AsyncGenerator<Result<Edge>> getInEdges(
        TransactionId txn_id,
        VertexId vertex_id,
        std::optional<EdgeLabelId> label_id = std::nullopt
    ) = 0;

    virtual folly::coro::AsyncGenerator<Result<VertexId>> getNeighborIds(
        TransactionId txn_id,
        VertexId vertex_id,
        Direction direction,
        std::optional<EdgeLabelId> label_id = std::nullopt
    ) = 0;

    // ==================== 统计 ====================

    virtual folly::coro::Task<Result<uint64_t>> countVerticesByLabel(
        TransactionId txn_id,
        LabelId label_id
    ) = 0;

    virtual folly::coro::Task<Result<uint64_t>> countDegree(
        TransactionId txn_id,
        VertexId vertex_id,
        Direction direction,
        std::optional<EdgeLabelId> label_id = std::nullopt
    ) = 0;
};

// 事务操作接口
class ITransactionOperations {
public:
    virtual ~ITransactionOperations() = default;

    virtual folly::coro::Task<Result<TransactionId>> beginTransaction(
        IsolationLevel level = IsolationLevel::REPEATABLE_READ
    ) = 0;

    virtual folly::coro::Task<Result<void>> commit(TransactionId txn_id) = 0;
    virtual folly::coro::Task<Result<void>> rollback(TransactionId txn_id) = 0;
    virtual folly::coro::Task<Result<bool>> isActive(TransactionId txn_id) = 0;
};

// 存储服务聚合接口
class IStorageService {
public:
    virtual ~IStorageService() = default;

    virtual IGraphOperations& graph() = 0;
    virtual ITransactionOperations& transaction() = 0;

    virtual folly::coro::Task<bool> start() = 0;
    virtual folly::coro::Task<bool> stop() = 0;
    virtual bool isReady() const = 0;
};

} // namespace eugraph
```

### 10.2 元数据服务接口
```cpp
// src/common/interface/metadata_interface.hpp

#pragma once

#include "common/types/graph_types.hpp"
#include "common/types/error.hpp"

#include <folly/coro/Task.h>
#include <folly/Expected.h>

namespace eugraph {

template<typename T>
using Result = folly::Expected<T, Error>;

class IMetadataService {
public:
    virtual ~IMetadataService() = default;

    // ==================== Label (点类型) 管理 ====================

    virtual folly::coro::Task<Result<LabelId>> createLabel(
        const LabelName& name,
        const std::vector<PropertyDef>& properties = {},
        std::optional<std::string> primary_key = std::nullopt
    ) = 0;

    virtual folly::coro::Task<Result<LabelId>> getLabelId(const LabelName& name) = 0;
    virtual folly::coro::Task<Result<LabelName>> getLabelName(LabelId id) = 0;
    virtual folly::coro::Task<Result<LabelDef>> getLabelDef(const LabelName& name) = 0;
    virtual folly::coro::Task<Result<LabelDef>> getLabelDefById(LabelId id) = 0;
    virtual folly::coro::Task<Result<std::vector<LabelDef>>> listLabels() = 0;

    // ==================== EdgeLabel (关系类型) 管理 ====================

    virtual folly::coro::Task<Result<EdgeLabelId>> createEdgeLabel(
        const EdgeLabelName& name,
        const std::vector<PropertyDef>& properties = {},
        bool directed = true
    ) = 0;

    virtual folly::coro::Task<Result<EdgeLabelId>> getEdgeLabelId(const EdgeLabelName& name) = 0;
    virtual folly::coro::Task<Result<EdgeLabelName>> getEdgeLabelName(EdgeLabelId id) = 0;
    virtual folly::coro::Task<Result<EdgeLabelDef>> getEdgeLabelDef(const EdgeLabelName& name) = 0;
    virtual folly::coro::Task<Result<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) = 0;
    virtual folly::coro::Task<Result<std::vector<EdgeLabelDef>>> listEdgeLabels() = 0;

    // ==================== ID 分配 ====================

    virtual folly::coro::Task<Result<VertexId>> nextVertexId() = 0;
    virtual folly::coro::Task<Result<EdgeId>> nextEdgeId() = 0;

    // 服务生命周期
    virtual folly::coro::Task<bool> start() = 0;
    virtual folly::coro::Task<bool> stop() = 0;
    virtual bool isReady() const = 0;
};

} // namespace eugraph
```

### 10.3 计算服务接口
```cpp
// src/common/interface/compute_interface.hpp

#pragma once

#include "common/types/graph_types.hpp"
#include "common/types/error.hpp"

#include <folly/coro/Task.h>
#include <folly/coro/AsyncGenerator.h>
#include <folly/Expected.h>
#include <memory>

namespace eugraph {

template<typename T>
using Result = folly::Expected<T, Error>;

struct ResultRow {
    std::vector<PropertyValue> values;
};

struct ResultColumn {
    std::string name;
    std::string type;
};

class IResultSet {
public:
    virtual ~IResultSet() = default;

    virtual const std::vector<ResultColumn>& columns() const = 0;
    virtual uint64_t rowCount() const = 0;
    virtual folly::coro::AsyncGenerator<ResultRow> rows() = 0;
};

struct ExecutionStats {
    uint64_t rows_returned = 0;
    uint64_t vertices_scanned = 0;
    uint64_t edges_scanned = 0;
    std::chrono::microseconds execution_time;
};

class IComputeService {
public:
    virtual ~IComputeService() = default;

    virtual folly::coro::Task<Result<SessionId>> createSession() = 0;
    virtual folly::coro::Task<Result<void>> closeSession(SessionId session_id) = 0;

    virtual folly::coro::Task<Result<std::unique_ptr<IResultSet>>> execute(
        SessionId session_id,
        const std::string& gsql
    ) = 0;

    virtual folly::coro::Task<bool> start() = 0;
    virtual folly::coro::Task<bool> stop() = 0;
    virtual bool isReady() const = 0;
};

} // namespace eugraph
```

## 十一、目录结构

```
eugraph/
├── CMakeLists.txt
├── src/
│   ├── common/
│   │   ├── types/
│   │   │   ├── graph_types.hpp       # 图类型定义
│   │   │   ├── error.hpp             # 错误类型
│   │   │   └── constants.hpp         # 常量定义
│   │   │
│   │   ├── interface/
│   │   │   ├── storage_interface.hpp # 存储服务接口
│   │   │   ├── compute_interface.hpp # 计算服务接口
│   │   │   └── metadata_interface.hpp# 元数据服务接口
│   │   │
│   │   └── util/
│   │       ├── logging.hpp           # 日志工具
│   │       └── config.hpp            # 配置解析
│   │
│   ├── storage_service/
│   │   ├── storage_service.hpp       # 存储服务实现
│   │   ├── graph_operations.hpp      # 图操作实现
│   │   ├── transaction_manager.hpp   # 事务管理
│   │   └── kv/
│   │       ├── wiredtiger_store.hpp  # WiredTiger 封装
│   │       ├── key_codec.hpp         # Key 编解码
│   │       └── value_codec.hpp       # Value 编解码
│   │
│   ├── compute_service/
│   │   ├── compute_service.hpp       # 计算服务实现
│   │   ├── parser/
│   │   │   ├── lexer.hpp             # 词法分析
│   │   │   └── parser.hpp            # 语法分析
│   │   ├── planner/
│   │   │   └── query_planner.hpp     # 查询计划
│   │   └── executor/
│   │       └── query_executor.hpp    # 执行引擎
│   │
│   ├── metadata_service/
│   │   ├── metadata_service.hpp      # 元数据服务实现
│   │   ├── label_manager.hpp         # Label 管理
│   │   └── id_allocator.hpp          # ID 分配
│   │
│   ├── rpc/
│   │   ├── remote_storage_client.hpp # 远程存储客户端
│   │   ├── remote_metadata_client.hpp# 远程元数据客户端
│   │   └── serializer.hpp            # 序列化
│   │
│   ├── service/
│   │   ├── service_manager.hpp       # 服务管理器
│   │   └── service_config.hpp        # 服务配置
│   │
│   └── main.cpp                      # 入口
│
├── proto/
│   ├── storage.thrift                # 存储服务 Thrift 定义
│   ├── compute.thrift                # 计算服务 Thrift 定义
│   └── metadata.thrift               # 元数据服务 Thrift 定义
│
├── tests/
│   ├── unit/                         # 单元测试
│   └── integration/                  # 集成测试
│
├── third_party/
├── docs/
└── plan.md                           # 本文件
```

## 十二、开发阶段规划

### 阶段一: 基础框架 (MVP)

#### 1.1 项目骨架搭建
**目标**: 搭建基本的构建系统和目录结构

**内容**:
- CMakeLists.txt 配置
- src/ 目录结构创建
- 基础类型定义 (graph_types.hpp, error.hpp)
- 第三方依赖引入 (folly, spdlog, GoogleTest)

**完成标准**: 编译通过，能运行基础可执行文件

**测试用例**:
```cpp
TEST(ProjectSkeleton, Compilation) {
    // 验证项目可以编译
    EXPECT_TRUE(build_project());

    // 验证基础类型定义存在
    EXPECT_TRUE(fs::exists("src/common/types/graph_types.hpp"));
}

TEST(ProjectSkeleton, BasicExecutable) {
    // 验证可以编译并运行简单程序
    EXPECT_EQ(run_basic_program(), 0);
}
```

---

#### 1.2 WiredTiger 集成
**目标**: 封装 WiredTiger，提供基础 KV 操作接口

**内容**:
- WiredTiger 连接管理 (WiredTigerStore 类)
- 基础读写操作 (put/get/del)
- 游标操作 (scan/prefix scan)
- 事务支持 (begin/commit/rollback)
- Key/Value 编解码工具 (KeyCodec/ValueCodec)

**完成标准**:
- WiredTigerStore 类实现完成
- KeyCodec/ValueCodec 实现完成
- 所有操作通过单元测试

**测试用例**:
```cpp
// ==================== WiredTiger 存储测试 ====================

class WiredTigerStoreTest : public ::testing::Test {
protected:
    std::string test_db_path_;
    std::unique_ptr<WiredTigerStore> store_;

    void SetUp() override {
        test_db_path_ = "/tmp/eugraph_test_" + std::to_string(getpid());
        store_ = std::make_unique<WiredTigerStore>(test_db_path_);
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(test_db_path_);
    }

    // ==================== 基础读写测试 ====================

    TEST_F(SimplePutAndGet) {
        auto result = co_await store_->put("key1", "value1");
        EXPECT_TRUE(result);

        auto value = co_await store_->get("key1");
        EXPECT_TRUE(value.has_value());
        EXPECT_EQ(value.value(), "value1");
    }

    TEST_F(SimpleDelete) {
        auto result = co_await store_->put("key1", "value1");
        EXPECT_TRUE(result);

        auto result = co_await store_->del("key1");
        EXPECT_TRUE(result);

        auto value = co_await store_->get("key1");
        EXPECT_FALSE(value.has_value());
    }

    // ==================== 游标测试 ====================

    TEST_F(ScanOperation) {
        // 插入多条数据
        co_await store_->put("key1", "value1");
        co_await store_->put("key2", "value2");
        co_await store_->put("key3", "value3");

        // 匉缀扫描
        std::vector<std::string> results;
        auto cursor = co_await store_->scan("key");
        while (co_await cursor->valid()) {
            results.push(cursor->value());
            co_await cursor->next();
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0], "value1");
        EXPECT_EQ(results[1], "value2");
        EXPECT_EQ(results[2], "value3");
    }

    // ==================== 事务测试 ====================

    TEST_F(TransactionCommit) {
        auto txn = co_await store_->beginTransaction();

        co_await store_->put("key1", "value1");
        co_await store_->put("key2", "value2");

        auto result = co_await store_->commit(txn);
        EXPECT_TRUE(result);

        // 验证数据已提交
        auto value1 = co_await store_->get("key1");
        auto value2 = co_await store_->get("key2");
        EXPECT_TRUE(value1.has_value());
        EXPECT_TRUE(value2.has_value());
    }

    TEST_F(TransactionRollback) {
        auto txn = co_await store_->beginTransaction();

        co_await store_->put("key1", "value1");
        co_await store_->put("key2", "value2");

        auto result = co_await store_->rollback(txn);
        EXPECT_TRUE(result);

        // 验证数据已回滚
        auto value1 = co_await store_->get("key1");
        auto value2 = co_await store_->get("key2");
        EXPECT_FALSE(value1.has_value());
        EXPECT_FALSE(value2.has_value());
    }
};

// ==================== Key 编解码测试 ====================

class KeyCodecTest : public ::testing::Test {
protected:
    TEST_F(VertexKeyEncode) {
        auto key = KeyCodec::encodeVertexKey(1);
        auto decoded = KeyCodec::decodeVertexKey(key);
        EXPECT_EQ(decoded, 1);
    }

    TEST_F(EdgeKeyEncode) {
        auto key = KeyCodec::encodeEdgeKey(
            1,                // vertex_id
            Direction::OUT,
            1,                // edge_label_id
            2,                // neighbor_id
            100               // edge_id
        );

        auto decoded = KeyCodec::decodeEdgeKey(key);
        EXPECT_EQ(decoded.vertex_id, 1);
        EXPECT_EQ(decoded.direction, Direction::OUT);
        EXPECT_EQ(decoded.edge_label_id, 1);
        EXPECT_EQ(decoded.neighbor_id, 2);
        EXPECT_EQ(decoded.edge_id, 100);
    }

    TEST_F(LabelIndexKeyEncode) {
        auto key = KeyCodec::encodeLabelReverseKey(1, 1);  // vertex_id, label_id
        auto decoded = KeyCodec::decodeLabelReverseKey(key);
        EXPECT_EQ(decoded.vertex_id, 1);
        EXPECT_EQ(decoded.label_id, 1);
    }
};
```

---

#### 1.3 元数据服务
**目标**: 实现 Label/EdgeLabel 的 ID 映射和分配

**内容**:
- Label 名称 ↔ ID 映射
- EdgeLabel 名称 ↔ ID 映射
- ID 分配器 (原子递增)
- 元数据持久化
- MetadataService 接口实现

- Label 创建/查询/列表
- EdgeLabel 创建/查询/列表

- ID 分配

**完成标准**:
- MetadataService 类实现完成
- 所有操作通过单元测试
- 集成测试验证重启后数据恢复

**测试用例**:
```cpp
// ==================== 元数据服务测试 ====================

class MetadataServiceTest : public ::testing::Test {
protected:
    std::unique_ptr<MetadataService> meta_;

    void SetUp() override {
        meta_ = std::make_unique<MetadataService>();
    }

    // ==================== Label 测试 ====================

    TEST_F(CreateAndGetLabel) {
        // 创建标签
        auto label_id = co_await meta_->createLabel("Person",
            {{"name", PropertyType::STRING, true},
            "email");  // primary_key

        EXPECT_NE(label_id, INVALID_LABEL_ID);

        // 验证可以查询
        auto result = co_await meta_->getLabelId("Person");
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), label_id);

        auto name = co_await meta_->getLabelName(label_id);
        EXPECT_TRUE(name.has_value());
        EXPECT_EQ(name.value(), "Person");
    }

    TEST_F(ListLabels) {
        co_await meta_->createLabel("Person");
        co_await meta_->createLabel("User");
        co_await meta_->createLabel("Product");

        auto labels = co_await meta_->listLabels();
        EXPECT_TRUE(labels.has_value());
        EXPECT_EQ(labels.value().size(), 3);
    }

    // ==================== EdgeLabel 测试 ====================

    TEST_F(CreateAndGetEdgeLabel) {
        auto label_id = co_await meta_->createEdgeLabel("follows");
        EXPECT_NE(label_id, INVALID_EDGE_LABEL_ID);

        auto result = co_await meta_->getEdgeLabelId("follows");
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), label_id);
    }

    // ==================== ID 分配测试 ====================

    TEST_F(UniqueIdAllocation) {
        std::unordered_set<VertexId> vertex_ids;
        std::unordered_set<EdgeId> edge_ids;

        for (int i = 0; i < 100; i++) {
            auto vid = co_await meta_->nextVertexId();
            EXPECT_TRUE(vertex_ids.insert(vid).second);  // 确保唯一
            vertex_ids.insert(vid);
        }

        for (int i = 0; i < 100; i++) {
            auto eid = co_await meta_->nextEdgeId();
            EXPECT_TRUE(edge_ids.insert(eid).second);
            edge_ids.insert(eid);
        }
    }
};

// ==================== 集成测试: 重启恢复 ====================

class MetadataPersistenceTest : public ::testing::Test {
protected:
    std::string test_db_path_;

    void SetUp() override {
        test_db_path_ = "/tmp/eugraph_meta_test";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_db_path_);
    }

    TEST_F(LabelPersistence) {
        // 第一次创建
        {
            auto meta = std::make_unique<MetadataService>(test_db_path_);
            co_await meta->createLabel("Person");
        }

        // 重启后验证
        {
            auto meta = std::make_unique<MetadataService>(test_db_path_);
            auto label_id = co_await meta->getLabelId("Person");
            EXPECT_TRUE(label_id.has_value());
        }
    }
};
```

---

#### 1.4 图存储服务
**目标**: 实现 Vertex/Edge 的 CRUD 操作

**内容**:
- Vertex 操作
  - insertVertex (含标签索引维护)
  - getVertex
  - updateVertexProperties
  - deleteVertex (含标签索引清理)
  - getVertexLabels / addLabelToVertex / removeLabelFromVertex
- Edge 操作
  - insertEdge (含双向索引维护)
  - getEdge
  - getOutEdges / getInEdges
  - deleteEdge
- 索引操作
  - getVertexIdsByLabel (正向索引)
  - getVertexByPrimaryKey (主键索引)
  - queryVertexIndex (属性索引)
- GraphOperations 接口实现
- StorageService 接口实现

- **完成标准**:
  - GraphOperations 类实现完成
  - StorageService 类实现完成
  - 所有操作通过单元测试
  - 集成测试验证完整 CRUD 流程
  - 集成测试验证索引正确性
  - **测试用例**:
```cpp
// ==================== 图存储服务测试 ====================

class GraphOperationsTest : public ::testing::Test {
protected:
    std::unique_ptr<StorageService> storage_;
    TransactionId txn_id_;

    void SetUp() override {
        storage_ = std::make_unique<StorageService>();
        txn_id_ = co_await storage_->transaction().beginTransaction();
    }

    void TearDown() override {
        co_await storage_->transaction().commit(txn_id_);
    }

    // ==================== Vertex 基础操作测试 ====================

    TEST_F(InsertAndGetVertex) {
        LabelIdSet labels = {1, 2};  // Person, User

        auto vid = co_await storage_->graph().insertVertex(txn_id_, labels,
            {{"name", "Alice"}, {"age", 30}});

        EXPECT_NE(vid, INVALID_VERTEX_ID);

        auto vertex = co_await storage_->graph().getVertex(txn_id_, vid);
        EXPECT_TRUE(vertex.has_value());
        EXPECT_EQ(vertex.value().id, vid);
    }

    TEST_F(UpdateVertexProperties) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        // 更新属性
        co_await storage_->graph().updateVertexProperties(txn_id_, vid,
            {{"name", "Bob"}, {"age", 25}});

        auto vertex = co_await storage_->graph().getVertex(txn_id_, vid);
        EXPECT_TRUE(vertex.has_value());
        EXPECT_EQ(std::get<std::string>(vertex.value().properties["name"]), "Bob");
        EXPECT_EQ(std::get<int64_t>(vertex.value().properties["age"]), 25);
    }

    TEST_F(DeleteVertex) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1}, {{"name", "Alice"}});

        co_await storage_->graph().deleteVertex(txn_id_, vid);

        auto vertex = co_await storage_->graph().getVertex(txn_id_, vid);
        EXPECT_FALSE(vertex.has_value());
    }

    // ==================== 标签操作测试 ====================

    TEST_F(GetVertexLabels) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1, 2}, {});

        auto labels = co_await storage_->graph().getVertexLabels(txn_id_, vid);
        EXPECT_TRUE(labels.has_value());
        EXPECT_EQ(labels.value().size(), 2);
        EXPECT_TRUE(labels.value().contains(1));
        EXPECT_TRUE(labels.value().contains(2));
    }

    TEST_F(AddAndRemoveLabel) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        // 添加标签
        co_await storage_->graph().addLabelToVertex(txn_id_, vid, 2);
        auto labels = co_await storage_->graph().getVertexLabels(txn_id_, vid);
        EXPECT_EQ(labels.value().size(), 2);

        // 移除标签
        co_await storage_->graph().removeLabelFromVertex(txn_id_, vid, 1);
        labels = co_await storage_->graph().getVertexLabels(txn_id_, vid);
        EXPECT_EQ(labels.value().size(), 1);
        EXPECT_TRUE(labels.value().contains(2));
    }

    // ==================== Edge 操作测试 ====================

    TEST_F(InsertAndGetEdge) {
        auto src = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto dst = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        auto eid = co_await storage_->graph().insertEdge(txn_id_, src, dst, 1,
            {{"since", 2020}});

        EXPECT_NE(eid, INVALID_EDGE_ID);

        auto edge = co_await storage_->graph().getEdge(txn_id_, eid);
        EXPECT_TRUE(edge.has_value());
        EXPECT_EQ(edge.value().src_id, src);
        EXPECT_EQ(edge.value().dst_id, dst);
    }

    TEST_F(GetOutEdges) {
        auto src = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto dst1 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto dst2 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        co_await storage_->graph().insertEdge(txn_id_, src, dst1, 1, {});
        co_await storage_->graph().insertEdge(txn_id_, src, dst2, 1, {});

        auto edges = storage_->graph().getOutEdges(txn_id_, src, 1);

        std::vector<Edge> results;
        while (auto e = co_await edges.next()) {
            if (e.hasValue()) results.push(e.value());
        }

        EXPECT_EQ(results.size(), 2);
    }

    TEST_F(GetInEdges) {
        auto src1 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto src2 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto dst = co_await storage_->graph().insertVertex(txn_id_, {1}, {});

        co_await storage_->graph().insertEdge(txn_id_, src1, dst, 1, {});
        co_await storage_->graph().insertEdge(txn_id_, src2, dst, 1, {});

        auto edges = storage_->graph().getInEdges(txn_id_, dst, 1);

        std::vector<Edge> results;
        while (auto e = co_await edges.next()) {
            if (e.hasValue()) results.push(e.value());
        }

        EXPECT_EQ(results.size(), 2);
    }

    // ==================== 索引查询测试 ====================

    TEST_F(GetVertexIdsByLabel) {
        auto vid1 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto vid2 = co_await storage_->graph().insertVertex(txn_id_, {1}, {});
        auto vid3 = co_await storage_->graph().insertVertex(txn_id_, {2}, {});

        auto gen = storage_->graph().getVertexIdsByLabel(txn_id_, 1);

        std::vector<VertexId> results;
        while (auto v = co_await gen.next()) {
            if (v.hasValue()) results.push(v.value());
        }

        EXPECT_EQ(results.size(), 2);
        EXPECT_TRUE(std::find(results.begin(), results.end(), vid1) != results.end());
        EXPECT_TRUE(std::find(results.begin(), results.end(), vid2) != results.end());
    }

    TEST_F(GetVertexByPrimaryKey) {
        auto vid = co_await storage_->graph().insertVertex(txn_id_, {1},
            {{"email", "alice@example.com"}});
        // 注意: insertVertex 需要支持主键索引

        auto result = co_await storage_->graph().getVertexIdByPrimaryKey("alice@example.com");
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), vid);
    }
};

// ==================== 集成测试: 完整流程 ====================

class GraphIntegrationTest : public ::testing::Test {
protected:
    std::unique_ptr<StorageService> storage_;
    std::unique_ptr<MetadataService> meta_;

    void SetUp() override {
        meta_ = std::make_unique<MetadataService>();
        storage_ = std::make_unique<StorageService>(meta_.get());
    }

    TEST_F(FullGraphFlow) {
        // 1. 创建标签
        auto person_id = co_await meta_->createLabel("Person");
        auto follows_id = co_await meta_->createEdgeLabel("follows");

        // 2. 创建顶点
        auto txn = co_await storage_->transaction().beginTransaction();

        auto alice = co_await storage_->graph().insertVertex(txn, {person_id},
            {{"name", "Alice"}, {"email", "alice@example.com"}});
        auto bob = co_await storage_->graph().insertVertex(txn, {person_id},
            {{"name", "Bob"}, {"email", "bob@example.com"}});

        // 3. 创建边
        co_await storage_->graph().insertEdge(txn, alice, bob, follows_id,
            {{"since", 2020}});

        co_await storage_->transaction().commit(txn);

        // 4. 查询验证
        auto txn2 = co_await storage_->transaction().beginTransaction();

        // 通过主键查询
        auto alice_by_pk = co_await storage_->graph().getVertexByPrimaryKey("alice@example.com");
        EXPECT_TRUE(alice_by_pk.has_value());

        // 通过标签查询
        auto persons = storage_->graph().getVertexIdsByLabel(txn2, person_id);
        int count = 0;
        while (auto v = co_await persons.next()) {
            if (v.hasValue()) count++;
        }
        EXPECT_EQ(count, 2);

        // 遍历边
        auto edges = storage_->graph().getOutEdges(txn2, alice, follows_id);
        count = 0;
        while (auto e = co_await edges.next()) {
            if (e.hasValue()) count++;
        }
        EXPECT_EQ(count, 1);

        co_await storage_->transaction().commit(txn2);
    }
};
```

---

#### 1.5 基础事务支持
**目标**: 实现单机 MVCC 事务

**内容**:
- 事务管理器 (TransactionManager)
  - beginTransaction
  - commit
  - rollback
  - getTransaction
- 快照管理 (SnapshotManager)
  - createSnapshot
  - getSnapshot
- MVCC 可见性控制
  - 写入时创建新版本
  - 读取时根据快照读取
- 可重复读隔离级别实现
- TransactionOperations 接口实现

- **完成标准**:
  - TransactionManager 类实现完成
  - SnapshotManager 类实现完成
  - 可重复读隔离级别正确实现
  - 所有操作通过单元测试
  - 集成测试验证并发场景
  - **测试用例**:
```cpp
// ==================== 事务管理器测试 ====================

class TransactionManagerTest : public ::testing::Test {
protected:
    std::unique_ptr<TransactionManager> txn_mgr_;

    void SetUp() override {
        txn_mgr_ = std::make_unique<TransactionManager>();
    }

    TEST_F(BeginTransaction) {
        auto txn_id = co_await txn_mgr_->beginTransaction();
        EXPECT_NE(txn_id, 0);

        auto txn = txn_mgr_->getTransaction(txn_id);
        EXPECT_TRUE(txn != nullptr);
        EXPECT_TRUE(txn->isActive());
    }

    TEST_F(CommitTransaction) {
        auto txn_id = co_await txn_mgr_->beginTransaction();

        auto result = co_await txn_mgr_->commit(txn_id);
        EXPECT_TRUE(result);

        auto txn = txn_mgr_->getTransaction(txn_id);
        EXPECT_TRUE(txn == nullptr || !txn->isActive());
    }

    TEST_F(RollbackTransaction) {
        auto txn_id = co_await txn_mgr_->beginTransaction();

        auto result = co_await txn_mgr_->rollback(txn_id);
        EXPECT_TRUE(result);

        auto txn = txn_mgr_->getTransaction(txn_id);
        EXPECT_TRUE(txn == nullptr || !txn->isActive());
    }

    TEST_F(ConcurrentTransactions) {
        auto txn1 = co_await txn_mgr_->beginTransaction();
        auto txn2 = co_await txn_mgr_->beginTransaction();

        EXPECT_NE(txn1, txn2);

        auto t1 = txn_mgr_->getTransaction(txn1);
        auto t2 = txn_mgr_->getTransaction(txn2);

        EXPECT_TRUE(t1 != t2);
    }
};

// ==================== 快照管理测试 ====================

class SnapshotManagerTest : public ::testing::Test {
protected:
    std::unique_ptr<SnapshotManager> snap_mgr_;

    void SetUp() override {
        snap_mgr_ = std::make_unique<SnapshotManager>();
    }

    TEST_F(CreateSnapshot) {
        auto snap_id = co_await snap_mgr_->createSnapshot();
        EXPECT_NE(snap_id, 0);
    }

    TEST_F(SnapshotVisibility) {
        // 创建快照
        auto snap1 = co_await snap_mgr_->createSnapshot();

        // 写入数据
        // ... (需要与存储层集成)

        // 创建新快照
        auto snap2 = co_await snap_mgr_->createSnapshot();

        // 验证快照1 可以看到 snap1 之前的数据
        // 验证快照2 可以看到所有数据
    }
};

// ==================== MVCC 隔离级别测试 ====================

class MVCCIsolationTest : public ::testing::Test {
protected:
    std::unique_ptr<StorageService> storage_;

    void SetUp() override {
        storage_ = std::make_unique<StorageService>();
    }

    TEST_F(RepeatableRead) {
        // 事务1: 写入数据
        auto txn1 = co_await storage_->transaction().beginTransaction();
        co_await storage_->graph().insertVertex(txn1, {1}, {{"name", "Alice"}});

        // 事务2: 开始事务 (应该看到 Alice)
        auto txn2 = co_await storage_->transaction().beginTransaction(
        auto v1 = co_await storage_->graph().getVertexIdsByLabel(txn2, 1);
        int count = 0;
        while (auto v = co_await v1.next()) {
            if (v.hasValue()) count++;
        }
        EXPECT_EQ(count, 1);

        // 事务1: 提交
        co_await storage_->transaction().commit(txn1);

        // 事务2: 应该仍然只看到 Alice (可重复读)
        auto v2 = co_await storage_->graph().getVertexIdsByLabel(txn2, 1);
        count = 0;
        while (auto v = co_await v2.next()) {
            if (v.hasValue()) count++;
        }
        EXPECT_EQ(count, 1);
    }

    TEST_F(WriteConflict) {
        // 两个事务同时修改同一个顶点
        auto txn1 = co_await storage_->transaction().beginTransaction();
        auto txn2 = co_await storage_->transaction().beginTransaction();

        auto vid = co_await storage_->graph().insertVertex(txn1, {1}, {{"value", 1}});

        // 事务2 尝试读取 (应该成功)
        auto v = co_await storage_->graph().getVertex(txn2, vid);
        EXPECT_TRUE(v.has_value());

        // 事务1 修改
        co_await storage_->graph().updateVertexProperties(txn1, vid, {{"value", 2}});

        // 事务1 提交
        auto result1 = co_await storage_->transaction().commit(txn1);
        EXPECT_TRUE(result1);

        // 事务2 尝试提交 (应该失败或需要重试)
        auto result2 = co_await storage_->transaction().commit(txn2);
        // 根据实现，可能是失败或成功
    }

    TEST_F(TransactionRollbackCorrectness) {
        auto txn = co_await storage_->transaction().beginTransaction();

        auto vid = co_await storage_->graph().insertVertex(txn, {1}, {{"name", "Alice"}});

        // 回滚
        co_await storage_->transaction().rollback(txn);

        // 验证数据不可见
        auto v = co_await storage_->graph().getVertex(txn, vid);
        EXPECT_FALSE(v.has_value());
    }
};
```

---

## 测试执行方式

- 使用 GoogleTest 框架
- 每个功能模块有对应的单元测试
- 集成测试覆盖主要使用场景
- 使用内存数据库进行测试 (WiredTiger 测试模式)
- 性能测试后期考虑
- 测试覆盖率目标: 80%+

## 十三、依赖库
| 组件 | 选择 | 说明 |
|------|------|------|
| 构建系统 | CMake | C++ 标准选择 |
| 协程库 | folly | Meta 成熟协程库 |
| RPC | fbthrift | folly 原生支持 |
| KV 存储 | WiredTiger | 成熟、高性能、支持事务 |
| 日志库 | spdlog | 高性能、C++20 友好 |
| 测试框架 | GoogleTest | 成熟稳定 |
