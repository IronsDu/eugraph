# EuGraph 文档导航

本文档为 EuGraph 项目的知识入口，所有技术性文档均在此导航。

## 项目概述

- **定位**：分布式图数据库，支持 Cypher 查询语言
- **语言**：C++20
- **存储引擎**：WiredTiger
- **协程库**：folly
- **RPC 框架**：fbthrift

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

| 文档 | 说明 |
|------|------|
| [开发阶段](development-phases.md) | 里程碑规划、任务拆解、各阶段目标 |
