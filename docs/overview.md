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
| 类型系统 | 基础类型定义 | `VertexId`, `EdgeId`, `Properties`, `PropertyValue`, `IndexState` | `src/common/types/` | [type-definitions.md] |
| Sync 数据存储 | WiredTiger 同步 CRUD，物理表管理，事务 | `ISyncGraphDataStore`, `SyncGraphDataStore` | `src/storage/data/` | [interfaces.md] |
| Async 数据存储 | 协程包装，IO 线程池调度 | `IAsyncGraphDataStore`, `AsyncGraphDataStore` | `src/storage/data/` | [interfaces.md] |
| Sync 元数据存储 | 元数据 KV 持久化（独立 WT 连接） | `ISyncGraphMetaStore`, `SyncGraphMetaStore` | `src/storage/meta/` | [interfaces.md], [metadata-service-design.md] |
| Async 元数据服务 | Label/EdgeLabel/索引管理、ID 分配、GraphSchema | `IAsyncGraphMetaStore`, `AsyncGraphMetaStore`, `GraphSchema` | `src/storage/meta/`, `src/storage/graph_schema.hpp` | [metadata-service-design.md] |
| KV 编解码 | Key/Value 序列化（多表 WT，无前缀字节） | `KeyCodec`, `ValueCodec`, `IndexKeyCodec` | `src/storage/kv/` | [kv-encoding.md] |
| IO 调度器 | IO 线程池封装，协程调度 | `IoScheduler` | `src/storage/io_scheduler.hpp` | [system-architecture.md] |
| 存储基类 | WT 连接管理、session/cursor 缓存 | `WtStoreBase` | `src/storage/wt_store_base.hpp` | [system-architecture.md] |
| Cypher 解析器 | ANTLR4 语法，Parse Tree → AST | `CypherQueryParser`, `AstBuilder` | `src/compute_service/parser/` | [cypher-parser-design.md] |
| 逻辑计划 | AST → `LogicalPlan`（算子树，Visitor 扩展点） | `LogicalPlanBuilder`, `LogicalOperator` | `src/compute_service/logical_plan/` | [query-engine-design.md] |
| 物理计划 | 逻辑算子 → 物理算子（ID 解析、存储引用绑定） | `PhysicalPlanner`, `PhysicalOperator` | `src/compute_service/physical_plan/` | [query-engine-design.md] |
| 查询执行器 | 协程管道执行，事务管理，表达式求值 | `QueryExecutor`, `ExpressionEvaluator` | `src/compute_service/executor/` | [query-engine-design.md], [execution-model.md] |
| RPC 服务 | fbthrift 服务端，DDL 协调，流式 DML | `EuGraphHandler` | `src/server/` | [server-shell-design.md] |
| Shell | 交互式 REPL（embedded / RPC 双模式） | `ShellRepl`, `EuGraphRpcClient` | `src/shell/` | [server-shell-design.md] |
| Thrift IDL | RPC 接口定义 | - | `proto/eugraph.thrift` | [server-shell-design.md] |

## 任务路由

| 任务类型 | 先读文档 | 关键源码 |
|---------|---------|---------|
| 添加/修改 Cypher 语法 | [cypher-syntax.md], [cypher-parser-design.md], [query-engine-design.md] | `src/compute_service/parser/`, `logical_plan/` |
| 添加查询算子 | [query-engine-design.md] | `src/compute_service/physical_plan/`, `executor/` |
| 修改 KV 存储编码 | [kv-encoding.md] | `src/storage/kv/` |
| 添加 DDL 操作 | [ddl.md], [metadata-service-design.md] | `src/storage/meta/` |
| 修改类型系统 | [type-definitions.md], [kv-encoding.md] | `src/common/types/` |
| 添加/修改 RPC 接口 | [server-shell-design.md] | `proto/eugraph.thrift`, `src/server/` |
| 修改 Shell 交互 | [server-shell-design.md] | `src/shell/` |
| 添加二级索引 | [index_design.md], [kv-encoding.md] | `src/storage/` |
| 数据导入/CSV 加载 | [loader-design.md] | `src/loader/` |
| 事务/MVCC | [transaction-model.md], [interfaces.md] | `src/storage/` |
| 执行模型/协程/流式 | [execution-model.md], [query-engine-design.md] | `src/storage/io_scheduler.hpp`, `executor/` |
| 元数据/Schema 管理 | [metadata-service-design.md] | `src/storage/meta/`, `src/storage/graph_schema.hpp` |
| 表达式求值相关 | [cypher-syntax.md], [query-engine-design.md] | `executor/expression_evaluator.cpp` |

