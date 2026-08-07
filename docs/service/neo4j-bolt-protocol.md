# Neo4j Bolt 协议支持设计

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

## 概述

EuGraph 支持 Neo4j Bolt 协议后，允许任何 Neo4j 官方/社区驱动（Python、Java、Go、JS、C#、Rust）直接连接 EuGraph，无需修改客户端代码。

### 支持的 Bolt 版本

Bolt v5.1（可协商降级至 v4.4）。

## 架构

```
┌──────────────────────────────────────────────────────────────────────┐
│                        EuGraph Process                                │
│                                                                       │
│  ┌─────────────────────┐  ┌──────────────────────────────────────┐   │
│  │  fbthrift Server    │  │  Bolt Server (NEW)                   │   │
│  │  port 9090          │  │  port 7687                           │   │
│  │  EuGraphHandler     │  │  BoltSession                         │   │
│  └─────────┬───────────┘  └──────────────┬───────────────────────┘   │
│            │                              │                           │
│            └──────────────┬───────────────┘                           │
│                           ▼                                           │
│          ┌────────────────┴────────────────┐                        │
│          │       GraphService (NEW)         │                        │
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
任何状态 → (GOODBYE) → 关闭
```

### 支持的消息类型

| 消息 | 方向 | 描述 |
|------|------|------|
| HELLO | C→S | 客户端认证与协议协商 |
| RUN | C→S | 执行 Cypher 查询 |
| PULL | C→S | 拉取结果批次（支持 fetch-size） |
| DISCARD | C→S | 丢弃剩余结果 |
| BEGIN | C→S | 开始显式事务 |
| COMMIT | C→S | 提交事务 |
| ROLLBACK | C→S | 回滚事务 |
| RESET | C→S | 重置连接状态 |
| GOODBYE | C→S | 关闭连接 |
| SUCCESS | S→C | 操作成功响应 |
| FAILURE | S→C | 操作失败响应 |
| IGNORED | S→C | 操作被忽略（FAILED 状态下的后续消息） |
| RECORD | S→C | 查询结果行 |

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

| 标记 | 名称 | 描述 |
|------|------|------|
| `0x01` | HELLO | 握手/认证 |
| `0x02` | GOODBYE | 关闭连接 |
| `0x0F` | RESET | 重置状态 |
| `0x10` | RUN | 执行查询 |
| `0x2F` | BEGIN | 开始事务 |
| `0x12` | COMMIT | 提交事务 |
| `0x13` | ROLLBACK | 回滚事务 |
| `0x3F` | PULL | 拉取结果 |
| `0x70` | SUCCESS | 成功响应 |
| `0x7E` | IGNORED | 忽略响应 |
| `0x7F` | FAILURE | 失败响应 |
| `0x71` | RECORD | 数据行 |
| `0x4E` | NODE | 节点结构体 |
| `0x52` | RELATIONSHIP | 关系结构体 |
| `0x50` | PATH | 路径结构体 |

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
| `DateTimeValue` | Bolt DateTime 结构体 |
| `TimeValue` | Bolt Time 结构体 |
| `DurationValue` | Bolt Duration 结构体 |

## 实施阶段

### 阶段 1：抽取 GraphService 服务层
- 从 `EuGraphHandler` 中抽出业务逻辑到 `src/server/graph_service.hpp/.cpp`
- 关键 API：`executeCypher()`、`createLabel()`、`createEdgeLabel()`、`batchInsert*()`
- 所有接口使用内部类型，零 Thrift 依赖

### 阶段 2：PackStream 编解码器
- `src/bolt/packstream/types.hpp` — 类型定义
- `src/bolt/packstream/encoder.hpp/.cpp` — 编码器
- `src/bolt/packstream/decoder.hpp/.cpp` — 解码器

### 阶段 3：Bolt 协议会话处理
- `src/bolt/bolt_messages.hpp` — 消息结构体定义
- `src/bolt/bolt_session.hpp/.cpp` — 会话状态机与消息分发
- `src/bolt/bolt_value_mapping.hpp/.cpp` — Value ↔ Bolt 类型转换

