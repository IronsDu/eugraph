# EuGraph 文档导航

> 本文档为 EuGraph 项目的知识入口，按任务类型路由到对应文档。

## 项目概述

- **定位**：分布式图数据库，支持 Cypher 查询语言
- **语言**：C++20
- **存储引擎**：WiredTiger
- **协程库**：folly
- **RPC 框架**：fbthrift

## 请求处理流程

```
Shell/Client（src/shell/）
  → fbthrift RPC（src/server/，EuGraphHandler）
    → QueryExecutor（src/compute_service/executor/）：编排全流程
      → CypherQueryParser（src/compute_service/parser/）：字符串 → AST
      → LogicalPlanBuilder（src/compute_service/logical_plan/）：AST → LogicalPlan
      → PhysicalPlanner（src/compute_service/physical_plan/）：LogicalPlan → PhysicalPlan
      → 协程执行器（Pull-based 火山模型，1024 行/批）
        → 物理算子 → AsyncGraphDataStore（src/storage/data/）
          → IoScheduler（src/storage/io_scheduler.hpp）→ SyncGraphDataStore（src/storage/data/）
            → WiredTiger
  ← 结果通过 AsyncGenerator<Row> 流式返回
```

DDL 操作（CREATE GRAPH / DROP LABEL 等）由 `EuGraphHandler` 直接协调元数据服务，不经过查询引擎。

## 模块职责速查

| 模块 | 职责 | 关键类/接口 | 源码 | 设计文档 |
|------|------|-----------|------|---------|
| 类型系统 | 基础类型定义 | `VertexId`, `EdgeId`, `Properties`, `PropertyValue`, `IndexState` | `src/common/types/` | [architecture/type-definitions.md] |
| Sync 数据存储 | WiredTiger 同步 CRUD，物理表管理，事务 | `ISyncGraphDataStore`, `SyncGraphDataStore` | `src/storage/data/` | [storage/interfaces.md] |
| Async 数据存储 | 协程包装，IO 线程池调度 | `IAsyncGraphDataStore`, `AsyncGraphDataStore` | `src/storage/data/` | [storage/interfaces.md] |
| Sync 元数据存储 | 元数据 KV 持久化（独立 WT 连接） | `ISyncGraphMetaStore`, `SyncGraphMetaStore` | `src/storage/meta/` | [storage/interfaces.md], [storage/metadata-service-design.md] |
| Async 元数据服务 | Label/EdgeLabel/索引管理、ID 分配、GraphSchema | `IAsyncGraphMetaStore`, `AsyncGraphMetaStore`, `GraphSchema` | `src/storage/meta/`, `src/storage/graph_schema.hpp` | [storage/metadata-service-design.md] |
| KV 编解码 | Key/Value 序列化（多表 WT，无前缀字节） | `KeyCodec`, `ValueCodec`, `IndexKeyCodec` | `src/storage/kv/` | [storage/kv-encoding.md] |
| IO 调度器 | IO 线程池封装，协程调度 | `IoScheduler` | `src/storage/io_scheduler.hpp` | [architecture/system-architecture.md] |
| 存储基类 | WT 连接管理、session/cursor 缓存 | `WtStoreBase` | `src/storage/wt_store_base.hpp` | [architecture/system-architecture.md] |
| Cypher 解析器 | ANTLR4 语法，Parse Tree → AST | `CypherQueryParser`, `AstBuilder` | `src/compute_service/parser/` | [query/syntax/cypher-parser-design.md] |
| 逻辑计划 | AST → `LogicalPlan`（算子树，Visitor 扩展点） | `LogicalPlanBuilder`, `LogicalOperator` | `src/compute_service/logical_plan/` | [query/engine/query-engine-design.md] |
| 物理计划 | 逻辑算子 → 物理算子（ID 解析、存储引用绑定） | `PhysicalPlanner`, `PhysicalOperator` | `src/compute_service/physical_plan/` | [query/engine/query-engine-design.md] |
| 查询执行器 | 协程管道执行，事务管理，表达式求值 | `QueryExecutor`, `ExpressionEvaluator` | `src/compute_service/executor/` | [query/engine/query-engine-design.md], [query/engine/execution-model.md] |
| RPC 服务 | fbthrift 服务端，DDL 协调，流式 DML | `EuGraphHandler` | `src/server/` | [service/rpc-service.md] |
| Shell | 交互式 REPL（embedded / RPC 双模式） | `ShellRepl`, `EuGraphRpcClient` | `src/shell/` | [program_design/server-shell-design.md] |
| Thrift IDL | RPC 接口定义 | - | `proto/eugraph.thrift` | [service/rpc-service.md] |

## 任务路由

