# Neo4j Bolt 协议支持设计

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

## 概述

EuGraph 支持 Neo4j Bolt 协议后，允许任何 Neo4j 官方/社区驱动（Python、Java、Go、JS、C#、Rust）直接连接 EuGraph，无需修改客户端代码。

### 支持的 Bolt 版本

**实际测试通过**：Bolt v5.1（Python neo4j 5.28.x 驱动 + cypher-shell 5.26.x）。

握手阶段声明支持 v5.1、v5.0、v4.4 三个版本并可协商成功，但**协议行为始终按 v5.1 处理**（未根据协商版本分派不同行为），因此 v5.0/v4.4 驱动的兼容性未实际验证，见缺陷 2。

## 架构

```
┌──────────────────────────────────────────────────────────────────────┐
│                        EuGraph Process                                │
│                                                                       │
│  ┌─────────────────────┐  ┌──────────────────────────────────────┐   │
│  │  fbthrift Server    │  │  Bolt Server                         │   │
│  │  port 9090          │  │  port 7687                           │   │
│  │  EuGraphHandler     │  │  BoltSession                         │   │
│  └─────────┬───────────┘  └──────────────┬───────────────────────┘   │
│            │                              │                           │
│            └──────────────┬───────────────┘                           │
│                           ▼                                           │
│          ┌────────────────┴────────────────┐                        │
│          │       GraphService               │                        │
│          │  协议无关业务逻辑层                │                        │
│          └────────────────┬────────────────┘                        │
│                           │                                           │
│           ┌───────────────┼───────────────┐                          │
│           │               ▼               │                          │
│    QueryExecutor    GraphManager    Storage Layer                     │
│                                                                       │
└──────────────────────────────────────────────────────────────────────┘
```

### 设计原则

1. **同一进程**：Bolt 服务器与 fbthrift 服务器在同一进程中运行，直接复用 IoScheduler 和 CPUThreadPoolExecutor，零额外网络开销
2. **共享服务层**：从 `EuGraphHandler` 抽取 `GraphService` 作为协议无关的业务逻辑层，Thrift 和 Bolt 共用
3. **零改动**：`QueryExecutor`、存储层、`GraphManager`、Cypher 解析器完全不需要修改

## Bolt 协议流程

### 消息状态机

```
客户端                          服务端
  |                               |
  |--- HANDSHAKE (0x6060B017) --->|
  |<-- 版本协商 ------------------|
  |--- HELLO {user_agent, ...} -->|
  |<-- SUCCESS {server, ...} -----|
  |--- LOGON {scheme, auth} ----->|
  |<-- SUCCESS -------------------|
  |                               |
  |--- RUN "MATCH (n) RETURN n" ->|
  |<-- SUCCESS {fields, ...} -----|
  |--- PULL {n: 1000} ----------->|
  |<-- RECORD [node1, node2, ...] |
  |<-- SUCCESS {type: "r"} -------|
  |                               |
  |--- GOODBYE ------------------>|
  |<-- (连接关闭) -----------------|
```

### 会话状态

```
CONNECTING → READY → STREAMING → READY
                 → TX_READY → TX_STREAMING → TX_READY
任何状态 → FAILED → (RESET) → READY
任何状态 → (GOODBYE) → CLOSED
```

### 支持的消息类型

| 消息 | 标签 | 方向 | 描述 |
|------|------|------|------|
| HELLO | 0x01 | C→S | 客户端认证与协议协商 |
| LOGON | 0x6A | C→S | 认证（Bolt v5.0+ 驱动） |
| LOGOFF | 0x6B | C→S | 登出 |
| RUN | 0x10 | C→S | 执行 Cypher 查询 |
| PULL | 0x3F | C→S | 拉取结果批次（支持 n 限制） |
| DISCARD | 0x2E | C→S | 丢弃剩余结果 |
| BEGIN | 0x11 | C→S | 开始显式事务 |
| COMMIT | 0x12 | C→S | 提交事务 |
| ROLLBACK | 0x13 | C→S | 回滚事务 |
| RESET | 0x0F | C→S | 重置连接状态（任意状态下可用） |
| GOODBYE | 0x02 | C→S | 关闭连接 |
| ROUTE | 0x66 | C→S | 获取路由表（neo4j:// 协议，返回自身单节点桩） |
| TELEMETRY | 0x54 | C→S | 驱动 API 遥测（返回空 SUCCESS 表示已收到） |
| NOOP | 0x00 | C→S | 保活心跳（chunk terminator 0x0000 隐式处理） |
| SUCCESS | 0x70 | S→C | 操作成功响应 |
| FAILURE | 0x7F | S→C | 操作失败响应 |
| IGNORED | 0x7E | S→C | 操作被忽略（FAILED 状态下的后续消息） |
| RECORD | 0x71 | S→C | 查询结果行 |