### 阶段 4：Bolt TCP 服务器
- `src/bolt/bolt_server.hpp/.cpp` — 基于 folly::AsyncServerSocket 的 Bolt 服务端
- 集成到 `eugraph_server_main.cpp`（新增 `--bolt-port` 参数，默认 7687）

### 阶段 5：测试
- `tests/test_packstream.cpp` — PackStream 编解码单元测试
- `tests/test_bolt_values.cpp` — Bolt 类型映射测试
- `tests/bolt/test_bolt_integration.py` — Python neo4j 驱动端到端集成测试

## 文件清单

### 新增文件
```
src/server/graph_service.hpp              # 协议无关服务层
src/server/graph_service.cpp
src/bolt/packstream/types.hpp             # PackStream 类型定义
src/bolt/packstream/encoder.hpp           # PackStream 编码器
src/bolt/packstream/encoder.cpp
src/bolt/packstream/decoder.hpp           # PackStream 解码器
src/bolt/packstream/decoder.cpp
src/bolt/bolt_messages.hpp                # Bolt 消息结构
src/bolt/bolt_session.hpp                 # 会话状态机
src/bolt/bolt_session.cpp
src/bolt/bolt_value_mapping.hpp           # Value ↔ Bolt 类型映射
src/bolt/bolt_value_mapping.cpp
src/bolt/bolt_server.hpp                  # TCP 服务端
src/bolt/bolt_server.cpp
src/query/planner/logical_plan/operator/bound_call_op.hpp   # CALL 子句逻辑算子
src/query/planner/binder/bind_call.cpp                      # CALL 子句绑定
src/query/physical_plan/operator/call_physical_op.hpp       # CALL 物理执行算子
src/query/physical_plan/operator/call_physical_op.cpp
src/query/parser/database_ddl_parser.hpp                    # 数据库 DDL 解析器
src/query/parser/database_ddl_parser.cpp
tests/test_packstream.cpp                 # PackStream 单元测试
tests/test_bolt_values.cpp                # Bolt 类型映射测试
tests/bolt/test_bolt_integration.py       # Python 驱动集成测试
```

### 修改文件
```
src/program/server/eugraph_handler.hpp     # 使用 GraphService 替代 GraphManager
src/program/server/eugraph_handler.cpp     # 委托给 GraphService
src/program/server/eugraph_server_main.cpp # 创建 GraphService + 启动 Bolt 服务端
src/query/planner/bound_logical_plan_fwd.hpp  # BoundCallOp 加入 variant
src/query/planner/binder.hpp                  # bindCall() 声明
src/query/planner/binder/binder.cpp           # CallClause/StandaloneCall 分发
src/query/physical_plan/physical_planner.cpp  # BoundCallOp 物理计划转换
src/query/optimizer/memo.cpp                  # BoundCallOp clone/children
src/query/optimizer/log_prop.hpp              # deriveCall 声明
src/query/optimizer/log_prop.cpp              # deriveCall 实现
src/query/optimizer/operator_eq.cpp           # BoundCallOp 等值比较
src/query/optimizer/operator_hash.cpp         # BoundCallOp 哈希
src/query/optimizer/column_rewrite.cpp        # BoundCallOp 槽位分配
src/query/optimizer/requirement_collector.cpp # BoundCallOp 需求收集
src/server/graph_service.hpp               # executeCypher 加入 DDL 检测, handleDatabaseDdl
src/server/graph_service.cpp               # DDL 实现, switched_database 字段
src/bolt/bolt_session.hpp                     # 移除 handleDatabaseDdl
src/bolt/bolt_session.cpp                     # 移除 DDL 拦截, 检查 switched_database
CMakeLists.txt                                # 新增源文件
```

### 零改动模块
- `src/query/executor/` — 已经协议无关
- `src/storage/data/` — 已经协议无关
- `src/storage/meta/` — 已经协议无关
- `src/storage/graph_manager.*` — 已经协议无关
- `src/query/parser/` — 处理 Cypher 字符串
- `proto/eugraph.thrift` — Thrift 协议保持不变

## 实现状态