| 任务类型 | 先读文档 | 关键源码 |
|---------|---------|---------|
| 添加/修改 Cypher 语法 | [query/syntax/cypher-syntax.md], [query/syntax/cypher-parser-design.md], [query/engine/query-engine-design.md] | `src/compute_service/parser/`, `logical_plan/` |
| 添加查询算子 | [query/engine/query-engine-design.md] | `src/compute_service/physical_plan/`, `executor/` |
| 修改 KV 存储编码 | [storage/kv-encoding.md] | `src/storage/kv/` |
| 添加 DDL 操作 | [storage/ddl.md], [storage/metadata-service-design.md] | `src/storage/meta/` |
| 修改类型系统 | [architecture/type-definitions.md], [storage/kv-encoding.md] | `src/common/types/` |
| 添加/修改 RPC 接口 | [service/rpc-service.md] | `proto/eugraph.thrift`, `src/server/` |
| 修改 Shell 交互 | [program_design/server-shell-design.md] | `src/shell/` |
| 添加二级索引 | [storage/index_design.md], [storage/kv-encoding.md] | `src/storage/` |
| 数据导入/CSV 加载 | [program_usage/loader.md], [program_design/loader-design.md] | `src/loader/` |
| 事务/MVCC | [query/engine/transaction-model.md], [storage/interfaces.md] | `src/storage/` |
| 执行模型/协程/流式 | [query/engine/execution-model.md], [query/engine/query-engine-design.md] | `src/storage/io_scheduler.hpp`, `executor/` |
| 元数据/Schema 管理 | [storage/metadata-service-design.md] | `src/storage/meta/`, `src/storage/graph_schema.hpp` |
| 表达式求值相关 | [query/syntax/cypher-syntax.md], [query/engine/query-engine-design.md] | `executor/expression_evaluator.cpp` |

## 构建与运行

| 文档 | 说明 |
|------|------|
| [构建指南](build/build-guide.md) | 前置条件、CMake Presets、ASan/UBSan 构建、测试、代码格式化 |
| [Server 使用](program_usage/server.md) | eugraph-server 启动参数与典型流程 |
| [Shell 使用](program_usage/shell.md) | eugraph-shell 启动参数、命令列表、交互示例 |
| [CSV Loader 使用](program_usage/loader.md) | LDBC SNB 格式导入、schema 推断、批量 RPC 插入 |

## 架构与设计

| 文档 | 说明 |
|------|------|
| [系统架构](architecture/system-architecture.md) | 分层架构、sync/async 分离、线程模型、IoScheduler |
| [数据模型](architecture/data-model.md) | Vertex/Edge 概念、Label/EdgeLabel 映射、属性模型、PK、运行时类型 |
| [类型定义](architecture/type-definitions.md) | C++ 核心类型（ID、属性值、LabelDef/EdgeLabelDef、运行时类型） |

## 存储引擎

| 文档 | 说明 |
|------|------|
| [KV 编码](storage/kv-encoding.md) | WT 多表架构、Key/Value 编码格式、二级索引编码 |
| [接口设计](storage/interfaces.md) | sync/async 分离接口 |
| [元数据服务](storage/metadata-service-design.md) | AsyncGraphMetaStore、GraphSchema、索引元数据、batch ID 分配 |
| [二级索引](storage/index_design.md) | B-tree 索引、IndexKeyCodec、状态机、IndexScan 优化 |
| [DDL 设计](storage/ddl.md) | 删除标签类型、删除关系类型、列的增删改（设计规划） |
| [多图支持](storage/multi-graph.md) | 设计阶段，尚未实现 |

## 查询引擎

### 语法解析

| 文档 | 说明 |
|------|------|
| [Cypher 语法参考](query/syntax/cypher-syntax.md) | 支持的 Cypher 语法边界（子句、表达式、聚合、索引 DDL） |
| [AST 参考](query/syntax/ast-reference.md) | AST 节点层次与类型速查 |
| [Cypher 解析器](query/syntax/cypher-parser-design.md) | ANTLR4 集成、AST 构建策略、设计决策 |

### 执行引擎

| 文档 | 说明 |
|------|------|
| [查询引擎](query/engine/query-engine-design.md) | 逻辑计划、物理计划、表达式求值、QueryExecutor |
| [执行模型](query/engine/execution-model.md) | Pull-based 火山模型、协程调度、IO/Compute 分离、流式执行、关键不变量 |
| [事务模型](query/engine/transaction-model.md) | 事务生命周期、snapshot isolation、流式/嵌入式事务差异 |

## RPC 服务

| 文档 | 说明 |
|------|------|
| [RPC 服务层](service/rpc-service.md) | fbthrift Handler、流式执行协议、Value/Thrift 转换、RPC 客户端 |

## 可执行程序

### 使用文档

| 文档 | 说明 |
|------|------|
| [Server 使用](program_usage/server.md) | 启动参数与典型流程 |
| [Shell 使用](program_usage/shell.md) | 启动参数、命令、交互示例 |
| [Loader 使用](program_usage/loader.md) | CSV 数据格式、启动参数 |

### 设计文档

| 文档 | 说明 |
|------|------|
| [Server + Shell 设计](program_design/server-shell-design.md) | 启动流程、双模式架构、Shell REPL 设计 |
| [Loader 设计](program_design/loader-design.md) | 装载流程、服务端批量端点、错误处理 |

## 调试

| 文档 | 说明 |
|------|------|
| [调试笔记](debugging/debugging-notes.md) | 典型 bug 排查过程与经验总结 |