## PackStream 编解码

Bolt 协议使用 PackStream 二进制编码（类似 MessagePack），定义在 `src/bolt/packstream/`。

### 类型映射

| PackStream | 标记 | C++ 类型 |
|-----------|------|---------|
| Null | `0xC0` | `std::monostate` |
| Boolean | `0xC2`/`0xC3` | `bool` |
| Integer | `0xC8`/`0xC9`/`0xCA`/`0xCB` | `int64_t` |
| Float | `0xC1` | `double` |
| String | `0x80-0x8F`/`0xD0`/`0xD1`/`0xD2` | `std::string` |
| Bytes | `0xCC`/`0xCD`/`0xCE` | `std::vector<uint8_t>` |
| List | `0x90-0x9F`/`0xD4`/`0xD5`/`0xD6` | `std::vector<PackStreamValue>` |
| Dictionary | `0xA0-0xAF`/`0xD8`/`0xD9`/`0xDA` | `std::unordered_map<string, PackStreamValue>` |
| Structure | `0xB0-0xBF` | `{tag, fields[]}` |

### Bolt 结构体标记（v5.1）

**Client→Server 消息**：
| 标记 | 名称 | 描述 |
|------|------|------|
| 0x01 | HELLO | 握手/认证 |
| 0x02 | GOODBYE | 关闭连接 |
| 0x0F | RESET | 重置状态 |
| 0x10 | RUN | 执行查询 |
| 0x11 | BEGIN | 开始事务 |
| 0x12 | COMMIT | 提交事务 |
| 0x13 | ROLLBACK | 回滚事务 |
| 0x2E | DISCARD | 丢弃结果 |
| 0x3F | PULL | 拉取结果 |
| 0x54 | TELEMETRY | 驱动遥测 |
| 0x66 | ROUTE | 路由表查询 |
| 0x6A | LOGON | 认证 |
| 0x6B | LOGOFF | 登出 |

**Server→Client 响应**：
| 标记 | 名称 | 描述 |
|------|------|------|
| 0x70 | SUCCESS | 成功响应 |
| 0x71 | RECORD | 数据行 |
| 0x7E | IGNORED | 忽略响应 |
| 0x7F | FAILURE | 失败响应 |

**结果结构体**：
| 标记 | 名称 | 描述 |
|------|------|------|
| 0x4E | NODE | 节点结构体 |
| 0x50 | PATH | 路径结构体 |
| 0x52 | RELATIONSHIP | 关系结构体 |

**时间类型结构体**：
| 标记 | 名称 | 字段 |
|------|------|------|
| 0x44 | DATE | `[days_since_epoch: int]` |
| 0x45 | DURATION | `[months: int, days: int, seconds: int, nanos: int]` |
| 0x49 | DATETIME | `[utc_seconds: int, nanos: int, offset_seconds: int]` |
| 0x54 | TIME | `[nanos: int, offset_seconds: int]` |
| 0x64 | LOCAL_DATETIME | `[seconds: int, nanos: int]` |
| 0x69 | DATETIME_ZONE_ID | `[utc_seconds: int, nanos: int, zone_id: string]` |
| 0x74 | LOCAL_TIME | `[nanos: int]` |

> **Bolt v5.0+ 变更**：DATETIME 标签从 0x46 改为 0x49，DATETIME_ZONE_ID 从 0x66 改为 0x69。`seconds` 字段从本地墙上时间改为 UTC 纪元秒（local_seconds - tz_offset_sec）。

## 数据类型映射（Value → Bolt）