> 最后更新：2026-08-06

### 完成度总览

**整体完成度：约 85%** — 核心功能完整，参数化谓词、事务提交、时间类型已修复。

| 模块 | 完成度 | 说明 |
|------|--------|------|
| 握手 + 版本协商 | 100% | 支持 v5.1、v5.0、v4.4，使用范围编码 |
| PackStream 编解码 | 95% | 缺少 STRUCT_32 标记（>65535 字段的结构体） |
| 会话状态机 | 100% | 6 个状态、所有转换路径已实现 |
| 消息分发 | 67% | 12 个主要消息已实现，缺 ROUTE/TELEMETRY/NOOP |
| 图实体编码 (Node/Rel/Path) | 100% | NODE(0x4E)/RELATIONSHIP(0x52)/PATH(0x50) 完整 |
| 时间类型编码 | 100% | Date/Time/DateTime/Duration 标准 Bolt 结构体编码 |
| 空间类型编码 | 0% | 类型系统无 Point 类型 |
| 多 chunk 消息 | 0% | 仅支持单 chunk (<64KB) |
| Bookmark/因果一致性 | 5% | 仅返回空 bookmark 桩 |
| 认证机制 | 5% | LOGON 接受任意凭据 |
| 分块传输编码 | 70% | 单 chunk 完整，多 chunk 不支持 |

### 已实现的消息

| 消息 | 标签 | 状态 | 说明 |
|------|------|------|------|
| HELLO | 0x01 | 完成 | 提取 user_agent，触发 LOGON，转换到 READY |
| LOGON | 0x6A | 基本 | 无认证直接通过 |
| LOGOFF | 0x6B | 基本 | 返回 SUCCESS，不执行实际会话清理 |
| RUN | 0x10 | 完成 | 执行 Cypher，构建字段元数据，状态机控制 |
| PULL | 0x3F | 完成 | 遍历异步生成器，编码 RECORD，支持 n 限制 |
| DISCARD | 0x2E | 完成 | 排空流，重置状态 |
| BEGIN | 0x11 | 完成 | 设置 in_transaction_，转换到 TX_READY |
| COMMIT | 0x12 | 完成 | 清理事务，返回 bookmark 元数据 |
| ROLLBACK | 0x13 | 完成 | 清理事务状态 |
| RESET | 0x0F | 完成 | 任意状态下重置所有状态 |
| GOODBYE | 0x02 | 完成 | 转换到 CLOSED 触发断开连接 |

### 未实现的消息

| 消息 | 标签 | 优先级 | 说明 |
|------|------|--------|------|
| ROUTE | 0x66 | 高 | 集群/路由驱动（neo4j:// 协议）无法连接 |
| TELEMETRY | 0x54 | 低 | v5.1 规范要求的遥测，不影响基本操作 |
| NOOP | 0x00 | 低 | 服务端到客户端保活，无此消息长空闲连接会断开 |

## 实现机制

### 1. 分块传输编码（Chunked Transfer Encoding）

Bolt v5.1 使用分块传输编码进行消息帧定界。每条消息由以下组成：

```
[2字节 chunk_size (大端序 uint16)] [chunk_size 字节数据] [0x00 0x00 终止符]
```

关键实现细节：
- **握手响应不分块**：来自客户端的握手（0x6060B017）及服务端版本协商响应均为原始字节，不经过 chunk 封装
- **入站解码**：`BoltConnection::processMessage()` 读取 chunk 头，验证终止符，提取数据部分后交给 `BoltSession::processMessage()` 解码 PackStream
- **出站编码**：`BoltConnection::sendResponse()` 检测数据是否已预分块（首字节为 0x00 表示 chunk 头），未分块数据自动添加 chunk 头 + 终止符
- **消息流水线**：neo4j 驱动会流水线发送 HELLO+LOGON 和 RUN+PULL，`processMessage()` 在一次 `readDataAvailable` 回调中循环处理缓冲区中的全部消息

> **已知限制**：不支持多 chunk 消息（chunk_size 后跟非零终止符）。超过 65535 字节的消息被静默丢弃。

### 2. 握手协商