## 构建与运行

| 文档 | 说明 |
|------|------|
| [构建与运行指南](build-guide.md) | 前置条件、CMake Presets、ASan/UBSan 构建、测试、代码格式化 |
| [使用指南](usage.md) | eugraph-server、eugraph-shell、eugraph-loader 的启动参数和典型流程 |

## 架构与设计

| 文档 | 说明 |
|------|------|
| [系统架构](system-architecture.md) | 分层架构、sync/async 分离、线程模型、IoScheduler |
| [数据模型](data-model.md) | Vertex/Edge 概念、Label/EdgeLabel 映射、属性模型、PK、运行时类型 |
| [KV 编码](kv-encoding.md) | WT 多表架构、Key/Value 编码格式、二级索引编码 |
| [类型定义](type-definitions.md) | C++ 核心类型（ID、属性值、LabelDef/EdgeLabelDef、运行时类型） |
| [接口设计](interfaces.md) | sync/async 分离接口 |
| [目录结构](directory-structure.md) | 源码目录说明 |

## 运行时模型

| 文档 | 说明 |
|------|------|
| [执行模型](execution-model.md) | Pull-based 火山模型、协程调度、IO/Compute 分离、流式执行、关键不变量 |
| [事务模型](transaction-model.md) | 事务生命周期、snapshot isolation、流式/嵌入式事务差异 |

## 功能模块

| 文档 | 说明 |
|------|------|
| [元数据服务](metadata-service-design.md) | AsyncGraphMetaStore、GraphSchema、索引元数据、batch ID 分配 |
| [多图支持](multi-graph.md) | 设计阶段，尚未实现 |
| [DDL 设计](ddl.md) | 删除标签类型、删除关系类型、列的增删改（设计规划） |
| [二级索引](index_design.md) | B-tree 索引、IndexKeyCodec、状态机、IndexScan 优化 |
| [CSV Loader](loader-design.md) | LDBC SNB 格式导入、schema 推断、批量 RPC 插入 |

## 查询引擎

| 文档 | 说明 |
|------|------|
| [Cypher 语法参考](cypher-syntax.md) | 支持的 Cypher 语法边界（子句、表达式、聚合、索引 DDL） |
| [AST 参考](ast-reference.md) | AST 节点层次与类型速查 |
| [Cypher 解析器](cypher-parser-design.md) | ANTLR4 集成、AST 构建策略、设计决策 |
| [查询引擎](query-engine-design.md) | 逻辑计划、物理计划、表达式求值、QueryExecutor |
| [Server + Shell](server-shell-design.md) | fbthrift 服务层 + 交互式 Shell（含流式执行） |

## 历史参考

| 文档 | 说明 |
|------|------|
| [设计调整记录](design-decisions.md) | 历史设计决策变更及原因 |
| [调试笔记](debugging-notes.md) | 典型 bug 排查过程与经验总结 |

[type-definitions.md]: type-definitions.md
[interfaces.md]: interfaces.md
[metadata-service-design.md]: metadata-service-design.md
[kv-encoding.md]: kv-encoding.md
[system-architecture.md]: system-architecture.md
[cypher-syntax.md]: cypher-syntax.md
[ast-reference.md]: ast-reference.md
[cypher-parser-design.md]: cypher-parser-design.md
[query-engine-design.md]: query-engine-design.md
[execution-model.md]: execution-model.md
[transaction-model.md]: transaction-model.md
[server-shell-design.md]: server-shell-design.md
[ddl.md]: ddl.md
[index_design.md]: index_design.md
[loader-design.md]: loader-design.md
[multi-graph.md]: multi-graph.md