| 内部 `Value` 类型 | Bolt 结构体 |
|-------------------|------------|
| `bool` | Boolean |
| `int64_t` | Integer |
| `double` | Float |
| `std::string` | String |
| `std::monostate` | Null |
| `VertexValue` | `NODE(id, labels[], properties{})` |
| `EdgeValue` | `RELATIONSHIP(id, startNodeId, endNodeId, type, properties{})` |
| `PathValue` | `PATH(nodes[], relationships[], sequence[])` |
| `ListValue` | List |
| `MapValue` | Dictionary |
| `DateTimeValue` | DateTime/Date/LocalDateTime/DateTimeZoneId（按 kind 分派） |
| `TimeValue` | Time/LocalTime（按 kind 分派） |
| `DurationValue` | Duration |

## 实现机制

### 1. 分块传输编码（Chunked Transfer Encoding）

Bolt v5.1 使用分块传输编码进行消息帧定界：

```
[2字节 chunk_size (大端)] [chunk_size 字节数据] ... [2字节 chunk_size] [chunk] [0x00 0x00 终止符]
```

- **多 chunk 支持**：`BoltConnection::processMessage()` 循环读取 chunk 头，将数据累积到 `message_accumulator_`，遇到 `0x0000` 终止符后解码完整消息。chunk 大小上限 16383 (0x3FFF)，消息总大小上限 64 MiB（防内存耗尽）
- **握手响应不分块**：客户端握手（0x6060B017）及服务端版本协商响应均为原始字节
- **消息流水线**：驱动会流水线发送 HELLO+LOGON 和 RUN+PULL，`processMessage()` 在一次 `readDataAvailable` 回调中循环处理缓冲区的全部消息
- **NOOP 保活**：空的 `0x0000` 终止符视为 NOOP，直接忽略

### 2. 握手协商

```
客户端 → 服务端: 0x6060B017 (4字节 BOLT 魔数)
客户端 → 服务端: [version1, version2, version3, 0x00000000] (4个4字节版本提案)
服务端 → 客户端: [selected_version] (4字节)
```

- 服务端按优先级支持：v5.1 (0x00000501)、v5.0、v4.4
- 支持简单格式和范围编码（range encoding）两种提案解析
- 无匹配时默认选择 v5.1

### 3. 认证机制

支持两种认证流程：

- **HELLO 内联认证**（Bolt v5.0+ 客户端）：HELLO 字段中直接包含 `scheme`、`principal`、`credentials`，在 `handleHello()` 中校验
- **LOGON 分离认证**（Python 驱动 v5.x、cypher-shell）：HELLO 只含 `user_agent`，auth 信息通过 LOGON 消息独立发送，在 `handleLogon()` 中提取 scheme 并校验

默认密码硬编码为 `"eugraph"`，用户名任意。无 scheme 声明的连接跳过认证（兼容无认证模式）。

### 4. 因果一致性（Bookmark）

单机场景下简化为单调递增 ID：

- `BoltServer` 维护 `std::atomic<uint64_t> bookmark_counter_`
- COMMIT 和自动提交 PULL 后生成 `"eugraph:bookmark:N"` 并返回给客户端
- RUN/BEGIN 从 `extra` metadata 中提取 `bookmarks` 列表存入会话

### 5. ROUTE 路由（单机桩）

`handleRoute()` 返回固定路由表，指向 `localhost:7687`：

```
{ rt: { servers: [
  { addresses: ["localhost:7687"], role: "WRITE" },
  { addresses: ["localhost:7687"], role: "READ"  },
  { addresses: ["localhost:7687"], role: "ROUTE" }
], ttl: 3600 } }
```

使 `neo4j://` 协议的驱动能正常连接单节点。ROUTE 在 READY 和 CONNECTING 状态下均可用。

### 6. 自动创建 Label

`CreateNodePhysicalOp::prepareLabels_()` 在 CREATE 节点时自动创建 AST 中指定但 catalog 中不存在的 label（Phase 0）。与 Neo4j 行为一致：无需预先执行 DDL。

### 7. CALL 存储过程

`BoundCallOp` 逻辑算子 + `CallPhysicalOp` 物理算子支持 CALL 子句。内置存储过程：`db.ping()` 和 `db.schema.visualization()`。

### 8. Cypher DDL（数据库管理）