```
客户端 → 服务端: 0x6060B017 (4字节 BOLT 魔数)
客户端 → 服务端: [version1, version2, version3, 0x00000000] (4个4字节版本提案)
服务端 → 客户端: [selected_version] (4字节，0x00000000 表示无匹配版本)
```

- 服务端按优先级列出支持的版本：v5.1 (0x00000501) > v5.0 > v4.4
- 选择客户端提案中第一个匹配的版本
- 使用范围编码（range encoding）：客户端在提案中填充零填充位以指示版本范围
- `BoltSession::negotiateHandshake()` 处理协商逻辑

### 3. 消息流水线（Pipelining）

neo4j 5.x 驱动会在同一个 TCP 数据包中流水线发送多个 Bolt 消息：

```
握手后首包：HELLO + LOGON   （两个消息在一个 TCP 段中）
查询执行时：RUN + PULL       （同上）
```

服务端处理策略：
- `BoltConnection::readDataAvailable()` 在一次回调中循环处理缓冲区内的所有完整消息
- 握手完成后立即检查是否有剩余数据（HELLO 消息可能在握手包中一起到达）
- 每条消息处理后调用 `sendResponse()` 但不 return，继续循环

### 4. Value ↔ Bolt 类型映射

类型映射实现在 `src/bolt/bolt_value_mapping.cpp`：

**标量类型（完整）**：
| 内部类型 | Bolt 编码 |
|---------|----------|
| `bool` | `0xC2` / `0xC3` |
| `int64_t` | `0xC8`-`0xCB` |
| `double` | `0xC1` |
| `std::string` | `0x80`-`0x8F` / `0xD0`-`0xD2` |
| `std::monostate` | `0xC0` |
| `ListValue` | `0x90`-`0x9F` / `0xD4`-`0xD6` |
| `MapValue` | `0xA0`-`0xAF` / `0xD8`-`0xDA` |

**图实体结构体（完整）**：

- **NODE**(0x4E): `struct(0x4E, [id: int, labels: list<string>, props: dict])`
  - 匿名标签（空字符串或数字前缀）被过滤
- **RELATIONSHIP**(0x52): `struct(0x52, [id: int, srcId: int, dstId: int, type: string, props: dict])`
- **PATH**(0x50): `struct(0x50, [nodes: list<NODE>, edges: list<RELATIONSHIP>, sequence: list<int>])`
  - sequence 使用基于索引的交替编码（正索引=nodes，负索引=edges）

**时间类型（标准 Bolt v5.0+ 结构体编码）**：

| 类型 | 标记 | 字段 |
|------|------|------|
| Date | 0x44 | `[days_since_epoch: int]` |
| Time | 0x54 | `[nanos: int, offset_seconds: int]` |
| LocalTime | 0x74 | `[nanos: int]` |
| DateTime | 0x49 | `[utc_seconds: int, nanos: int, offset_seconds: int]` |
| DateTimeZoneId | 0x69 | `[utc_seconds: int, nanos: int, zone_id: string]` |
| LocalDateTime | 0x64 | `[seconds: int, nanos: int]` |
| Duration | 0x45 | `[months: int, days: int, seconds: int, nanos: int]` |

> **Bolt v5.0+ 变更**: DateTime 标签从 0x46 改为 0x49，DateTimeZoneId 从 0x66 改为 0x69。
> `seconds` 字段语义从本地墙上时间（wall-clock）改为 UTC 纪元秒（local_seconds - tz_offset_sec）。
> 其他时间类型标签未变（0x44、0x54、0x74、0x64、0x45）。

### 5. 会话状态机

