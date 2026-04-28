# EuGraph 文档导航

本文档为 EuGraph 项目的知识入口，所有技术性文档均在此导航。

## 项目概述

- **定位**：分布式图数据库，支持 Cypher 查询语言
- **语言**：C++20
- **存储引擎**：WiredTiger
- **协程库**：folly
- **RPC 框架**：fbthrift

## 模块职责速查

接到模糊或跨模块需求时，通过此表建立心智模型，推理涉及的模块，再查阅对应设计文档。

| 模块 | 职责 | 关键类/接口 | 源码 | 设计文档 |
|------|------|-----------|------|---------|
| 类型系统 | 基础类型定义（ID、属性值、错误码、常量） | `VertexId`, `EdgeId`, `Properties`, `PropertyValue` | `src/common/types/` | [type-definitions.md] |
| Sync 数据存储 | WiredTiger 同步 CRUD，物理表管理，事务 | `ISyncGraphDataStore`, `SyncGraphDataStore` | `src/storage/data/` | [interfaces.md] |
| Async 数据存储 | 协程包装，IO 线程池调度（header-only） | `IAsyncGraphDataStore`, `AsyncGraphDataStore` | `src/storage/data/` | [interfaces.md] |
| Sync 元数据存储 | 元数据 KV 持久化（独立 WT 连接于 `{db}/meta/`） | `ISyncGraphMetaStore`, `SyncGraphMetaStore` | `src/storage/meta/` | [interfaces.md], [metadata-service-design.md] |
| Async 元数据服务 | Label/EdgeLabel 管理、ID 分配、内存 `GraphSchema` | `IAsyncGraphMetaStore`, `AsyncGraphMetaStore`, `GraphSchema` | `src/storage/meta/`, `src/storage/graph_schema.hpp` | [metadata-service-design.md] |
| KV 编解码 | Key/Value 序列化格式（L/I/P/R/X/E/D 前缀） | `KeyCodec`, `ValueCodec` | `src/storage/kv/` | [kv-encoding.md] |
| IO 调度器 | IO 线程池封装（`IOThreadPoolExecutor`），协程调度 | `IoScheduler` | `src/storage/io_scheduler.hpp` | [system-architecture.md] |
| 存储基类 | WT 连接管理、session/cursor 缓存、KV 操作 | `WtStoreBase` | `src/storage/wt_store_base.hpp` | [system-architecture.md] |
| Cypher 解析器 | ANTLR4 语法定义，Parse Tree → AST | `CypherQueryParser`, `AstBuilder` | `src/compute_service/parser/` | [cypher-parser-design.md] |
| 逻辑计划 | AST → `LogicalPlan`（字符串名称，算子树，Visitor 扩展点） | `LogicalPlanBuilder`, `LogicalOperator` | `src/compute_service/logical_plan/` | [query-engine-design.md] |
| 物理计划 | 逻辑算子 → 物理算子（ID 解析、存储引用绑定） | `PhysicalPlanner`, `PhysicalOperator` | `src/compute_service/physical_plan/` | [query-engine-design.md] |
| 查询执行器 | 协程管道执行（Pull-based 火山模型，1024 行/批），事务管理，表达式求值 | `QueryExecutor`, `ExpressionEvaluator` | `src/compute_service/executor/` | [query-engine-design.md] |
| RPC 服务 | fbthrift 服务端，DDL 协调（meta → data），DML 委托 QueryExecutor | `EuGraphHandler` | `src/server/` | [server-shell-design.md] |
| Shell | 交互式 REPL（embedded / RPC 双模式） | `ShellRepl`, `EuGraphRpcClient` | `src/shell/` | [server-shell-design.md] |
| Thrift IDL | RPC 接口定义（DDL + DML + 索引操作） | - | `proto/eugraph.thrift` | [server-shell-design.md] |

## 任务路由

接到精确开发任务时，按此表定位入口文档和源码目录。