通过 `DatabaseDdlParser`（token-based）在 `GraphService::executeCypher()` 中拦截 DDL 语句，Bolt 和 Thrift 两条路径共享。支持：`CREATE DATABASE`、`DROP DATABASE`、`SHOW DATABASES`、`SHOW DATABASE`、`USE <graph>`。

## 文件清单

```
src/server/graph_service.hpp/.cpp               # 协议无关服务层
src/bolt/packstream/types.hpp                   # PackStream 类型定义
src/bolt/packstream/encoder.hpp/.cpp            # PackStream 编码器
src/bolt/packstream/decoder.hpp/.cpp            # PackStream 解码器
src/bolt/bolt_messages.hpp                      # Bolt 消息结构与标签
src/bolt/bolt_session.hpp/.cpp                  # 会话状态机与消息处理
src/bolt/bolt_value_mapping.hpp/.cpp            # Value ↔ Bolt 类型映射
src/bolt/bolt_server.hpp/.cpp                   # TCP 服务端（folly::AsyncServerSocket）
src/query/planner/logical_plan/operator/bound_call_op.hpp   # CALL 子句逻辑算子
src/query/planner/binder/bind_call.cpp                      # CALL 子句绑定
src/query/physical_plan/operator/call_physical_op.hpp/.cpp  # CALL 物理执行算子
src/query/parser/database_ddl_parser.hpp/.cpp               # 数据库 DDL 解析器
tests/test_packstream.cpp                       # PackStream 单元测试
tests/test_bolt_values.cpp                      # Bolt 类型映射测试
tests/bolt/test_bolt_integration.py             # Python 驱动集成测试
```

## 测试覆盖

| 测试层 | 数量 | 内容 |
|--------|------|------|
| C++ PackStream 单元测试 | 27 | 编解码往返（Null/Bool/Int/Float/String/Bytes/List/Dict/Struct） |
| C++ Bolt 类型映射测试 | 26 | 标量/Vertex/Edge/Path/时间类型 × Param 双方向 |
| Python 集成测试 | 22 | 连接、CRUD、全部类型往返、参数传递、显式事务提交/回滚 |

## 已知缺陷

### 缺陷 1：NODE 缺少 element_id 字段

**严重程度**：中 | **影响范围**：neo4j 5.x 驱动通过 `GraphDatabase.driver()` 连接

neo4j 5.x 驱动期望 NODE 结构体包含 4 个字段：`id, labels, props, element_id`。当前只输出 3 个字段（缺 element_id）。部分驱动（cypher-shell）容忍此差异，但 Python 驱动可能报错。

### 缺陷 2：v4.x 驱动不兼容

**严重程度**：低 | **影响范围**：旧版驱动

当前始终使用 v5.x 的时间类型标签（DATETIME=0x49、DATETIME_ZONE_ID=0x69），如果客户端协商到 v4.4，返回的时间类型标签会与驱动预期不匹配（v4.4 期望 0x46/0x66）。需要根据 `negotiated_version_` 分派不同的标签。

### 缺陷 3：LOGOFF 无实际清理

**严重程度**：低 | **影响范围**：长时间连接的会话

`handleLogoff()` 返回 SUCCESS 但不释放任何资源（stream_ctx_、pending_txn_ 等），资源实际在 RESET 或 GOODBYE 时才释放。

### 低优先级

| # | 问题 | 说明 |
|---|------|------|
| 4 | **PackStream 解码器缺 STRUCT_32** | 不支持 >65535 字段的结构体（极端罕见场景） |
| 5 | **空间类型 (Point) 缺失** | 类型系统无 Point2D/Point3D |
| 6 | **认证密码硬编码** | 密码 `"eugraph"` 写死在代码中，应从配置文件读取 |

## 参考

- [Neo4j Bolt Protocol Specification v5.1](https://neo4j.com/docs/bolt/current/)
- [ArcadeDB Bolt Plugin](https://docs.arcadedb.com/) — Java 参考实现
- [Memgraph](https://memgraph.com/) — C++ 图数据库 Bolt 实现
- [libneo4j-client](https://github.com/cleishm/libneo4j-client) — C Bolt 客户端库（PackStream 参考）
- [packstream](https://github.com/neo4j-packstream/neo4j-packstream-specification-v1)