```
                    ┌──────────┐
                    │CONNECTING│  等待 HELLO
                    └────┬─────┘
                         │ HELLO + LOGON
                    ┌────▼─────┐
              ┌─────│  READY   │◄──────────────┐
              │     └────┬─────┘               │
              │          │ RUN         RESET   │
              │     ┌────▼─────┐   (任意状态)  │
              │     │STREAMING │───────────────┘
              │     └────┬─────┘
              │ PULL/DISCARD
              │          │
              │          ▼
              │     ┌──────────┐
              │     │  READY   │
              │     └──────────┘
              │
              │ BEGIN
              │          │
              │     ┌────▼─────┐
              │     │ TX_READY │
              │     └────┬─────┘
              │          │ RUN
              │     ┌────▼──────┐
              │     │TX_STREAMING│
              │     └────┬──────┘
              │ PULL/DISCARD
              │          │
              │          ▼
              │     ┌──────────┐
              └─────│ TX_READY │
                    └────┬─────┘
                         │ COMMIT/ROLLBACK
                         ▼
                    ┌──────────┐
                    │  READY   │
                    └──────────┘
                    
              任意状态 ──GOODBYE──► CLOSED
              任意状态 ──错误──► FAILED ──RESET──► READY
```

### 6. 连接生命周期

```
1. TCP 连接建立 → BoltServer::connectionAccepted()
2. 创建 BoltConnection → start() → setReadCB(this)
3. 客户端发送 HANDSHAKE → processHandshake() → 版本协商
4. 客户端发送 HELLO+LOGON → processMessage() → 会话进入 READY
5. 客户端发送 RUN+PULL → 执行查询、返回结果
6. 客户端发送 GOODBYE → 会话进入 CLOSED → closeConnection()
7. closeConnection() → removeConnection() + socket_->close()
```

## 测试覆盖

### C++ 单元测试（通过 ctest 运行）

| 测试套件 | 测试数 | 内容 |
|---------|--------|------|
| PackStream Test | 27 | 编解码往返测试（Null/Bool/Int/Float/String/Bytes/List/Dict/Struct） |
| Bolt Values Test | 19 | 类型映射测试（标量/VertexValue/EdgeValue/PathValue） |

### Python 集成测试

| 状态 | 数量 | 说明 |
|------|------|------|
| 通过 | 22 | 连接、查询、类型往返（含时间类型）、参数传递、事务提交/回滚 |
| xfail | 0 | — |

运行命令：
```bash
# 启用 Bolt 集成测试（需要 pytest 和 neo4j 驱动）
ctest -R bolt_integration -V

# 跳过 Bolt 集成测试
cmake -DSKIP_BOLT_INTEGRATION_TESTS=ON ..
```

> **注意**：Bolt 集成测试需要在构建机器上安装 `neo4j` 和 `pytest` Python 包。

## 手工验证测试（cypher-shell）

| 操作 | 状态 | 说明 |
|------|------|------|
| `RETURN 1 AS n` | 通过 | 基本查询 |
| `RETURN "hello"` / `3.14` / `true` | 通过 | 标量类型往返 |
| `RETURN [1,2,3]` / `{k: "v"}` | 通过 | 集合类型往返 |
| `CREATE (n:Person {name: $name})` | 通过 | 参数化创建节点 |
| `MATCH (n:Person) RETURN n` | 通过 | 无过滤匹配 |
| `CREATE (a)-[:KNOWS]->(b)` | 通过 | 创建关系 |
| `MATCH (a)-[r]->(b) RETURN a, type(r), b` | 通过 | 路径查询 |
| `RETURN count(n)` / `avg(n.age)` | 通过 | 聚合函数 |
| `ORDER BY` | 通过 | 排序 |
| MATCH 属性过滤（参数化） | 失败 | `MATCH ... {name: $name}` 不支持参数化谓词 |
| 显式事务 BEGIN→CREATE→COMMIT | 失败 | 事务提交后数据不跨 session 持久化 |

## 已知问题与待改进项

> 最后更新：2026-08-05

### 问题 1：Binder 不支持 CallClause（已修复）

**严重程度**：高（已修复） | **影响范围**：cypher-shell、所有 neo4j 5.x 驱动

**修复方案**：实现了 `BindCallOp` 逻辑算子 + `CallPhysicalOp` 物理算子，binder 完整支持 `CallClause` 绑定（包括 StandaloneCall 和 CALL 作为子句）。支持 `db.ping()` 和 `db.schema.visualization()` 两个内置存储过程。

