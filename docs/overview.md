# EuGraph 图数据库

## 项目概述

- **名称**: EuGraph
- **定位**: 分布式图数据库，支持 Cypher 查询语言
- **语言**: C++20
- **存储引擎**: WiredTiger
- **协程库**: folly
- **RPC 框架**: fbthrift

## 核心特性

| 特性   | 说明                     |
| ---- | ---------------------- |
| 查询语言 | Cypher (OpenCypher 子集)   |
| 事务模型 | MVCC + 2PC (分布式)       |
| 隔离级别 | 可重复读 (REPEATABLE_READ) |
| 部署模式 | 单机 → 分布式               |
| 架构模式 | 存算分离、sync/async 分层    |

## 文档导航

| 文档 | 说明 |
|------|------|
| [系统架构](system-architecture.md) | 分层架构、sync/async 分离、线程模型、IoScheduler |
| [数据模型](data-model.md) | Vertex/Edge 概念、Label/EdgeLabel ID 映射 |
| [KV 编码](kv-encoding.md) | Key 前缀定义、详细编码规则、查询能力总结 |
| [类型定义](type-definitions.md) | C++ 核心类型（ID、属性值、Vertex、Edge 等） |
| [接口设计](interfaces.md) | sync/async 分离接口（ISyncGraphDataStore、IAsyncGraphDataStore、IAsyncGraphMetaStore） |
| [元数据服务设计](metadata-service-design.md) | AsyncGraphMetaStore、GraphSchema、MetadataCodec |
| [目录结构](directory-structure.md) | 源码目录说明 |
| [多图支持](multi-graph.md) | 列簇隔离、图创建/删除、启动恢复 |
| [DDL 设计](ddl.md) | 删除标签类型、删除关系类型、列的增删改 |
| [Cypher 解析器](cypher-parser-design.md) | ANTLR4 语法、AST 节点定义、解析器 API |
| [查询引擎](query-engine-design.md) | 逻辑计划、物理计划、协程执行模型、IO/计算分离 |
| [Server + Shell](server-shell-design.md) | fbthrift 服务层 + 交互式 Shell |
| [开发阶段](development-phases.md) | 里程碑规划、任务拆解 |