| 任务类型 | 先读文档 | 关键源码 |
|---------|---------|---------|
| 添加/修改 Cypher 语法（新子句、新表达式） | [cypher-parser-design.md], [query-engine-design.md] | `src/compute_service/parser/`, `logical_plan/` |
| 添加查询算子（新物理/逻辑算子） | [query-engine-design.md] | `src/compute_service/physical_plan/`, `executor/` |
| 修改 KV 存储编码格式 | [kv-encoding.md], [interfaces.md] | `src/storage/kv/` |
| 添加 DDL 操作（创建/删除标签、列操作） | [ddl.md], [metadata-service-design.md] | `src/storage/meta/` |
| 修改类型系统（属性类型、ID 类型） | [type-definitions.md], [kv-encoding.md] | `src/common/types/` |
| 添加/修改 RPC 接口 | [server-shell-design.md] | `proto/eugraph.thrift`, `src/server/` |
| 修改 Shell 交互 | [server-shell-design.md] | `src/shell/` |
| 添加二级索引 | [index_design.md], [kv-encoding.md] | `src/storage/` |
| 数据导入/CSV 加载 | [loader-design.md] | 独立 loader 工具 |
| 事务/MVCC | [system-architecture.md], [interfaces.md] | `src/storage/` |
| 性能优化（IO/协程/批处理） | [system-architecture.md], [query-engine-design.md] | `src/storage/io_scheduler.hpp`, `executor/` |
| 元数据/Schema 管理 | [metadata-service-design.md] | `src/storage/meta/`, `src/storage/graph_schema.hpp` |
| 多图支持（图创建/删除/恢复） | [multi-graph.md] | `src/storage/` |

## 构建与运行

| 文档 | 说明 |
|------|------|
| [构建与运行指南](build-guide.md) | 前置条件、vcpkg 子模块配置、CMake Presets、ASan/UBSan 构建、运行测试、代码格式化 |
| [使用指南](usage.md) | eugraph-server、eugraph-shell、eugraph-loader 的启动参数和典型流程 |

## 架构与设计

| 文档 | 说明 |
|------|------|
| [系统架构](system-architecture.md) | 分层架构、sync/async 分离、线程模型、IoScheduler |
| [数据模型](data-model.md) | Vertex/Edge 概念、Label/EdgeLabel ID 映射 |
| [KV 编码](kv-encoding.md) | Key 前缀定义、详细编码规则、查询能力总结 |
| [类型定义](type-definitions.md) | C++ 核心类型（ID、属性值、Vertex、Edge 等） |
| [接口设计](interfaces.md) | sync/async 分离接口（ISyncGraphDataStore、IAsyncGraphDataStore、IAsyncGraphMetaStore） |
| [目录结构](directory-structure.md) | 源码目录说明 |

## 功能模块

| 文档 | 说明 |
|------|------|
| [元数据服务](metadata-service-design.md) | AsyncGraphMetaStore、GraphSchema、MetadataCodec |
| [多图支持](multi-graph.md) | WiredTiger 多表隔离、图创建/删除、启动恢复 |
| [DDL 设计](ddl.md) | 删除标签类型、删除关系类型、列的增删改 |
| [二级索引](index_design.md) | B-tree 索引、IndexKeyCodec、IndexScan、DDL 异步执行 |
| [CSV Loader](loader-design.md) | LDBC SNB 格式导入、schema 推断、批量 RPC 插入 |

## 查询引擎

| 文档 | 说明 |
|------|------|
| [Cypher 解析器](cypher-parser-design.md) | ANTLR4 语法、AST 节点定义、解析器 API |
| [查询引擎](query-engine-design.md) | 逻辑计划、物理计划、协程执行模型、IO/计算分离 |
| [Server + Shell](server-shell-design.md) | fbthrift 服务层 + 交互式 Shell |

## 项目规划

- **进行中**：fbthrift 服务层（RPC）
- **下一步**：MVCC、DDL 实现

| 文档 | 说明 |
|------|------|
| [开发阶段](development-phases.md) | 里程碑规划、任务拆解、各阶段目标 |
| [设计调整记录](design-decisions.md) | 历史设计决策变更及原因 |

## 开发经验

| 文档 | 说明 |
|------|------|
| [调试笔记](debugging-notes.md) | 典型 bug 排查过程与经验总结（use-after-move、prop_id 索引、协程引用生命周期等） |

[type-definitions.md]: type-definitions.md
[interfaces.md]: interfaces.md
[metadata-service-design.md]: metadata-service-design.md
[kv-encoding.md]: kv-encoding.md
[system-architecture.md]: system-architecture.md
[cypher-parser-design.md]: cypher-parser-design.md
[query-engine-design.md]: query-engine-design.md
[server-shell-design.md]: server-shell-design.md
[ddl.md]: ddl.md
[index_design.md]: index_design.md
[loader-design.md]: loader-design.md
[multi-graph.md]: multi-graph.md