**新增文件**：
- `src/query/planner/logical_plan/operator/bound_call_op.hpp` — BoundCallOp 叶子算子定义
- `src/query/planner/binder/bind_call.cpp` — Binder::bindCall() 实现
- `src/query/physical_plan/operator/call_physical_op.hpp/.cpp` — 物理执行算子

**相关变更**：`BoundLogicalOperator` variant 新增 `std::unique_ptr<BoundCallOp>`；optimizer 全套（memo/log_prop/operator_eq/operator_hash/column_rewrite/requirement_collector）新增 BoundCallOp 处理。

### 问题 2：Bolt 层硬编码路由到默认图（已修复）

**严重程度**：高（已修复） | **影响范围**：多数据库场景

**修复方案**：
- `BoltSession` 新增 `current_database_` 成员（默认 `"default"`），HELLO 消息中解析 `db` 字段设置当前数据库
- RUN 消息的 extra metadata 中支持 per-query `db` 字段覆盖
- 所有 `service_.executeCypher()` 调用使用 `current_database_` 路由到对应图

### 问题 3：缺少 Cypher DDL 数据库管理语句（已修复）

**严重程度**：高（已修复） | **影响范围**：数据库生命周期管理

**修复方案**：实现了 `DatabaseDdlParser`（token-based 字符串解析器），DDL 处理位于 `GraphService::executeCypher()` 中，在调用 `QueryExecutor::prepareStream()` 之前拦截。这样 Bolt 和 Thrift/eugraph-shell 两条路径都能执行 DDL。

| Cypher DDL | 实现 |
|-----------|------|
| `CREATE DATABASE <name>` | `GraphService::handleDatabaseDdl()` → `GraphManager::createGraph()` |
| `DROP DATABASE <name>` | `GraphService::handleDatabaseDdl()` → `GraphManager::dropGraph()` |
| `SHOW DATABASES` | 返回所有数据库列表（name, status, type, current） |
| `SHOW DATABASE <name>` | 返回指定数据库信息 |
| `USE <graph>` | 返回 `switched_database` 字段，由协议层（BoltSession）更新 session 状态 |

**新增文件**：`src/query/parser/database_ddl_parser.hpp/.cpp`

**关键设计**：DDL 结果通过标准 `StreamContext` 管线返回（构建 `Row` → `RowBatch` → `wrapRowBatchToChunkGenerator`），与 Index DDL 模式一致。`USE <graph>` 通过 `CypherExecutionContext::switched_database` 字段通知调用方更新会话状态。

### 问题 4：MATCH 不支持参数化谓词（已修复）

**严重程度**：中（已修复） | **影响范围**：所有带过滤条件的 MATCH 查询

**修复方案**：删除了 `bind_match.cpp` 和 `bind_merge.cpp` 中硬编码的 `containsParameter()`/`propertiesContainParameter()` 检查。`bindExpression()` 已有完整的 `Parameter` 解析支持（查找参数表并转换为 `BoundLiteral`），只需移除阻拦门禁即可。相应的 2 个 xfail 集成测试标记已移除。

### 问题 5：显式事务 COMMIT 数据不可见（已修复）

**严重程度**：中（已修复） | **影响范围**：需要事务保证的写操作

**修复方案**：
- `BoltSession` 新增 `pending_txn_` 和 `pending_store_` 成员，在 `handlePull()`/`handleDiscard()` 中 `stream_ctx_.reset()` 前保存事务句柄
- `handleCommit()` 调用 `pending_store_->commitTran(pending_txn_)` 提交数据库事务
- `handleRollback()` 调用 `pending_store_->rollbackTran()` 回滚
- `handleReset()` 清理未完成的事务
- 移除了 1 个 xfail 集成测试标记

### 问题 6：时间类型序列化为非标准字符串（已修复）

**严重程度**：中（已修复） | **影响范围**：使用时间类型的查询

