# 开发阶段规划 [已归档]

> 本文档已归档，里程碑均已完成。保留仅供历史参考。
> 当前实现状态见各设计文档中的"实现状态"章节。

## 阶段一: 基础框架 (MVP) — 已完成

### 1.1 项目骨架搭建 ✅

- CMakeLists.txt 配置
- src/ 目录结构
- 基础类型定义 (graph_types.hpp, constants.hpp)
- 依赖引入 (folly, spdlog, GoogleTest, WiredTiger)

### 1.2 KV 存储引擎集成 ✅

- WiredTiger 封装 (WtStoreBase)
- Key/Value 编解码 (KeyCodec/ValueCodec)

### 1.3 图存储层 ✅

- ISyncGraphDataStore 接口 + SyncGraphDataStore 实现
- Vertex/Edge CRUD、标签管理、边遍历、事务

### 1.4 元数据服务 ✅

- ISyncGraphMetaStore + SyncGraphMetaStore
- IAsyncGraphMetaStore + AsyncGraphMetaStore
- GraphSchema 内存对象
- MetadataCodec 序列化

### 1.5 存储层重构 ✅

- sync/async 分离：sync 层直接封装 WT，async 层通过 IoScheduler 调度
- meta/data 分离：独立 WT 连接，独立目录
- compute 层零 sync 依赖
- IoScheduler 统一 IO 调度

### 1.6 Cypher 解析器 ✅

- ANTLR4 语法 → AST
- 支持：MATCH, CREATE, WHERE, RETURN, LIMIT

### 1.7 查询引擎 ✅

- 逻辑计划构建 (LogicalPlanBuilder)
- 物理计划 + 协程执行 (PhysicalPlanner, PhysicalOperator)
- QueryExecutor (parse → plan → execute)
- 8 种物理算子：AllNodeScan, LabelScan, Expand, Filter, Project, Limit, CreateNode, CreateEdge

### 1.8 Server + Shell ✅

- fbthrift 服务端 (EuGraphHandler)
- 交互式 Shell (embedded + RPC 双模式)
- RPC 集成测试

## 阶段二: 查询能力增强 — 规划中

| 里程碑 | 内容 | 说明 |
|--------|------|------|
| M4 | 基础读增强 | ORDER BY, DISTINCT, 聚合 (count/sum/avg), SKIP |
| M5 | 写操作完善 | SET, DELETE, MERGE, 批量 CREATE |
| M6 | 多 MATCH + JOIN | 多个 MATCH 子句, HashJoin, Apply |
| M7 | 查询优化器 | 谓词下推, 投影裁剪, 基于 LogicalPlanVisitor |
| M8 | WITH 子句 | 流水线断点, 变量作用域重置 |

## 阶段三: 分布式扩展 — 待规划

- RPC 通信优化
- 分布式事务（2PC）
- 元数据中心化

## 测试覆盖

当前测试统计（共 151+ 测试）：

| 测试文件 | 数量 | 覆盖范围 |
|----------|------|----------|
| test_graph_store_wt.cpp | 79 | WT 图存储 CRUD、事务、索引 |
| test_metadata_service.cpp | 17 | 元数据 CRUD、ID 分配、持久化、序列化 |
| test_query_executor.cpp | 47 | 端到端 Cypher 查询执行 |
| test_rpc_integration.cpp | 8 | RPC 服务端到端 |
| test_cypher_parser.cpp | — | Cypher 解析器 |
| test_logical_plan.cpp | — | 逻辑计划构建 |
| test_key_codec.cpp | — | KV 编解码 |
| test_value_codec.cpp | — | Value 编解码 |

## 依赖库

| 组件    | 选择          | 说明           |
| ----- | ----------- | ------------ |
| 构建系统  | CMake       | C++ 标准选择     |
| 协程库   | folly       | Meta 成熟协程库   |
| RPC   | fbthrift    | folly 原生支持   |
| KV 存储 | WiredTiger  | 高性能、支持事务     |
| 日志库   | spdlog      | 高性能、C++20 友好 |
| 测试框架  | GoogleTest  | 成熟稳定         |
