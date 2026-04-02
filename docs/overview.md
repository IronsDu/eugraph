# EuGraph 图数据库

## 项目概述

- **名称**: EuGraph
- **定位**: 分布式图数据库，支持 GSQL 查询语言
- **语言**: C++20
- **存储引擎**: RocksDB (KV 存储)
- **协程库**: folly
- **RPC 框架**: fbthrift

## 核心特性

| 特性   | 说明                     |
| ---- | ---------------------- |
| 查询语言 | GSQL (类 SQL 的图查询语言)    |
| 事务模型 | MVCC + 2PC (分布式)       |
| 隔离级别 | 可重复读 (REPEATABLE_READ) |
| 部署模式 | 单机 → 分布式               |
| 架构模式 | 模块化、服务化                |

## 文档导航

| 文档 | 说明 |
|------|------|
| [系统架构](system-architecture.md) | 架构总览、服务定义（存储/计算/元数据）、服务组合模式 |
| [数据模型](data-model.md) | Vertex/Edge 概念、Label/EdgeLabel ID 映射 |
| [KV 编码](kv-encoding.md) | Key 前缀定义、详细编码规则、查询能力总结 |
| [类型定义](type-definitions.md) | C++ 核心类型（ID、属性值、Vertex、Edge 等） |
| [接口设计](interfaces.md) | 抽象接口（IGraphOperations、IMetadataService、IComputeService） |
| [目录结构](directory-structure.md) | 源码目录规划 |
| [开发阶段](development-phases.md) | 里程碑规划、任务拆解、测试用例、依赖库 |