**修复方案**：
- `bolt_messages.hpp` 新增 7 个时间类型标签（DATE 0x44、TIME 0x54、LOCAL_TIME 0x74、DATETIME 0x49、DATETIME_ZONE_ID 0x69、LOCAL_DATETIME 0x64、DURATION 0x45）。DATETIME/DATETIME_ZONE_ID 使用 Bolt 5.0+ 标签（原 0x46/0x66 为旧版标签，已被 cypher-shell v5.x 拒绝）
- `bolt_value_mapping.cpp` 新增 `dateTimeToStruct()`、`timeToStruct()`、`durationToStruct()` 辅助函数，替换所有 `temporalToString()` 调用。DATETIME/DATETIME_ZONE_ID 的 `seconds` 字段使用 UTC 纪元秒（local_seconds - tz_offset_sec）
- DateTimeValue 根据 `kind` 分派到 Date/LocalDateTime/DateTime/DateTimeZoneId
- 新增 7 个 C++ 单元测试 + 4 个 Python 集成测试（date/datetime/time/duration 往返）

### 问题 7：多 chunk 消息不支持

**严重程度**：中 | **影响范围**：大消息场景

**现象**：`bolt_server.cpp` 的消息读取器显式跳过非零终止符 chunk，超过 64KB 的查询或参数被静默丢弃。

```cpp
// bolt_server.cpp:165
if (term != 0) {
    spdlog::warn("[bolt] multi-chunk message not yet supported, skipping");
    read_buf_->trimStart(needed);
    continue;
}
```

### 低优先级问题

| # | 问题 | 说明 |
|---|------|------|
| 8 | **ROUTE 消息缺失** | `neo4j://` 协议连接失败，集群驱动不可用 |
| 9 | **无认证机制** | LOGON 接受任何凭据，生产不可用 |
| 10 | **Bookmark 是空字符串** | handleCommit 返回 `"bookmark": ""`，无因果一致性 |
| 11 | **空间类型缺失** | 类型系统无 Point2D/Point3D，不可能暴露给驱动 |
| 12 | **TELEMETRY/NOOP 缺失** | v5.1 规范要求的遥测和保活未实现 |
| 13 | **LOGOFF 无实际清理** | 返回 SUCCESS 但不释放资源 |
| 14 | **STRUCT_32 未实现** | PackStream 解码器只支持到 struct16 (>65535 字段的结构体不工作) |

## 改进路线图（建议）

### 第 1 批（核心可用性）✅ 已完成
1. ~~**[Binder] 支持 CallClause** — 根治 CALL db.ping() 及存储过程~~
2. ~~**[Bolt] 多数据库路由** — 解析 HELLO db 字段，路由到对应图~~
3. ~~**[Cypher] CREATE/DROP/SHOW DATABASES + USE** — DDL 语法支持~~

### 第 2 批（查询能力）✅ 已完成
4. ~~**[Binder] MATCH 参数化谓词** — 修复 2 个 xfail 测试~~
5. ~~**[事务] COMMIT 持久化** — 修复 1 个 xfail 测试~~
6. ~~**[类型] 时间类型标准编码** — DateTime/Time/Duration 的 Bolt 结构体输出~~

### 第 3 批（生产加固）✅ 已完成
7. ~~**多 chunk 消息** — 支持 >64KB 查询/参数~~
8. ~~**基本认证** — BASIC auth scheme~~
9. ~~**因果一致性** — Bookmark 生成与验证~~

### 第 4 批（完整协议）
10. ~~**TELEMETRY/NOOP** — 规范合规~~（NOOP 已被 chunk terminator 处理）
11. ~~**ROUTE 消息** — 返回指向自身的固定路由表，支持 neo4j:// 协议~~（单机桩）
12. **空间类型** — Point 类型系统 + Bolt 编码（暂不需要）

## 参考

- [Neo4j Bolt Protocol Specification v5.1](https://neo4j.com/docs/bolt/current/)
- [ArcadeDB Bolt Plugin](https://docs.arcadedb.com/) — Java 参考实现
- [Memgraph](https://memgraph.com/) — C++ 图数据库 Bolt 实现
- [libneo4j-client](https://github.com/cleishm/libneo4j-client) — C Bolt 客户端库（PackStream 参考）
- [packstream](https://github.com/neo4j-packstream/neo4j-packstream-specification-v1)
